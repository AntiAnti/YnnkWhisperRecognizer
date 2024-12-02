// (c) Yuri N. K. 2024. All rights reserved.
// ykasczc@gmail.com


#include "WhisperSubsystem.h"
#include "Async/Async.h"
#include "Containers/StringConv.h"
#include "AudioResampler.h"
#include "WhisperSubsystem.h"
#include "YnnkVoiceLipsyncModule.h"
#include "AsyncRecognizer.h"
#include "YnnkWhisperSettings.h"
#include "ZipUFSArchive.h"
#include "Engine/AssetManager.h"
#include "Misc/Paths.h"

//#include "GenericPlatform/GenericPlatformFile.h"
#if PLATFORM_ANDROID
#include "HAL/PlatformProcess.h"
#include "Android/AndroidPlatformFile.h"
#include "Misc/App.h"
#endif

#include <filesystem>
#include <fstream>

#include "WhisperPrivate.h"

DEFINE_LOG_CATEGORY(LogWhisper);

namespace WhisperCallback
{
	void NewTextSegmentCallback(whisper_context* WhisperContext, whisper_state* WhisperState, int NewSegmentCount, void* UserData);
	bool EncoderBeginCallback(whisper_context* WhisperContext, whisper_state* WhisperState, void* UserData);
	bool EncoderAbortCallback(void* UserData);
	void ProgressCallback(whisper_context* WhisperContext, whisper_state* WhisperState, int Progress, void* UserData);
}

void UWhisperSubsystem::NormalizePath(FString& Path)
{
	Path.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
	Path.ReplaceInline(TEXT("//"), TEXT("/"), ESearchCase::CaseSensitive);
	Path.ReplaceInline(TEXT("/./"), TEXT("/"), ESearchCase::CaseSensitive);
}

FString UWhisperSubsystem::GetPlatformPath(FString Path)
{
#if PLATFORM_ANDROID
	auto& PlatformFile = IAndroidPlatformFile::GetPlatformPhysical();
	NormalizePath(Path);

	while (Path.StartsWith(TEXT("../"), ESearchCase::CaseSensitive))
	{
		Path.RightChopInline(3, EAllowShrinking::No);
	}
	Path.ReplaceInline(FPlatformProcess::BaseDir(), TEXT(""));
	if (Path.Equals(TEXT(".."), ESearchCase::CaseSensitive))
	{
		Path = TEXT("");
	}
	// Local filepaths are directly in the deployment directory.
	// FileBasePath = GFilePathBase/UnrealGame/FApp::GetProjectName()/
	FString BasePath = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(TEXT("../"));
	Path = BasePath / Path;

	NormalizePath(Path);
	return Path;
#else
	return FPaths::ConvertRelativePathToFull(Path);
#endif
}

void UWhisperSubsystem::IterateDirectory(const FString& Dir, TArray<FString>& Data)
{
	if (!std::filesystem::exists(TCHAR_TO_ANSI(*Dir)))
	{
		Data.Add("--- ERROR ---");
		return;
	}
	auto dir = std::filesystem::directory_iterator(TCHAR_TO_ANSI(*Dir));
	for (const auto& dirEntry : dir)
	{
		if (!dirEntry.exists()) continue;
		
		FString s = ANSI_TO_TCHAR(dirEntry.path().string().c_str());
		if (dirEntry.is_directory())
		{
			s.Append(TEXT("/"));
		}
		Data.Add(s);
	}
}

void UWhisperSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WhisperContext = nullptr;
	WhisperParameters = nullptr;

	whisper_log_set([](enum ggml_log_level Level, const char* Text, void* UserData)
		{
			if (Level == GGML_LOG_LEVEL_ERROR)
			{
				UE_LOG(LogWhisper, Error, TEXT("(internal) %s"), *FString(Text));
			}
			else if (Level == GGML_LOG_LEVEL_WARN)
			{
				UE_LOG(LogWhisper, Warning, TEXT("(internal) %s"), *FString(Text));
			}
			else
			{
				UE_LOG(LogWhisper, Log, TEXT("(internal) %s"), *FString(Text));
			}
		}, nullptr
	);

	// try to auto-initialize
	auto Settings = GetDefault<UYnnkWhisperSettings>();
	if (Settings)
	{
		FString ModelPath = Settings->GetModelPath();
		if (FPaths::FileExists(ModelPath))
		{
			LoadModelFromFile(ModelPath);
		}
	}
}

void UWhisperSubsystem::Deinitialize()
{
	Super::Deinitialize();
	ReleaseWhisper();
}

void UWhisperSubsystem::ReleaseWhisper()
{
	bReady.AtomicSet(false);

	if (WhisperContext)
	{
		whisper_free(WhisperContext);
		WhisperContext = nullptr;
	}

	if (WhisperParameters && WhisperParameters->initial_prompt)
	{
		WhisperParameters->initial_prompt = nullptr;
	}

	if (WhisperParameters)
	{
		delete WhisperParameters;
		WhisperParameters = nullptr;
	}
}

void UWhisperSubsystem::OnModelReady()
{
	auto ModuleLS = FModuleManager::GetModulePtr<FYnnkVoiceLipsyncModule>(TEXT("YnnkVoiceLipsync"));
	if (ModuleLS)
	{
		UE_LOG(LogWhisper, Log, TEXT("Whisper initialization complete. Whisper binded to YnnkVoiceLipsync as external voice recognition system."));
		ModuleLS->SetExternalRecognizeAgent(this);
	}
}

void UWhisperSubsystem::InitializeParameters()
{
	WhisperParameters = new whisper_full_params(whisper_full_default_params(whisper_sampling_strategy::WHISPER_SAMPLING_GREEDY));
	if (!WhisperParameters)
	{
		UE_LOG(LogWhisper, Error, TEXT("Failed to create whisper parameters"));
		return;
	}

	WhisperParameters->initial_prompt = nullptr;

	// Disable all prints
	WhisperParameters->print_realtime = false;
	WhisperParameters->print_progress = false;
	WhisperParameters->print_timestamps = false;
	WhisperParameters->print_special = false;

	WhisperParameters->translate = false;
	WhisperParameters->no_context = false;
	WhisperParameters->single_segment = false;
	WhisperParameters->max_tokens = 0;
	WhisperParameters->audio_ctx = 0;
	WhisperParameters->temperature_inc = 0.4f;
	WhisperParameters->entropy_thold = 2.4f;
	WhisperParameters->token_timestamps = true;
	WhisperParameters->offset_ms = 0;
	WhisperParameters->language = "auto";

	WhisperParameters->n_threads = 1;
	WhisperParameters->suppress_blank = true;

	WhisperParameters->suppress_non_speech_tokens = true;
	WhisperParameters->suppress_digit_tokens = true;
	WhisperParameters->beam_search.beam_size = -1.f;

	// Setting up the new segment callback, which is called on every new recognized text segment
	WhisperParameters->new_segment_callback = WhisperCallback::NewTextSegmentCallback;
	WhisperParameters->new_segment_callback_user_data = this;

	// Setting up the abort mechanism callback, which is called every time before the encoder starts
	WhisperParameters->encoder_begin_callback = WhisperCallback::EncoderBeginCallback;
	WhisperParameters->encoder_begin_callback_user_data = this;

	// Setting up the abort mechanism callback, which is called every time before ggml computation starts
	WhisperParameters->abort_callback = WhisperCallback::EncoderAbortCallback;
	WhisperParameters->abort_callback_user_data = this;

	// Setting up the progress callback, which is called every time the progress changes
	WhisperParameters->progress_callback = WhisperCallback::ProgressCallback;
	WhisperParameters->progress_callback_user_data = this;
}

bool UWhisperSubsystem::BindWhisper()
{
	if (bReady)
	{
		OnModelReady();
		return true;
	}
	return false;
}

void UWhisperSubsystem::LoadModelFromFile(const FString& FileName, bool bAutoBind, bool bForceReinitialize)
{
	// Already initialized?
	if (!bForceReinitialize && IsInitialized())
	{
		UE_LOG(LogWhisper, Log, TEXT("Whisper context is already initialized. Skipping reinitialization."));
		return;
	}

	//GExternalFilePath = "";

	FString FileNameFull = FPaths::ConvertRelativePathToFull(FileName);
	
#if PLATFORM_ANDROID
	auto& PlatformFile = IAndroidPlatformFile::GetPlatformPhysical();
	// FileBasePath = GFilePathBase /	UnrealGame / FApp::GetProjectName() /

	FileNameFull.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
	FileNameFull.ReplaceInline(TEXT("//"), TEXT("/"), ESearchCase::CaseSensitive);
	FileNameFull.ReplaceInline(TEXT("/./"), TEXT("/"), ESearchCase::CaseSensitive);
	while (FileNameFull.StartsWith(TEXT("../"), ESearchCase::CaseSensitive))
	{
		FileNameFull.RightChopInline(3, EAllowShrinking::No);
	}
	FileNameFull.ReplaceInline(FPlatformProcess::BaseDir(), TEXT(""));
	if (FileNameFull.Equals(TEXT(".."), ESearchCase::CaseSensitive))
	{
		FileNameFull = TEXT("");
	}
	// Local filepaths are directly in the deployment directory.
	FString BasePath = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(TEXT("../"));
	FileNameFull = BasePath / FileNameFull;

	FileNameFull.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
	FileNameFull.ReplaceInline(TEXT("//"), TEXT("/"), ESearchCase::CaseSensitive);
	FileNameFull.ReplaceInline(TEXT("/./"), TEXT("/"), ESearchCase::CaseSensitive);

	/*
	FString FileBasePath = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(TEXT("../"));
	FString FileRootPath = PlatformFile.FileRootPath(*FileName);

	UE_LOG(LogWhisper, Log, TEXT("Android FileBasePath: %s"), *FileBasePath);
	UE_LOG(LogWhisper, Log, TEXT("Android FileRootPath: %s"), *FileRootPath);

	FileNameFull = FileRootPath / FApp::GetProjectName() / TEXT("Content/Whisper/ggml-tiny.bin");
	FileNameFull.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
	FileNameFull.ReplaceInline(TEXT("//"), TEXT("/"), ESearchCase::CaseSensitive);
	FileNameFull.ReplaceInline(TEXT("/./"), TEXT("/"), ESearchCase::CaseSensitive);
	*/
#endif

	ReleaseWhisper();
	InitializeParameters();

	AsyncTask(ENamedThreads::AnyThread, [this, FileNameFull, bAutoBind]() mutable
		{
			if (true || FPaths::FileExists(FileNameFull))
			{
				UE_LOG(LogWhisper, Log, TEXT("Whisper initialization from file: %s"), *FileNameFull);
				WhisperContext = whisper_init_from_file_with_params(TCHAR_TO_ANSI(*FileNameFull), whisper_context_default_params());
				if (WhisperContext)
				{
					bReady.AtomicSet(true);
					if (bAutoBind)
					{
						AsyncTask(ENamedThreads::GameThread, [this]()
							{
								OnModelReady();
							}
						);
					}
				}
			}
			else
			{
				UE_LOG(LogWhisper, Warning, TEXT("Whisper model file not found: %s"), *FileNameFull);
			}
		}
	);
}

void UWhisperSubsystem::LoadModelFromAsset(TSoftObjectPtr<UZipUFSArchive> Archive, bool bAutoBind, bool bForceReinitialize)
{
	// Already initialized?
	if (!bForceReinitialize && IsInitialized())
	{
		// free memory
		//UAssetManager::Get().UnloadPrimaryAsset(Archive.Get()->GetPrimaryAssetId());
		Archive.Reset();
		UE_LOG(LogWhisper, Log, TEXT("Whisper context is already initialized. Skipping reinitialization."));
		return;
	}

	Archive.LoadSynchronous();

	if (!IsValid(Archive.Get()) || Archive->Size < 100)
	{
		UE_LOG(LogWhisper, Warning, TEXT("Whisper model archive is invalid"));
		return;
	}

	ReleaseWhisper();
	InitializeParameters();

	AsyncTask(ENamedThreads::AnyThread, [this, Archive, bAutoBind]() mutable
		{
			UE_LOG(LogWhisper, Log, TEXT("Whisper initialization from archive: %s"), *Archive->GetName());

			void* DataPtr = nullptr;
			Archive->Buffer.GetCopy(&DataPtr);

			WhisperContext = whisper_init_from_buffer_with_params(DataPtr, Archive->Buffer.GetBulkDataSize(), whisper_context_default_params());
			if (WhisperContext)
			{
				bReady.AtomicSet(true);

				// free memory
				//UAssetManager::Get().UnloadPrimaryAsset(Archive.Get()->GetPrimaryAssetId());
				Archive.Reset();

				AsyncTask(ENamedThreads::GameThread, [this, Archive, bAutoBind]()
				{
					// free memory from the asset
					//UAssetManager::Get().UnloadPrimaryAsset(Archive->GetPrimaryAssetId());

					if (bAutoBind)
					{
						OnModelReady();
					}
				}
				);
			}
		}
	);
}

void UWhisperSubsystem::RecognizeAudio(const TArray<float>& AudioDataF32)
{
	if (!bReady || !WhisperContext || !WhisperParameters)
	{
		UE_LOG(LogWhisper, Log, TEXT("WhisperContext should be initialized first"));
	}

	TempRequest.AudioBuffer = AudioDataF32;
	RecognizedString = "";

	AsyncTask(ENamedThreads::AnyThread, [this]() mutable
		{
			if (whisper_full_parallel(WhisperContext, *WhisperParameters, TempRequest.AudioBuffer.GetData(), TempRequest.AudioBuffer.Num(), 1) != 0)
			{
				UE_LOG(LogWhisper, Log, TEXT("%d: failed to process audio"), TempRequest.AudioBuffer.Num());
			}
		}
	);
}

bool UWhisperSubsystem::IsInitialized() const
{
	return (bReady && WhisperContext && WhisperParameters);
}

FString UWhisperSubsystem::AsTimestamp(int64_t t)
{
	int64_t msec = t * 10;
	int64_t hr = msec / (1000 * 60 * 60);
	msec = msec - hr * (1000 * 60 * 60);
	int64_t min = msec / (1000 * 60);
	msec = msec - min * (1000 * 60);
	int64_t sec = msec / 1000;
	msec = msec - sec * 1000;

	FString out;
	out = out.Printf(TEXT("%02d:%02d:%02d.%03d"), (int)hr, (int)min, (int)sec, (int)msec);
	return out;
}

float UWhisperSubsystem::AsSeconds(int64_t t)
{
	int64_t msec = t * 10;
	int64_t hr = msec / (1000 * 60 * 60);
	msec = msec - hr * (1000 * 60 * 60);
	int64_t min = msec / (1000 * 60);
	msec = msec - min * (1000 * 60);
	int64_t sec = msec / 1000;
	msec = msec - sec * 1000;

	return (float)sec + (float)(min * 60) + (float)(hr * 60 * 60) + (float)msec * 0.001f;
}

void UWhisperSubsystem::Recognize_16_Implementation(UAsyncRecognizer* Sender, const TArray<uint8>& PCMData, int32 SampleRate, int32 Id, uint8 Flag)
{
	int32 SamplesNum = PCMData.Num() / 2;

	TempRequest.Sender = Sender;
	TempRequest.Flag = Flag;
	TempRequest.Id = Id;
	TempRequest.AudioBuffer.SetNumUninitialized(SamplesNum);
	
	AsyncTask(ENamedThreads::AnyThread, [this, PCMData, SamplesNum, SampleRate]() mutable
		{
			int16* pcm16 = (int16*)&PCMData[0];
			for (int32 i = 0; i < SamplesNum; i++)
			{
				TempRequest.AudioBuffer[i] = float(pcm16[i]) / 32768.0f;
			}

			AsyncTask(ENamedThreads::GameThread, [this, SampleRate]()
				{
					ResampleTempBuffer(SampleRate);
				}
			);
		}
	);
}

void UWhisperSubsystem::Recognize_32_Implementation(UAsyncRecognizer* Sender, const TArray<float>& PCMData, int32 SampleRate, int32 Id, uint8 Flag)
{
	TempRequest.Sender = Sender;
	TempRequest.Flag = Flag;
	TempRequest.Id = Id;
	TempRequest.AudioBuffer = PCMData;

	ResampleTempBuffer(SampleRate);
}

void UWhisperSubsystem::ResampleTempBuffer(int32 DataSampleRate)
{
	if (DataSampleRate != WHISPER_SAMPLE_RATE)
	{
		AsyncTask(ENamedThreads::AnyThread, [this, OriginalSampleRate = DataSampleRate]() mutable
			{
				Audio::FAlignedFloatBuffer& PCMData = TempRequest.AudioBuffer;
				Audio::FAlignedFloatBuffer ResampledPCMData;

				const Audio::FResamplingParameters ResampleParameters =
				{
					Audio::EResamplingMethod::Linear,
					1,
					static_cast<float>(OriginalSampleRate),
					static_cast<float>(WHISPER_SAMPLE_RATE),
					PCMData
				};

				ResampledPCMData.AddUninitialized(Audio::GetOutputBufferSize(ResampleParameters));
				Audio::FResamplerResults ResampleResults;
				ResampleResults.OutBuffer = &ResampledPCMData;

				if (Audio::Resample(ResampleParameters, ResampleResults))
				{
					PCMData = MoveTemp(ResampledPCMData);
					AsyncTask(ENamedThreads::GameThread, [this]()
						{
							ResampleTempBuffer(WHISPER_SAMPLE_RATE);
						}
					);
				}
				else
				{
					UE_LOG(LogWhisper, Error, TEXT("Failed to resample audio data from %d to %d"), OriginalSampleRate, WHISPER_SAMPLE_RATE);
				}
			}
		);
	}
	else
	{
		bool bStartRecognition = RequestsQueue.IsEmpty();
		RequestsQueue.Enqueue(TempRequest);

		if (bStartRecognition)
		{
			RecognizeFromQueue();
		}
	}
}

void UWhisperSubsystem::RecognizeFromQueue()
{
	if (!RequestsQueue.IsEmpty())
	{
		RecognizedString = TEXT("");
		RecognizedData.Empty();
		bBreakWork.AtomicSet(false);

		AsyncTask(ENamedThreads::AnyThread, [this]() mutable
			{
				RequestsQueue.Dequeue(ActiveRequest);
				if (whisper_full_parallel(WhisperContext, *WhisperParameters, ActiveRequest.AudioBuffer.GetData(), ActiveRequest.AudioBuffer.Num(), 1) != 0)
				{
					UE_LOG(LogWhisper, Log, TEXT("%d: failed to process audio"), ActiveRequest.AudioBuffer.Num());
				}
			}
		);
	}
}

void UWhisperSubsystem::SetLanguage_Implementation(const FString& InLanguage)
{
	Language = InLanguage;
	if (WhisperParameters)
	{
		UE_LOG(LogWhisper, Log, TEXT("Whisper set new language: %s"), *InLanguage);

		//const char* lang = StringCast<ANSICHAR>(*Language.ToLower()).Get();

		/**/ if (Language == TEXT("EN"))
			WhisperParameters->language = "en";
		else if (Language == TEXT("RU"))
			WhisperParameters->language = "ru";
		else if (Language == TEXT("CN"))
			WhisperParameters->language = "zh";
		else if (Language == TEXT("IT"))
			WhisperParameters->language = "it";
		else if (Language == TEXT("DE"))
			WhisperParameters->language = "de";
		else if (Language == TEXT("FR"))
			WhisperParameters->language = "fr";
		else if (Language == TEXT("ES"))
			WhisperParameters->language = "es";
		else if (Language == TEXT("BR - PT") || Language == TEXT("PT"))
			WhisperParameters->language = "pt";
		else if (Language == TEXT("PL"))
			WhisperParameters->language = "pl";
		else if (Language == TEXT("TR"))
			WhisperParameters->language = "tr";
	}
}

void UWhisperSubsystem::StopRecognition_Implementation(UAsyncRecognizer* Sender)
{
	RequestsQueue.Empty();
	bBreakWork.AtomicSet(true);
}

void UWhisperSubsystem::AddRecognizedWord(FString Word, float Time1, float Time2)
{
	static const TSet<FString> l_non_speech_tokens = {
		TEXT("\""), TEXT("#"), TEXT("("), TEXT(")"), TEXT("*"), TEXT("+"), TEXT("/"), TEXT(":"), TEXT(";"), TEXT("<"), TEXT("="), TEXT(">"), TEXT("@"),
		TEXT("["), TEXT("\\"), TEXT("]"), TEXT("^"), TEXT("_"), TEXT("`"), TEXT("{"), TEXT("|"), TEXT("}"), TEXT("~"), TEXT("「"), TEXT("」"), TEXT("『"),
		TEXT("』"), TEXT("-"), TEXT("["), TEXT("(\""), TEXT("♪"), TEXT("♩"), TEXT("♪"), TEXT("♫"),
		TEXT("♬"), TEXT("♭"), TEXT("♮"), TEXT("♯"), TEXT("."), TEXT(","), TEXT("!"), TEXT("?")
	};
	static const TSet<FString> l_service_tokens = {
		TEXT("[_TT_"), TEXT("[_EOT_]"), TEXT("[_SOT_]"), TEXT("[_TRANSLATE_]"), TEXT("[_TRANSCRIBE_]"), TEXT("[_SOLM_]"), TEXT("[_PREV_]"),
		TEXT("[_NOSP_]"), TEXT("[_NOT_]"), TEXT("[_BEG_]"), TEXT("[_LANG_"), TEXT("[_extra_token_")
	};

	// ignore service tokens
	for (const auto& token : l_service_tokens)
	{
		int32 t_len = token.Len();
		if (Word.Len() >= t_len && Word.Left(t_len) == token)
		{
			return;
		}
	}
	// remove non-speech symbols
	Word.ReplaceInline(TEXT("\t"), TEXT(" "));
	for (const auto& token : l_non_speech_tokens)
	{
		Word.ReplaceInline(*token, TEXT(""));
	}

	Word.ToLowerInline();
	Word.TrimStartAndEndInline();
	if (Word.IsEmpty())
	{
		return;
	}

	if (!RecognizedData.IsEmpty())
	{
		auto& Last = RecognizedData.Last();
		if (FMath::IsNearlyEqual(Last.TimeStart, Time1, 0.05f) && FMath::IsNearlyEqual(Last.TimeEnd, Time2, 0.05f))
		{
			Last = FSingeWordData(Word, Time1, Time2);
			return;
		}
	}

	RecognizedData.Add(FSingeWordData(Word, Time1, Time2));
}

void WhisperCallback::NewTextSegmentCallback(whisper_context* WhisperContext, whisper_state* WhisperState, int NewSegmentCount, void* UserData)
{
	if (!UserData) return;
	UWhisperSubsystem* WhisperSubsystem = (UWhisperSubsystem*)UserData;
	if (!WhisperSubsystem->WhisperContext || !WhisperSubsystem->WhisperParameters)
	{
		return;
	}

	const int32 TotalSegmentCount = whisper_full_n_segments(WhisperContext);
	const int32 StartIndex = TotalSegmentCount - NewSegmentCount;

	FString NewData;
	for (int32 Index = StartIndex; Index < TotalSegmentCount; ++Index)
	{
		const char* TextPerSegment = whisper_full_get_segment_text(WhisperContext, static_cast<int>(Index));
		FString TextPerSegment_String = UTF8_TO_TCHAR(TextPerSegment);
		NewData.Append(TextPerSegment_String);

		// segment start and end
		const int64_t t0 = whisper_full_get_segment_t0(WhisperSubsystem->WhisperContext, Index);
		const int64_t t1 = whisper_full_get_segment_t1(WhisperSubsystem->WhisperContext, Index);

		// token is a word
		const int NumTokensInSegment = whisper_full_n_tokens(WhisperSubsystem->WhisperContext, Index);

		for (int32 TokenIndex = 0; TokenIndex < NumTokensInSegment; TokenIndex++)
		{
			auto token = whisper_full_get_token_data(WhisperSubsystem->WhisperContext, Index, TokenIndex);

			const char* szTokenText = whisper_token_to_str(WhisperSubsystem->WhisperContext, token.id);
			FString TokenText = UTF8_TO_TCHAR(szTokenText);
			TokenText.TrimStartAndEndInline();

			float TimeStart = UWhisperSubsystem::AsSeconds(token.t0);
			float TimeEnd = UWhisperSubsystem::AsSeconds(token.t1);

			WhisperSubsystem->AddRecognizedWord(TokenText, TimeStart, TimeEnd);
		}
	}

	AsyncTask(ENamedThreads::GameThread, [WhisperSubsystem, NewData = MoveTemp(NewData)]() mutable
		{
			if (IsValid(WhisperSubsystem))
			{
				UE_LOG(LogWhisper, Log, TEXT("Recognized text segment: \"%s\""), *NewData);
				WhisperSubsystem->RecognizedString.Append(NewData);
			}
		}
	);
}

bool WhisperCallback::EncoderBeginCallback(whisper_context* WhisperContext, whisper_state* WhisperState, void* UserData)
{
	if (!UserData) return false;
	UWhisperSubsystem* WhisperSubsystem = (UWhisperSubsystem*)UserData;
	if (!WhisperSubsystem->WhisperContext || !WhisperSubsystem->WhisperParameters)
	{
		return false;
	}

	return true;
}

bool WhisperCallback::EncoderAbortCallback(void* UserData)
{
	if (!UserData) return true;
	UWhisperSubsystem* WhisperSubsystem = (UWhisperSubsystem*)UserData;
	if (!WhisperSubsystem->WhisperContext || !WhisperSubsystem->WhisperParameters)
	{
		return true;
	}
	
	return WhisperSubsystem->ShouldBreak();
}

void WhisperCallback::ProgressCallback(whisper_context* WhisperContext, whisper_state* WhisperState, int Progress, void* UserData)
{
	if (!UserData) return;
	UWhisperSubsystem* WhisperSubsystem = (UWhisperSubsystem*)UserData;
	if (!WhisperSubsystem->WhisperContext || !WhisperSubsystem->WhisperParameters)
	{
		return;
	}

	Progress = FMath::Clamp(Progress, 0, 100);

	UE_LOG(LogWhisper, Log, TEXT("Speech recognition progress: %d"), Progress);

	if (Progress == 100)
	{
		AsyncTask(ENamedThreads::GameThread, [WhisperSubsystem, Progress]() mutable
			{
				if (IsValid(WhisperSubsystem->ActiveRequest.Sender))
				{
					WhisperSubsystem->RecognizedString.ReplaceInline(TEXT("  "), TEXT(" "));
					WhisperSubsystem->RecognizedString.TrimStartAndEndInline();

					WhisperSubsystem->ActiveRequest.Sender->OnExternalRecognizeResult(
						WhisperSubsystem->ActiveRequest.Id,
						WhisperSubsystem->ActiveRequest.Flag,
						WhisperSubsystem->RecognizedString,
						WhisperSubsystem->RecognizedData
					);
				}
				WhisperSubsystem->RecognizeFromQueue();
			}
		);
	}
}

