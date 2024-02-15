// (c) Yuri N. K. 2021. All rights reserved.
// ykasczc@gmail.com

#pragma once

#include "CoreMinimal.h"
#include "YnnkWhisperSettings.generated.h"

/**
* Settings object for YnnkWhisperRecognizer plugin
*/
UCLASS(config = Engine, defaultconfig)
class YNNKWHISPERRECOGNIZER_API UYnnkWhisperSettings : public UObject
{
	GENERATED_BODY()

public:
	UYnnkWhisperSettings();

	// Get finalized path to the model file, generated from DefaultModelFilePath
	FString GetModelPath() const;

	/**
	* Path to whisper voice recognition model, relative to Content folder
	* You can download trained models here: https://huggingface.co/ggerganov/whisper.cpp/tree/main
	* Note that larger models are slower.
	*/
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "General")
	FString DefaultModelFilePath = TEXT("Whisper/ggml-tiny.bin");
	
private:
	void MakeFullPath(FString& InOutPath) const;
};