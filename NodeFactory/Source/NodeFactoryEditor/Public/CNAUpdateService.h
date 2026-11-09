// Copyright (c) NodeFactory. Update of all node instances after a change of the asset (spec 9.1).

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/WeakObjectPtr.h"

class UBlueprint;
class UCustomNodeAsset;
class UK2Node_CustomNode;

/** What started an update (spec 9.1, item 1). */
enum class ECNAUpdateTrigger : uint8
{
	/** The asset was saved and its pin structure changed (AssetVersion was bumped). */
	AssetSaved,
	/** Command Recompile Node of the asset window or of the Content Browser: every instance is rebuilt. */
	RecompileCommand,
	/** A Blueprint with outdated nodes was opened in an editor. */
	BlueprintOpened
};

/** Result of one update, written to the Message Log "NodeFactory" (spec 9.1, item 6). */
struct NODEFACTORYEDITOR_API FCNAUpdateReport
{
	/** Blueprints in which at least one node was rebuilt. */
	int32 NumBlueprints = 0;

	/** Rebuilt nodes. */
	int32 NumNodes = 0;

	/** Lost wires, one entry per pin: "Blueprint: Node.Pin". Each loss is also reported as CNA016. */
	TArray<FString> LostWires;

	/** Blueprints that could not be loaded or checked out. */
	TArray<FString> FailedBlueprints;

	/** Blueprints that keep outdated nodes: not loaded after Skip, cancelled, or level Blueprints of closed maps. */
	TArray<FString> SkippedBlueprints;

	/** The user pressed Cancel in the confirmation window or in the progress window. */
	bool bCancelled = false;

	bool HasChanges() const { return NumNodes > 0; }
	bool HasProblems() const { return LostWires.Num() > 0 || FailedBlueprints.Num() > 0; }

	/** Adds the numbers and lists of another report. */
	void Append(const FCNAUpdateReport& Other);

	/** One line summary for the log and the notification. */
	FText ToText() const;
};

/** A Blueprint that references a Custom Node Asset, found through the asset registry without loading (spec 9.1, item 2). */
struct NODEFACTORYEDITOR_API FCNAReferencingBlueprint
{
	/** Object path of the Blueprint asset; for a map, the path of the world. */
	FSoftObjectPath ObjectPath;

	/** Package of the Blueprint or of the map. */
	FName PackageName;

	/** Asset name shown in the confirmation window. */
	FString DisplayName;

	/** The Blueprint when it is in memory (for a map: its level Blueprint while the map is open). */
	TWeakObjectPtr<UBlueprint> Loaded;

	/** The package is a map. Its level Blueprint is updated only while the map is open; maps are never loaded. */
	bool bIsMap = false;

	bool IsLoaded() const { return Loaded.IsValid(); }
};

/**
 * Keeps the instances of Custom Node Assets in step with their assets (spec 9.1):
 *  - a save of the asset compares the signature hash, bumps AssetVersion and offers the update afterwards;
 *  - the command Recompile Node rebuilds every instance;
 *  - opening a Blueprint with outdated nodes offers to rebuild them (their rebuild is deferred while the
 *    Blueprint loads, see UK2Node_CustomNode::ReconstructNode);
 *  - a change of the appearance of an asset redraws the instances in loaded Blueprints, without a rebuild.
 *
 * Blueprints are found through the asset registry and loaded only after the user confirmed (Update All). The
 * update runs in one transaction, marks every Blueprint as structurally modified and never saves anything.
 */
struct NODEFACTORYEDITOR_API FCNAUpdateService
{
	/** Subscribes to saves of assets, changes of assets and opened asset editors. */
	static void Startup();

	/** Removes every subscription and pending timer, in reverse order. */
	static void Shutdown();

	static bool IsStarted();

	/** An update is running (recursion guard, spec 9.6). */
	static bool IsUpdating();

	// Version

	/**
	 * Compares the signature hash of the asset with SignatureHash. On a difference the hash is stored and
	 * AssetVersion is bumped; an asset without a stored hash only records it. Returns true when bumped.
	 */
	static bool RefreshAssetVersion(UCustomNodeAsset* Asset);

	// Search

	/** Blueprints that reference one of the assets: referencers from the registry plus loaded Blueprints with instances. */
	static void FindReferencingBlueprints(const TArray<UCustomNodeAsset*>& Assets, TArray<FCNAReferencingBlueprint>& OutBlueprints);

	/** Instances of the assets in a loaded Blueprint, in all its graphs. An empty asset list accepts every asset. */
	static void FindInstances(UBlueprint* Blueprint, const TArray<UCustomNodeAsset*>& Assets, bool bOnlyOutdated, TArray<UK2Node_CustomNode*>& OutNodes);

	/** Instances of the asset in all loaded Blueprints. */
	static int32 CountLoadedInstances(const UCustomNodeAsset* Asset, bool bOnlyOutdated);

	// Update

	/** Finds the Blueprints, asks with the confirmation window when it is enabled, updates and reports. */
	static FCNAUpdateReport RequestUpdate(UCustomNodeAsset* Asset, ECNAUpdateTrigger Trigger);
	static FCNAUpdateReport RequestUpdate(const TArray<UCustomNodeAsset*>& Assets, ECNAUpdateTrigger Trigger);

	/**
	 * Updates the listed Blueprints without asking (spec 9.1, item 4).
	 * @param bLoadFromDisk  false: Blueprints that are not in memory are skipped (button Skip).
	 * @param bCheckOut      check the files out first when source control is connected.
	 * @param bOnlyOutdated  rebuild only nodes whose NodeVersion differs from AssetVersion.
	 */
	static FCNAUpdateReport UpdateBlueprints(const TArray<UCustomNodeAsset*>& Assets, const TArray<FCNAReferencingBlueprint>& Blueprints,
		bool bLoadFromDisk, bool bCheckOut, bool bOnlyOutdated);

	/** Rebuilds the instances in one loaded Blueprint. The caller owns the transaction. Returns the number of nodes. */
	static int32 UpdateBlueprint(UBlueprint* Blueprint, const TArray<UCustomNodeAsset*>& Assets, bool bOnlyOutdated, FCNAUpdateReport& InOutReport);

	/** Command Recompile Node (spec 7.2): refreshes caches and version, compiles the asset and rebuilds every instance. */
	static FCNAUpdateReport RecompileNode(UCustomNodeAsset* Asset);

	/** Trigger 3 of spec 9.1: offers to rebuild the outdated nodes of a Blueprint that was opened. */
	static void HandleBlueprintOpened(UBlueprint* Blueprint);

	/** Writes the summary to the Message Log "NodeFactory" and shows a notification. */
	static void ReportToLog(const TArray<UCustomNodeAsset*>& Assets, const FCNAUpdateReport& Report, ECNAUpdateTrigger Trigger);

	/** Redraws the instances of the asset in loaded Blueprints after a change of its appearance (debounced). */
	static void RequestWidgetRefresh(const UCustomNodeAsset* Asset);

	/** Short text naming the assets: "CNA_A, CNA_B and 3 more". */
	static FText DescribeAssets(const TArray<UCustomNodeAsset*>& Assets);
};
