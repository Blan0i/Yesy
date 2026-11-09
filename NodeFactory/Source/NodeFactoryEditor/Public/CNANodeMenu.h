// Copyright (c) NodeFactory. Entries of the Blueprint action menu for custom nodes and local variables (spec 9.8, V9).

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class FBlueprintActionDatabaseRegistrar;
class UClass;
class UEdGraphNode;
class UCustomNodeAsset;
struct FAssetData;
struct FCNALocalVariable;

/**
 * Menu of custom nodes. All entries are recorded under the node classes (UK2Node_CustomNode, UK2Node_GetLocal,
 * UK2Node_SetLocal), so one RefreshClassActions per class rebuilds them; the refresh is debounced and runs on
 * the next ticks after the asset registry or an asset changed (V9).
 */
class NODEFACTORYEDITOR_API FCNANodeMenu
{
public:
	/**
	 * One spawner per Custom Node Asset of the asset registry, built from the registry tags without loading
	 * the asset (spec 9.8). Loaded assets give their current values instead of the saved tags.
	 * Assets with bHiddenInMenu get no spawner (spec 3.1).
	 */
	static void RegisterMenuActions(FBlueprintActionDatabaseRegistrar& Registrar, UClass* NodeClass);

	/** Get Local or Set Local: one entry per local variable of every loaded asset, plus one unbound entry. */
	static void RegisterLocalVariableActions(FBlueprintActionDatabaseRegistrar& Registrar, UClass* NodeClass);

	/** Rebuilds the entries soon (debounced). Safe to call before Startup and after Shutdown (no effect). */
	static void RequestRefresh();

	/** Rebuilds the entries now. Used by the command Refresh Nodes of the asset menu (V9 fallback). */
	static void RefreshNow();

	/** Subscribes to the asset registry and to FCNAEditorHooks (spec 9.5). */
	static void Startup();

	/** Removes every subscription and a pending refresh; reverse order of Startup. */
	static void Shutdown();

	/** Number of spawners the last RegisterMenuActions created (tests and the catalog status line). */
	static int32 GetLastRegisteredCount();

private:
	static void CustomizeCustomNode(UEdGraphNode* NewNode, bool bIsTemplateNode, FSoftObjectPath AssetPath);
	static void CustomizeLocalVariableNode(UEdGraphNode* NewNode, bool bIsTemplateNode, FGuid VariableGuid, FName VariableName);
	static bool IsMenuAsset(const FAssetData& AssetData);
};
