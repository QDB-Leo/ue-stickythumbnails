using UnrealBuildTool;

public class StickyThumbnails : ModuleRules
{
	public StickyThumbnails(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"EditorSubsystem",
			"ToolMenus",
			"ContentBrowser",
			"AssetTools",
			"AssetRegistry",
			"LevelSequence",
		});
	}
}
