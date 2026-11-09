// Copyright (c) NodeFactory. Graph helpers: signature of the macro Execute, graph kinds, latent and pure
// analysis, nesting of custom nodes (spec 4.1, 5.2, 10.2).

#pragma once

#include "CoreMinimal.h"
#include "CNATypes.h"
#include "CNAPinUtils.h"
#include "EdGraph/EdGraph.h"

class UBlueprint;
class UCustomNodeAsset;
class UK2Node_Tunnel;
class UK2Node_CustomNode;

struct NODEFACTORYEDITOR_API FCNAGraphUtils
{
	// Execute macro and its signature (spec 4.1)

	/** Tunnel Input of a macro graph: the exact UK2Node_Tunnel that can only have outputs. */
	static UK2Node_Tunnel* FindEntryTunnel(const UEdGraph* Graph);

	/** Tunnel Output of a macro graph: the exact UK2Node_Tunnel that can only have inputs. */
	static UK2Node_Tunnel* FindExitTunnel(const UEdGraph* Graph);

	/** Macro Execute with both Tunnel nodes exists (otherwise CNA005). */
	static bool HasValidExecuteGraph(const UCustomNodeAsset* Asset);

	/** Reads the pins of the Tunnel nodes in order. Returns false when the macro or a Tunnel is missing. */
	static bool GetSignature(const UCustomNodeAsset* Asset, TArray<FCNASignaturePin>& OutPins);

	static const FCNASignaturePin* FindSignaturePin(const TArray<FCNASignaturePin>& Pins, FName Name, EEdGraphPinDirection Direction = EGPD_MAX);

	/** Hash of everything that defines the pin structure of the node (spec 2.4, 9.1). */
	static uint32 ComputeSignatureHash(const UCustomNodeAsset* Asset);

	// Graphs

	/** Root graph and all of its sub graphs (collapsed graphs), depth first. */
	static void CollectGraphs(UEdGraph* Root, TArray<UEdGraph*>& OutGraphs);

	/** Macro Execute and its collapsed sub graphs. */
	static void CollectExecuteGraphs(const UCustomNodeAsset* Asset, TArray<UEdGraph*>& OutGraphs);

	/** Outermost graph that is owned directly by a Blueprint or by nothing. */
	static UEdGraph* GetTopLevelGraph(const UEdGraph* Graph);

	/** Kind of the graph for AllowedGraphs (spec 3.1). */
	static ECNAGraphKind ClassifyGraph(const UEdGraph* Graph);

	/**
	 * Latent nodes are allowed in the graph (spec 5.2, CNA003). Event Graphs - and the consolidated
	 * "ExecuteUbergraph_" graph of the compiler - allow them, functions and the construction script do not.
	 * Macro graphs are allowed because the final context is only known when the macro is expanded.
	 */
	static bool IsLatentAllowedIn(const UEdGraph* Graph);

	/** Blueprint that owns the graph (also for the intermediate graphs of the compiler). */
	static UBlueprint* GetBlueprint(const UObject* GraphOrNode);

	/** Custom Node Asset that owns the object, if any. */
	static UCustomNodeAsset* GetOwningAsset(const UObject* GraphOrNode);

	/** The asset when the graph is the macro Execute of a CNA or one of its sub graphs, otherwise nullptr. */
	static UCustomNodeAsset* GetExecuteGraphOwner(const UEdGraph* Graph);

	/** The graph is the Editor Behavior Graph (an Event Graph page) of a CNA (spec 6.5). */
	static bool IsBehaviorGraph(const UEdGraph* Graph);

	/** Graph contains at least one custom node; used to decide whether our wire policy is needed. */
	static bool ContainsCustomNodes(const UEdGraph* Graph);

	/** The Blueprint provides the context ContextClass requires (CNA012). */
	static bool DoesBlueprintSatisfyContext(const UBlueprint* Blueprint, const UClass* ContextClass);

	// Analysis of the macro Execute (spec 5.2, 10.2)

	/** Latent nodes in Execute, including nested macros and custom nodes. Names are collected when requested. */
	static bool FindLatentNodes(const UCustomNodeAsset* Asset, TArray<FString>* OutNodeNames);

	/** Nodes with execution pins (Sequence, loops, impure calls) - forbidden in a pure node (CNA004). */
	static bool FindExecutionNodes(const UCustomNodeAsset* Asset, TArray<FString>* OutNodeNames);

	/** Custom nodes placed inside Execute. */
	static void GetNestedCustomNodes(const UCustomNodeAsset* Asset, TArray<UK2Node_CustomNode*>& OutNodes);

	/** Finds a cycle A -> B -> A of nested assets; OutChain receives the asset names (CNA001). */
	static bool FindCycle(const UCustomNodeAsset* Asset, TArray<FString>& OutChain);

	/** Deepest nesting below the asset, counting the asset itself as 1; stops counting past Limit (CNA002). */
	static int32 GetNestingDepth(const UCustomNodeAsset* Asset, int32 Limit);

	/** Execute contains a Get Caller node (spec 4.9). */
	static bool UsesGetCaller(const UCustomNodeAsset* Asset);

	/** "None" or "Array of T" for the CNA.DynamicPins tag (spec 7.2). */
	static FString DescribeDynamicPins(const UCustomNodeAsset* Asset);

	/** Updates bContainsLatent and CachedDynamicPinsDescription of the asset. Returns true if anything changed. */
	static bool RefreshCachedAssetData(UCustomNodeAsset* Asset);

	template<typename NodeType>
	static void GetExecuteNodesOfClass(const UCustomNodeAsset* Asset, TArray<NodeType*>& OutNodes)
	{
		TArray<UEdGraph*> Graphs;
		CollectExecuteGraphs(Asset, Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			Graph->GetNodesOfClass<NodeType>(OutNodes);
		}
	}

	// Text

	/** Short node name for messages. */
	static FString GetNodeName(const UEdGraphNode* Node);

	/** "A, B, C and 2 more". */
	static FString JoinNames(const TArray<FString>& Names, int32 MaxItems = 6);
};
