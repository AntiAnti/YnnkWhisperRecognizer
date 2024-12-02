// (c) Yuri N. K. 2024. All rights reserved.
// ykasczc@gmail.com

using UnrealBuildTool;
using System.IO;

public class YnnkWhisperRecognizer : ModuleRules
{
	public YnnkWhisperRecognizer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
/*
#if UE_5_3_OR_LATER
        MinCpuArchX64 = MinimumCpuArchitectureX64.AVX;
#endif
*/
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty", "whisper.cpp"));
				
		PublicIncludePaths.AddRange(
			new string[] {
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"Projects",
				"YnnkVoiceLipsync",
                "SignalProcessing",
                "AudioPlatformConfiguration"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);

		// iOS isn't in PlatformAllowList, but whisper should work on it
        if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.IOS)
        {
            string ModulePath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
            AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(ModulePath, "YnnkWhisperRecognizer_UPL.xml"));
        }
    }
}
