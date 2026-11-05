// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ARPGDemo : ModuleRules
{
	public ARPGDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore", 
			"EnhancedInput",
            "GameplayAbilities",
			"GameplayTags",
			"GameplayTasks" ,
			"AIModule",
			"UMG",
			"Slate",
			"SlateCore",
            "Niagara",
			"HTTP",// FHttpModule / IHttpRequest
			"Json",// FJsonSerializer
            "JsonUtilities"// FJsonObjectConverter::JsonObjectToUStruct
		});

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
