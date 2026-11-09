// Copyright (c) NodeFactory. Runtime module rules (spec 2.1).

using UnrealBuildTool;

public class NodeFactory : ModuleRules
{
	public NodeFactory(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// The runtime module only declares the asset class and its data types: no editor code (spec 2.1, 2.2).
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});
	}
}
