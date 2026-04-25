// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MusicDemo : ModuleRules
{
	public MusicDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Json", "JsonUtilities", "Slate", "SlateCore", "HTTP", "PCG" });
	}
}
