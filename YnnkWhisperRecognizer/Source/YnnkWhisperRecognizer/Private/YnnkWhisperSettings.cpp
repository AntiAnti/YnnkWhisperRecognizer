// (c) Yuri N. K. 2021. All rights reserved.
// ykasczc@gmail.com

#include "YnnkWhisperSettings.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

UYnnkWhisperSettings::UYnnkWhisperSettings()
{
}

FString UYnnkWhisperSettings::GetModelPath() const
{
	FString Result = DefaultModelFilePath;
	
	if (!Result.IsEmpty())
	{
		MakeFullPath(Result);
	}
	
	return Result;
}

void UYnnkWhisperSettings::MakeFullPath(FString& InOutPath) const
{
	FString ContentDir;
	
#if WITH_EDITOR
	auto ThisPlugin = IPluginManager::Get().FindPlugin(TEXT("YnnkWhisperRecognizer"));
	if (ThisPlugin.IsValid())
	{
		ContentDir = FPaths::ConvertRelativePathToFull(ThisPlugin->GetBaseDir()) / TEXT("Content");
	}

	if (!FPaths::FileExists(ContentDir / InOutPath) && !FPaths::DirectoryExists(ContentDir / InOutPath))
	{
		ContentDir = FPaths::ProjectContentDir();
	}

#else
	ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
#endif

	InOutPath = ContentDir / InOutPath;
}
