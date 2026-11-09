// Copyright (c) NodeFactory. Editor module rules (spec 2.1).

using UnrealBuildTool;

public class NodeFactoryEditor : ModuleRules
{
	public NodeFactoryEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Modules whose types appear in the public headers of this module.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"NodeFactory",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"UnrealEd",
			"GraphEditor",
			"BlueprintGraph",
			"KismetCompiler",
			"Kismet"
		});

		// Modules used only by the implementation (spec 2.1 lists the required ones, the rest are needed by
		// the pin type selector, file dialogs, source control checkout, clipboard, plugin paths and the Window menu).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",
			"AssetTools",
			"AssetRegistry",
			"PropertyEditor",
			"ContentBrowser",
			"MessageLog",
			"Json",
			"JsonUtilities",
			"Projects",
			"SourceControl",
			"KismetWidgets",
			"EditorWidgets",
			"DesktopPlatform",
			"ApplicationCore",
			"WorkspaceMenuStructure"
		});

		// ISettingsModule is only reached through the module manager.
		PrivateIncludePathModuleNames.AddRange(new string[]
		{
			"Settings"
		});
	}
}
