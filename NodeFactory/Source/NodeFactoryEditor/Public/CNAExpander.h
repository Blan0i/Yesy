// Copyright (c) NodeFactory. Expansion of a custom node into ordinary nodes when a Blueprint compiles (spec 5, D3).

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "CNATypes.h"

class FKismetCompilerContext;
class UCustomNodeAsset;
class UEdGraph;
class UEdGraphNode;
class UK2Node;
class UK2Node_CustomNode;
class UK2Node_GetCaller;
class UK2Node_GetLocal;
class UK2Node_SetLocal;
class UK2Node_Tunnel;
enum class ECNAMessage : uint8;

/**
 * Replaces one UK2Node_CustomNode by a clone of the macro Execute of its asset (spec 5.1).
 *
 * The node does not derive from UK2Node_MacroInstance (D3): the compiler expands macros before ExpandNode (V2), so
 * the expander inlines everything itself - the macro Execute, the macros and collapsed graphs inside it, reroute
 * nodes, Get Local, Set Local and Get Caller. Only nodes the compiler has to translate stay in the compiled graph.
 *
 * Steps (spec 5.1):
 *  1. ValidateNodeDuringCompilation checks, unless the compiler already ran them on this copy.
 *  2. The macro Execute is cloned; every instance gets its own clone.
 *  3. Dynamic element pins are gathered by Make Array.
 *  4. Split struct pins are expanded the standard way (UK2Node::ExpandSplitPin, V21).
 *  5. Local variables become UK2Node_TemporaryVariable nodes; each exec entry resets them first (spec 4.8, V28).
 *  6. Get Caller becomes Self (spec 4.9, V29).
 *  7. Inputs: wires and values of the node go to the consumers of the Tunnel Entry pins.
 *  8. Outputs: the sources of the Tunnel Exit pins drive the consumers of the node; pass-through pins are wired
 *     directly to their input.
 *  9. Default To Self pins without a wire get a Self node.
 * 10. Nested custom nodes are expanded right away with the asset stack (cycles CNA001, depth CNA002, V3).
 * 11. The node is disconnected.
 *
 * Every node of the clone is reported to the compiler log as created by the instance, so errors inside the subgraph
 * and the breakpoint F9 refer to the instance (spec 5.3, 5.4, V12).
 */
class NODEFACTORYEDITOR_API FCNAExpander
{
public:
	/** Entry point of UK2Node_CustomNode::ExpandNode. Each compiled copy of a node is expanded once. */
	static void Expand(UK2Node_CustomNode* Node, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph);

	/** Counters of the expansions since the last reset, read by the automation tests (spec 11.1). */
	struct FStats
	{
		int32 ExpandedInstances = 0;
		int32 FailedInstances = 0;
		int32 MovedNodes = 0;
		int32 InlinedMacros = 0;
		int32 InlinedComposites = 0;
		int32 CollapsedKnots = 0;
		int32 TemporaryVariables = 0;
		int32 ResetAssignments = 0;
		int32 MakeArrayNodes = 0;
		int32 MakeArrayElements = 0;
		int32 SelfNodes = 0;
		int32 NestedInstances = 0;
		int32 PassThroughWires = 0;
		int32 MaxDepth = 0;
	};
	static const FStats& GetStats();
	static void ResetStats();

	/** Limit of macros and collapsed graphs inlined into one instance; protects against broken macro libraries. */
	static const int32 MaxInlinedInstances = 4096;

	/** Limit of macros nested inside macros. */
	static const int32 MaxMacroDepth = 64;

private:
	FCNAExpander(UK2Node_CustomNode* InNode, FKismetCompilerContext& InContext, UEdGraph* InSourceGraph);

	/** Runs the steps of spec 5.1. Returns false when the node could not be expanded. */
	bool Run();

	// Step 1

	/** Diagnostics of the node; false stops the expansion. */
	bool ValidateBeforeExpansion();

	// Cloning and inlining

	/** Transient copy of a graph whose nodes are recorded as intermediate objects of the compiler. */
	UEdGraph* CloneForCompile(UEdGraph* Source) const;

	/** Gives every Wildcard pin of the graph and its sub graphs the resolved type, like a macro instance does (V14). */
	static void ApplyWildcardType(UEdGraph* Graph, const FEdGraphPinType& Resolved);

	/** Moves the nodes of a graph into the compiled graph and sorts out the nodes that have to be replaced. */
	void TakeNodesOfGraph(UEdGraph* Graph, int32 MacroDepth, UK2Node_Tunnel*& OutEntry, UK2Node_Tunnel*& OutExit);

	/** Moves one node into the compiled graph and reports it as created by the instance. */
	void MoveToSourceGraph(UEdGraphNode* GraphNode);

	/** Expands the split struct pins of a node, inner splits first (V21). */
	void ExpandSplitPins(UK2Node* K2Node);

	/** Inlines the macro instances and collapsed graphs found so far, including the ones they contain. */
	void InlineInstances();
	bool InlineInstance(UK2Node_Tunnel* Instance, int32 MacroDepth);

	/** Wires the sources of every reroute node directly to its consumers. */
	void CollapseKnots();

	// Local variables and Get Caller (steps 5 and 6)

	/** Temporary variable of a local variable, created on first use. */
	UEdGraphPin* GetLocalVariablePin(const FCNALocalVariable& Variable);

	void ReplaceGetLocalNodes();
	void ReplaceSetLocalNodes();
	void ReplaceCallerNodes();

	/** Output of the Self node of this expansion, created on first use. */
	UEdGraphPin* GetSelfPin();

	/** Pins of a spawned UK2Node_AssignmentStatement. */
	struct FAssignmentPins
	{
		UEdGraphPin* Exec = nullptr;
		UEdGraphPin* Then = nullptr;
		UEdGraphPin* Variable = nullptr;
		UEdGraphPin* Value = nullptr;

		bool IsValid() const { return Exec && Then && Variable && Value; }
	};
	FAssignmentPins SpawnAssignment(UEdGraphPin* VariablePin, const FEdGraphPinType& Type);

	/** Assignments of the default values of all used local variables; OutHead is the exec input of the chain. */
	bool BuildResetChain(UEdGraphPin*& OutHead, UEdGraphPin*& OutTail);

	/** Writes the text form of a value (the DefaultValue format of a pin) into a pin. */
	static void SetPinDefaultFromString(UEdGraphPin* Pin, const FString& Value);

	// Boundary of an instance (steps 3, 7, 8, 9)

	/** What feeds one input of an instance: wires, or the value of a pin when there is no wire. */
	struct FInputSource
	{
		TArray<UEdGraphPin*> Sources;
		const UEdGraphPin* DefaultFrom = nullptr;
	};

	/** Extras of the custom node itself; macro instances and collapsed graphs use none. */
	struct FBoundaryOptions
	{
		/** Inputs that do not come from a pin of the same name: dynamic groups, Default To Self. */
		TMap<FName, FInputSource> InputOverrides;

		/** Output name -> input name of PassThroughOf. */
		TMap<FName, FName> PassThrough;

		/** Insert the reset of the local variables after every exec input. */
		bool bResetLocalVariables = false;

		/** Report the inner sources of the outputs as the pins of the node, so Watch works on the node (spec 5.3). */
		bool bMapOutputWatches = false;
	};

	/** Builds the Make Array nodes of the dynamic groups and returns their outputs by group name (step 3). */
	void BuildDynamicArrays(TMap<FName, FInputSource>& OutOverrides);

	/** Self for Default To Self pins without wire (step 9). */
	void BuildDefaultToSelf(TMap<FName, FInputSource>& OutOverrides);

	/** Pass-through outputs (spec 4.6). */
	void BuildPassThrough(TMap<FName, FName>& OutPassThrough) const;

	/**
	 * Bypasses an instance and the Tunnel nodes of its graph: the consumers of the Entry pins get the sources of the
	 * instance inputs and the consumers of the instance outputs get the sources of the Exit pins (steps 7 and 8).
	 */
	void CollapseBoundary(UEdGraphNode* Instance, UK2Node_Tunnel* InnerEntry, UK2Node_Tunnel* InnerExit, const FBoundaryOptions* Options);

	/** Connects a consumer to the source of an input, or copies the value when there is no wire. */
	static void ConnectSource(const FInputSource* Source, UEdGraphPin* Consumer);

	// Nested nodes and cleanup (steps 10 and 11)

	void ExpandNestedNodes();

	/** Removes the replaced nodes from the compiled graph where that is safe, otherwise leaves them without wires. */
	void RemoveReplacedNodes();

	// Helpers

	static void BreakLinksQuietly(UEdGraphNode* GraphNode);
	static UEdGraphPin* FindFirstPin(const UEdGraphNode* GraphNode, EEdGraphPinDirection Direction);
	static bool IsBoundaryTunnel(const UEdGraphNode* GraphNode);

	/** Compiler error with a link to the instance and the asset name (spec 5.4). */
	void ReportError(const FText& Message) const;
	void ReportCode(ECNAMessage Code, const FFormatNamedArguments& Arguments) const;

	/** The custom node being expanded. */
	UK2Node_CustomNode* Node;
	FKismetCompilerContext& Context;
	UEdGraph* SourceGraph;
	UCustomNodeAsset* Asset;

	/** Tunnel nodes of the clone of the macro Execute. */
	UK2Node_Tunnel* Entry;
	UK2Node_Tunnel* Exit;

	/** Macro instances and collapsed graphs waiting to be inlined, with their macro depth. */
	TArray<TPair<UK2Node_Tunnel*, int32>> PendingInstances;

	TArray<UEdGraphNode*> Knots;
	TArray<UK2Node_GetLocal*> GetLocalNodes;
	TArray<UK2Node_SetLocal*> SetLocalNodes;
	TArray<UK2Node_GetCaller*> CallerNodes;
	TArray<UK2Node_CustomNode*> NestedNodes;

	/** Nodes that were bypassed and must not be compiled. */
	TArray<UEdGraphNode*> ReplacedNodes;

	/** Used local variables in the order of creation, and their temporary variables. */
	TArray<FCNALocalVariable> UsedVariables;
	TMap<FGuid, UEdGraphPin*> LocalVariablePins;

	/** Output of the Self node of this expansion. */
	UEdGraphPin* SelfPin;

	/** Number of inlined macros and collapsed graphs. */
	int32 InlinedCount;
};
