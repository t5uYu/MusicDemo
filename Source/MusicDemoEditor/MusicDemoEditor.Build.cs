using UnrealBuildTool;

public class MusicDemoEditor : ModuleRules
{
	public MusicDemoEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"ToolMenus",
			"DesktopPlatform",
			"Projects",
			"AssetTools",
			"Json",
			"DeveloperSettings"
		});
	}
}
