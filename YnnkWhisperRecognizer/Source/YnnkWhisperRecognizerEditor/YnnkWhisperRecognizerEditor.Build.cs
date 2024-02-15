// (c) Yuri N. K. 2024. All rights reserved.
// ykasczc@gmail.com

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
    public class YnnkWhisperRecognizerEditor : ModuleRules
    {
        public YnnkWhisperRecognizerEditor(ReadOnlyTargetRules Target) : base(Target)
        {
            PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
            PrivatePCHHeaderFile = "Public/YnnkWhisperRecognizerEditor.h";

            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "Core",
                    "SlateCore",
                    "Slate",
                    "CoreUObject",
                    "Settings",
                    "UnrealEd",
                    "Projects",
					"YnnkWhisperRecognizer"
                }
            );
        }
    }
}