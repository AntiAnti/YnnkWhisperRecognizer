// (c) Yuri N. K. 2024. All rights reserved.
// ykasczc@gmail.com

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Containers/Queue.h"
#include "DSP/AlignedBuffer.h"
#include "HAL/ThreadSafeBool.h"
#include <atomic>
#include "ExternalRecognizerInterface.h"
#include "YnnkTypes.h"
#include "WhisperSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogWhisper, Log, All);

class UAsyncRecognizer;

/**
* Struct to store recognition requests in the queue.
* In fact, I don't expect the queue is needed, because requests are processed one by one
* in YnnkVoiceLipsync plugin. But better safe then sorry.
*/
USTRUCT(BlueprintType)
struct YNNKWHISPERRECOGNIZER_API FWhisperRequest
{
	GENERATED_BODY()

	/** Request sender, i. e. pointer to UAsyncRecognizer object from YnnkVoiceLipsync */
	TObjectPtr<UAsyncRecognizer> Sender = nullptr;

	/** Request Id, just need to return it */
	int32 Id = INDEX_NONE;

	/** Request flag, just need to return it */
	uint8 Flag = 0;

	/** Audio data (16,000 Hz, mono, 32bit) */
	Audio::FAlignedFloatBuffer AudioBuffer;
};

/**
 * Engine subsystem-wrapper for whisper.cpp voice recognition library 
 */
UCLASS()
class YNNKWHISPERRECOGNIZER_API UWhisperSubsystem :
	public UEngineSubsystem,
	public IExternalRecognizerInterface
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** init whisper_full_params */
	void InitializeParameters();

	/** Free memory */
	void ReleaseWhisper();

	/** The Whisper context used for speech recognition */
	struct whisper_context* WhisperContext;
	/** The parameters used for configuring the Whisper speech recognizer */
	struct whisper_full_params* WhisperParameters;

	/** Whisper language */
	UPROPERTY()
	FString Language = TEXT("en");

	/** Output result: clean subtitles */
	UPROPERTY()
	FString RecognizedString;

	/** Output result: recognized words with time marks */
	UPROPERTY()
	TArray<FSingeWordData> RecognizedData;

	// Debug function; should delete
	UFUNCTION(BlueprintPure, Category = "Whisper")
	void IterateDirectory(const FString& Dir, TArray<FString>& Data);

	static void NormalizePath(FString& Path);
	static FString GetPlatformPath(FString Path);

	/**
	* Bind whisper to YnnkVoiceLip-sync as primary voice recognition system
	*/
	UFUNCTION(BlueprintCallable, Category = "Whisper")
	bool BindWhisper();

	/**
	* Load binary ggml whisper model from file on disk
	* @param FileName full name of .bin file
	* @param bAutoBind automatically bind Whisper module to YnnkVoiceLipsync as primary voice recognition toolkit (no need to call BindWhisper)
	* @param bForceReinitialize load model if whisper was already initialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void LoadModelFromFile(const FString& FileName, bool bAutoBind = true, bool bForceReinitialize = false);

	/**
	* Load binary ggml whisper model from primary data asset. If it succeed, the function removes this asset from the memory.
	* @param Archive BinaryArchive asset with binary ggml model
	* @param bAutoBind automatically bind Whisper module to YnnkVoiceLipsync as primary voice recognition toolkit (no need to call BindWhisper)
	* @param bForceReinitialize load model if whisper was already initialized
	*/
	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void LoadModelFromAsset(TSoftObjectPtr<class UZipUFSArchive> Archive, bool bAutoBind = true, bool bForceReinitialize = false);

	/** Debug function processing direct requests, currently shouldn't be used because result output isn't implemented */
	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void RecognizeAudio(const TArray<float>& AudioDataF32);

	/** Is WhisperSubsytem ready to use? */
	UFUNCTION(BlueprintPure, Category = "Whisper")
	bool IsInitialized() const;

	/** Internal function to recognize next audio from the RequestsQueue */
	void RecognizeFromQueue();

	/** Internal function to interrupt current recognition request */
	bool ShouldBreak() { return bBreakWork; }

	/** Add new token to RecognizedData array */
	void AddRecognizedWord(FString Word, float Time1, float Time2);

	/** Convert time mark to string timestamp hh:mm:ss:msec */
	static FString AsTimestamp(int64_t t);
	/** Convert time mark to float (seconds) */
	static float AsSeconds(int64_t t);

	/* ~Begin IExternalRecognizerInterface interface */
	virtual void Recognize_16_Implementation(UAsyncRecognizer* Sender, const TArray<uint8>& PCMData, int32 SampleRate, int32 Id, uint8 Flag) override;
	virtual void Recognize_32_Implementation(UAsyncRecognizer* Sender, const TArray<float>& PCMData, int32 SampleRate, int32 Id, uint8 Flag) override;
	virtual void SetLanguage_Implementation(const FString& InLanguage) override;
	virtual void StopRecognition_Implementation(UAsyncRecognizer* Sender) override;
	virtual FName GetToolName_Implementation() override
	{
		return TEXT("Whisper.cpp");
	}
	/* ~End IExternalRecognizerInterface interface */

	/** Queue containing future requests for voice recognition */
	TQueue<FWhisperRequest> RequestsQueue;
	/** Currently executed recognition request */
	FWhisperRequest ActiveRequest;

protected:
	/** Temp variable to resample input data (supports any sample rate and bit rate = 16 on input) */
	FWhisperRequest TempRequest;

	/** Resample (if needed) TempRequest and start recognition */
	void ResampleTempBuffer(int32 DataSampleRate);
	/** Called internally after loading model to bind to YnnkVoiceLipsync */
	void OnModelReady();

	/** Set by StopRecognition_Implementation to interupt current requests */
	FThreadSafeBool bBreakWork = false;
	/** Set when the whisper is ready to use */
	FThreadSafeBool bReady = false;
};

