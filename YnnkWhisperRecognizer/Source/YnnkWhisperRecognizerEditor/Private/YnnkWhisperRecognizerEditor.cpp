// (c) Yuri N. K. 2021. All rights reserved.
// ykasczc@gmail.com

#include "YnnkWhisperRecognizerEditor.h"
#include "ISettingsModule.h"
#include "YnnkWhisperSettings.h"

#define LOCTEXT_NAMESPACE "FYnnkWhisperRecognizerEditor"

void FYnnkWhisperRecognizerEditor::StartupModule()
{
	// Register plugin settings
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	SettingsModule.RegisterSettings("Project", "Plugins", "Ynnk Lip-sync (Whisper)",
		NSLOCTEXT("YnnkLipsync", "YnnkLipsyncWhisper", "Ynnk Lip-sync (Whisper)"),
		NSLOCTEXT("YnnkLipsync", "YnnkLipsyncWhisperConfigs", "Configure path to whisper model"),
		GetMutableDefault<UYnnkWhisperSettings>());
}


void FYnnkWhisperRecognizerEditor::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FYnnkWhisperRecognizerEditor, YnnkWhisperRecognizerEditor)
