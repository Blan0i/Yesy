// Copyright (c) NodeFactory. Expansion of a custom node into ordinary nodes when a Blueprint compiles (spec 5).

#include "CNAExpander.h"
#include "CNACurveUtils.h"
#include "CNAEditorSettings.h"
#include "CNAGraphUtils.h"
#include "CNAMessages.h"
#include "CNAPinUtils.h"
#include "CNAValidator.h"
#include "CustomNodeAsset.h"
#include "K2Node_CNACurveValue.h"
#include "K2Node_CustomNode.h"
#include "K2Node_GetCaller.h"
#include "K2Node_GetLocal.h"
#include "K2Node_SetLocal.h"
#include "NodeFactoryModule.h"

#include "Curves/CurveBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Self.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_Tunnel.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/CompilerResultsLog.h"
#include "KismetCompiler.h"
#include "Misc/Guid.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNAExpanderLocal
{
	/** Counters for the automation tests. */
	FCNAExpander::FStats Stats;

	/**
	 * State of the top-level expansion, shared with the nested expansions it runs. The compiler calls ExpandNode
	 * while it walks Graph->Nodes by index; every node appended behind RootIndex has not been visited yet and can
	 * be taken out of the graph again without disturbing that walk (V3).
	 */
	int32 ActiveExpansions = 0;
	UEdGraph* RootGraph = nullptr;
	int32 RootIndex = INDEX_NONE;

	/** Moving a clone node into the compiled graph: no redirectors, no transaction, no dirty package. */
	const ERenameFlags MoveFlags = REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional | REN_DoNotDirty;

	bool IsObjectCategory(FName Category)
	{
		return Category == UEdGraphSchema_K2::PC_Object
			|| Category == UEdGraphSchema_K2::PC_Class
			|| Category == UEdGraphSchema_K2::PC_Interface;
	}

	/** Pin type of a variable or an assignment: a value, never a reference. */
	FEdGraphPinType MakeValueType(const FEdGraphPinType& Type)
	{
		FEdGraphPinType Result = Type;
		Result.bIsReference = false;
		Result.bIsConst = false;
		return Result;
	}

	/** Same wording as FCNAMessages::AssetArgs: "Display (AssetName)" when they differ. */
	FText GetAssetText(const UCustomNodeAsset* Asset)
	{
		if (!Asset)
		{
			return FText::GetEmpty();
		}
		const FText Display = Asset->GetEffectiveDisplayName();
		const FString AssetName = Asset->GetName();
		return Display.ToString() == AssetName
			? Display
			: FText::Format(NSLOCTEXT("NodeFactory", "Expander.AssetWithName", "{0} ({1})"), Display, FText::FromString(AssetName));
	}
}

// Entry point

const FCNAExpander::FStats& FCNAExpander::GetStats()
{
	return CNAExpanderLocal::Stats;
}

void FCNAExpander::ResetStats()
{
	CNAExpanderLocal::Stats = FStats();
}

void FCNAExpander::Expand(UK2Node_CustomNode* Node, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	using namespace CNAExpanderLocal;

	if (!Node || !SourceGraph || Node->bExpanded)
	{
		return;
	}
	Node->bExpanded = true;

	const bool bTopLevel = ActiveExpansions == 0;
	if (bTopLevel)
	{
		RootGraph = SourceGraph;
		RootIndex = SourceGraph->Nodes.IndexOfByKey(Node);
	}

	{
		TGuardValue<int32> ExpansionGuard(ActiveExpansions, ActiveExpansions + 1);
		CNAExpanderLocal::Stats.MaxDepth = FMath::Max(CNAExpanderLocal::Stats.MaxDepth, Node->ExpansionStack.Num() + 1);

		FCNAExpander Expander(Node, CompilerContext, SourceGraph);
		if (Expander.Run())
		{
			++CNAExpanderLocal::Stats.ExpandedInstances;
		}
		else
		{
			++CNAExpanderLocal::Stats.FailedInstances;
		}

		// Spec 5.1 item 11: the node itself is never compiled; it keeps no wire and is pruned by the compiler.
		BreakLinksQuietly(Node);
		Expander.RemoveReplacedNodes();
	}

	if (bTopLevel)
	{
		RootGraph = nullptr;
		RootIndex = INDEX_NONE;
	}
}

FCNAExpander::FCNAExpander(UK2Node_CustomNode* InNode, FKismetCompilerContext& InContext, UEdGraph* InSourceGraph)
	: Node(InNode)
	, Context(InContext)
	, SourceGraph(InSourceGraph)
	, Asset(nullptr)
	, Entry(nullptr)
	, Exit(nullptr)
	, SelfPin(nullptr)
	, InlinedCount(0)
{
}

bool FCNAExpander::Run()
{
	// Step 1.
	if (!ValidateBeforeExpansion())
	{
		return false;
	}

	// Step 2: every instance gets its own clone of the macro Execute.
	UEdGraph* Clone = CloneForCompile(Asset->FindExecuteGraph());
	if (!Clone)
	{
		ReportCode(ECNAMessage::CNA005, FCNAMessages::AssetArgs(Asset));
		return false;
	}

	// The Wildcard type of the instance goes into the clone before anything is wired (spec 4.5, V14).
	if (Node->HasWildcardPins() && Node->IsWildcardResolved())
	{
		ApplyWildcardType(Clone, Node->ResolvedWildcardType);
	}

	// Step 4 for the pins of the node: a split pin becomes the whole pin, fed by Make or read by Break (V21).
	ExpandSplitPins(Node);

	TakeNodesOfGraph(Clone, 0, Entry, Exit);
	if (!Entry || !Exit)
	{
		ReportCode(ECNAMessage::CNA005, FCNAMessages::AssetArgs(Asset));
		return false;
	}

	// Macros and collapsed graphs of the subgraph are inlined here: the compiler expanded its macros before
	// ExpandNode and would not see these (V2).
	InlineInstances();
	CollapseKnots();

	// Steps 5 and 6.
	ReplaceGetLocalNodes();
	ReplaceSetLocalNodes();
	ReplaceCallerNodes();

	// Steps 3, 7, 8 and 9. The curves read the curve pins of the node before the boundary takes its wires.
	FBoundaryOptions Options;
	BuildCurveDrivers(Options);
	ReplaceCurveValueNodes();
	BuildDynamicArrays(Options.InputOverrides);
	BuildDynamicOutputs(Options.OutputOverrides);
	BuildDefaultToSelf(Options.InputOverrides);
	BuildPassThrough(Options.PassThrough);
	Options.bResetLocalVariables = UsedVariables.Num() > 0;
	Options.bMapOutputWatches = true;
	CollapseBoundary(Node, Entry, Exit, &Options);

	// Step 10.
	ExpandNestedNodes();

	CNA_VERBOSE(TEXT("Expanded %s (%s) in %s: %d nodes, %d macros or collapsed graphs, %d local variables, depth %d."),
		*Node->GetName(), *Asset->GetName(), *GetNameSafe(SourceGraph), CNAExpanderLocal::Stats.MovedNodes, InlinedCount,
		UsedVariables.Num(), Node->ExpansionStack.Num() + 1);
	return true;
}

// Step 1

bool FCNAExpander::ValidateBeforeExpansion()
{
	// The compiler may already have validated this copy; the diagnostics are reported once (spec 5.1 item 1).
	if (!Node->bCompileDiagnosticsReported)
	{
		Node->bCompileDiagnosticsReported = true;
		FCNADiagnosticList Diagnostics;
		FCNAValidator::ValidateNodeForCompilation(Node, Diagnostics);
		Node->bCompileHasErrors = Diagnostics.HasErrors();
		Diagnostics.FlushToCompilerLog(Context.MessageLog, Node);
	}
	if (Node->bCompileHasErrors)
	{
		return false;
	}

	Asset = Node->GetCustomNodeAsset();
	if (!Asset || !FCNAGraphUtils::HasValidExecuteGraph(Asset))
	{
		ReportCode(ECNAMessage::CNA005, FCNAMessages::AssetArgs(Asset));
		return false;
	}

	// Last line of defence against endless recursion, independent of the validator (spec 5.2).
	const int32 MaxDepth = FMath::Max(1, GetDefault<UCNAEditorSettings>()->MaxExpansionDepth);
	if (Node->ExpansionStack.Contains(Asset))
	{
		TArray<FString> Chain;
		for (const UCustomNodeAsset* StackAsset : Node->ExpansionStack)
		{
			Chain.Add(GetNameSafe(StackAsset));
		}
		Chain.Add(Asset->GetName());
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Chain"), FText::FromString(FString::Join(Chain, TEXT(" -> "))));
		ReportCode(ECNAMessage::CNA001, Arguments);
		return false;
	}
	if (Node->ExpansionStack.Num() >= MaxDepth)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Depth"), FText::AsNumber(Node->ExpansionStack.Num() + 1));
		Arguments.Add(TEXT("Limit"), FText::AsNumber(MaxDepth));
		ReportCode(ECNAMessage::CNA002, Arguments);
		return false;
	}
	return true;
}

// Cloning and inlining

UEdGraph* FCNAExpander::CloneForCompile(UEdGraph* Source) const
{
	if (!Source)
	{
		return nullptr;
	}
	// The transient clone is only a staging area: its nodes move into the compiled graph, whose owner is the
	// Blueprint being compiled (V1). The compiler log records the clones as intermediate objects.
	return FEdGraphUtilities::CloneGraph(Source, nullptr, &Context.MessageLog, true);
}

void FCNAExpander::ApplyWildcardType(UEdGraph* Graph, const FEdGraphPinType& Resolved)
{
	if (!Graph || Resolved.PinCategory.IsNone() || FCNAPinUtils::IsWildcardType(Resolved))
	{
		return;
	}

	TArray<UEdGraph*> Graphs;
	FCNAGraphUtils::CollectGraphs(Graph, Graphs);
	for (UEdGraph* EachGraph : Graphs)
	{
		for (UEdGraphNode* GraphNode : EachGraph->Nodes)
		{
			if (!GraphNode)
			{
				continue;
			}
			bool bHadWildcard = false;
			for (UEdGraphPin* Pin : GraphNode->Pins)
			{
				if (Pin && FCNAPinUtils::IsWildcardType(Pin->PinType))
				{
					FCNAPinUtils::ApplyResolvedWildcard(Pin->PinType, Resolved);
					bHadWildcard = true;
				}
			}

			// A macro of the subgraph whose Wildcard pins were typed by ours takes the same type when it is inlined.
			UK2Node_MacroInstance* MacroInstance = Cast<UK2Node_MacroInstance>(GraphNode);
			if (bHadWildcard && MacroInstance && MacroInstance->ResolvedWildcardType.PinCategory.IsNone())
			{
				MacroInstance->ResolvedWildcardType = FCNAPinUtils::GetElementType(Resolved);
			}
		}
	}
}

void FCNAExpander::TakeNodesOfGraph(UEdGraph* Graph, int32 MacroDepth, UK2Node_Tunnel*& OutEntry, UK2Node_Tunnel*& OutExit)
{
	OutEntry = FCNAGraphUtils::FindEntryTunnel(Graph);
	OutExit = FCNAGraphUtils::FindExitTunnel(Graph);
	if (!Graph)
	{
		return;
	}

	const TArray<UEdGraphNode*> GraphNodes = Graph->Nodes;
	for (UEdGraphNode* GraphNode : GraphNodes)
	{
		UK2Node* K2Node = Cast<UK2Node>(GraphNode);
		if (!K2Node)
		{
			// Comments and other editor nodes are not compiled; they stay in the transient clone.
			continue;
		}

		// Every node moves, also the ones that are bypassed later: wires are only made between nodes of one graph.
		MoveToSourceGraph(K2Node);

		if (IsBoundaryTunnel(K2Node))
		{
			ReplacedNodes.Add(K2Node);
		}
		else if (K2Node->IsA<UK2Node_MacroInstance>() || K2Node->IsA<UK2Node_Composite>())
		{
			PendingInstances.Add(TPair<UK2Node_Tunnel*, int32>(CastChecked<UK2Node_Tunnel>(K2Node), MacroDepth));
			ReplacedNodes.Add(K2Node);
		}
		else if (K2Node->IsA<UK2Node_Knot>())
		{
			Knots.Add(K2Node);
			ReplacedNodes.Add(K2Node);
		}
		else if (UK2Node_GetLocal* GetLocalNode = Cast<UK2Node_GetLocal>(K2Node))
		{
			GetLocalNodes.Add(GetLocalNode);
			ReplacedNodes.Add(K2Node);
		}
		else if (UK2Node_SetLocal* SetLocalNode = Cast<UK2Node_SetLocal>(K2Node))
		{
			SetLocalNodes.Add(SetLocalNode);
			ReplacedNodes.Add(K2Node);
		}
		else if (UK2Node_GetCaller* CallerNode = Cast<UK2Node_GetCaller>(K2Node))
		{
			CallerNodes.Add(CallerNode);
			ReplacedNodes.Add(K2Node);
		}
		else if (UK2Node_CNACurveValue* CurveNode = Cast<UK2Node_CNACurveValue>(K2Node))
		{
			CurveValueNodes.Add(CurveNode);
			ReplacedNodes.Add(K2Node);
		}
		else if (UK2Node_CustomNode* NestedNode = Cast<UK2Node_CustomNode>(K2Node))
		{
			NestedNodes.Add(NestedNode);
		}
	}
}

void FCNAExpander::MoveToSourceGraph(UEdGraphNode* GraphNode)
{
	if (UEdGraph* OldGraph = Cast<UEdGraph>(GraphNode->GetOuter()))
	{
		OldGraph->Nodes.Remove(GraphNode);
	}
	GraphNode->Rename(nullptr, SourceGraph, CNAExpanderLocal::MoveFlags);
	SourceGraph->Nodes.Add(GraphNode);

	// The instance is the source of every node of its subgraph: errors and the breakpoint refer to the
	// instance (spec 5.3, 5.4, V12). The log follows the chain back to the node in the editor graph.
	Context.MessageLog.NotifyIntermediateObjectCreation(GraphNode, Node);
	++CNAExpanderLocal::Stats.MovedNodes;

	if (UK2Node* K2Node = Cast<UK2Node>(GraphNode))
	{
		ExpandSplitPins(K2Node);
	}
}

void FCNAExpander::ExpandSplitPins(UK2Node* K2Node)
{
	if (!K2Node)
	{
		return;
	}

	// Inner splits first: a struct inside a split struct is rebuilt before its parent (V21).
	for (int32 Guard = 0; Guard < 256; ++Guard)
	{
		UEdGraphPin* LeafSplit = nullptr;
		for (UEdGraphPin* Pin : K2Node->Pins)
		{
			if (Pin && Pin->SubPins.Num() > 0)
			{
				const bool bHasSplitChild = Pin->SubPins.ContainsByPredicate([](const UEdGraphPin* SubPin)
				{
					return SubPin && SubPin->SubPins.Num() > 0;
				});
				if (!bHasSplitChild)
				{
					LeafSplit = Pin;
					break;
				}
			}
		}
		if (!LeafSplit)
		{
			return;
		}

		const TArray<UEdGraphPin*> SubPins = LeafSplit->SubPins;

		// V21: the standard expansion spawns Make or Break, moves the wires of the sub pins to it and wires it to
		// the parent pin.
		K2Node->ExpandSplitPin(&Context, SourceGraph, LeafSplit);

		// The sub pins have no wires left; they are removed so the split is not expanded a second time.
		for (UEdGraphPin* SubPin : SubPins)
		{
			if (SubPin && K2Node->Pins.Contains(SubPin))
			{
				SubPin->BreakAllPinLinks();
				K2Node->Pins.Remove(SubPin);
				SubPin->MarkPendingKill();	// V21: UEdGraphPin::MarkPendingKill releases a pin removed by hand.
			}
		}
		LeafSplit->SubPins.Reset();
		LeafSplit->bHidden = false;
	}

	UE_LOG(LogNodeFactory, Warning, TEXT("Split pins of %s could not be expanded completely."), *GetNameSafe(K2Node));
}

void FCNAExpander::InlineInstances()
{
	while (PendingInstances.Num() > 0)
	{
		const TPair<UK2Node_Tunnel*, int32> Item = PendingInstances.Pop(false);
		if (++InlinedCount > MaxInlinedInstances)
		{
			ReportError(FText::Format(
				LOCTEXT("TooManyInlined", "the subgraph of {0} contains more than {1} macros and collapsed graphs; the expansion stopped."),
				CNAExpanderLocal::GetAssetText(Asset), FText::AsNumber(MaxInlinedInstances)));
			PendingInstances.Reset();
			return;
		}
		if (Item.Value >= MaxMacroDepth)
		{
			ReportError(FText::Format(
				LOCTEXT("MacroTooDeep", "the macros inside the subgraph of {0} are nested deeper than {1} levels."),
				CNAExpanderLocal::GetAssetText(Asset), FText::AsNumber(MaxMacroDepth)));
			BreakLinksQuietly(Item.Key);
			continue;
		}
		InlineInstance(Item.Key, Item.Value + 1);
	}
}

bool FCNAExpander::InlineInstance(UK2Node_Tunnel* Instance, int32 MacroDepth)
{
	if (!Instance)
	{
		return false;
	}

	UEdGraph* InnerGraph = nullptr;
	if (UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Instance))
	{
		// The collapsed graph was duplicated together with the clone; its nodes are moved as they are.
		InnerGraph = Composite->BoundGraph;
		++CNAExpanderLocal::Stats.InlinedComposites;
	}
	else if (UK2Node_MacroInstance* MacroInstance = Cast<UK2Node_MacroInstance>(Instance))
	{
		// A macro lives in another Blueprint and may be used many times: every use gets its own clone.
		InnerGraph = CloneForCompile(MacroInstance->GetMacroGraph());
		if (InnerGraph && !MacroInstance->ResolvedWildcardType.PinCategory.IsNone())
		{
			ApplyWildcardType(InnerGraph, MacroInstance->ResolvedWildcardType);
		}
		++CNAExpanderLocal::Stats.InlinedMacros;
	}

	if (!InnerGraph)
	{
		ReportError(FText::Format(
			LOCTEXT("InstanceGraphMissing", "the subgraph of {0} uses {1}, whose graph was not found."),
			CNAExpanderLocal::GetAssetText(Asset), FText::FromString(FCNAGraphUtils::GetNodeName(Instance))));
		BreakLinksQuietly(Instance);
		return false;
	}

	UK2Node_Tunnel* InnerEntry = nullptr;
	UK2Node_Tunnel* InnerExit = nullptr;
	TakeNodesOfGraph(InnerGraph, MacroDepth, InnerEntry, InnerExit);
	CollapseBoundary(Instance, InnerEntry, InnerExit, nullptr);
	return true;
}

void FCNAExpander::CollapseKnots()
{
	// Reroute nodes are bypassed one by one; chains of them resolve in any order.
	for (UEdGraphNode* Knot : Knots)
	{
		UEdGraphPin* InputPin = FindFirstPin(Knot, EGPD_Input);
		UEdGraphPin* OutputPin = FindFirstPin(Knot, EGPD_Output);
		if (!InputPin || !OutputPin)
		{
			BreakLinksQuietly(Knot);
			continue;
		}
		const TArray<UEdGraphPin*> Sources = InputPin->LinkedTo;
		const TArray<UEdGraphPin*> Consumers = OutputPin->LinkedTo;
		BreakLinksQuietly(Knot);
		for (UEdGraphPin* Consumer : Consumers)
		{
			for (UEdGraphPin* Source : Sources)
			{
				Source->MakeLinkTo(Consumer);
			}
		}
		++CNAExpanderLocal::Stats.CollapsedKnots;
	}
	Knots.Reset();
}

// Local variables and Get Caller (steps 5 and 6)

UEdGraphPin* FCNAExpander::GetLocalVariablePin(const FCNALocalVariable& Variable)
{
	if (UEdGraphPin** Found = LocalVariablePins.Find(Variable.Guid))
	{
		return *Found;
	}

	// V28: one temporary variable per variable and instance; in an Event Graph it lives in the frame of the
	// ubergraph, in a function on its stack. Either way no other instance can see it (spec 4.8 item 5).
	UK2Node_TemporaryVariable* Temporary = Context.SpawnIntermediateNode<UK2Node_TemporaryVariable>(Node, SourceGraph);
	Temporary->VariableType = CNAExpanderLocal::MakeValueType(Variable.Type);
	Temporary->AllocateDefaultPins();

	UEdGraphPin* VariablePin = FindFirstPin(Temporary, EGPD_Output);
	if (!VariablePin)
	{
		ReportError(FText::Format(LOCTEXT("TemporaryFailed", "the local variable {0} of {1} could not be created."),
			FText::FromName(Variable.Name), CNAExpanderLocal::GetAssetText(Asset)));
		return nullptr;
	}
	LocalVariablePins.Add(Variable.Guid, VariablePin);
	UsedVariables.Add(Variable);
	++CNAExpanderLocal::Stats.TemporaryVariables;
	return VariablePin;
}

void FCNAExpander::ReplaceGetLocalNodes()
{
	for (UK2Node_GetLocal* GetNode : GetLocalNodes)
	{
		UEdGraphPin* ValuePin = GetNode->GetValuePin();
		const TArray<UEdGraphPin*> Consumers = ValuePin ? ValuePin->LinkedTo : TArray<UEdGraphPin*>();
		const FCNALocalVariable* Variable = Asset->FindLocalVariable(GetNode->VariableGuid, GetNode->VariableName);
		BreakLinksQuietly(GetNode);

		if (!Variable)
		{
			FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
			Arguments.Add(TEXT("Node"), GetNode->GetNodeTitle(ENodeTitleType::ListView));
			Arguments.Add(TEXT("Variable"), FText::FromName(GetNode->VariableName));
			ReportCode(ECNAMessage::CNA017, Arguments);
			continue;
		}
		if (Consumers.Num() == 0)
		{
			continue;
		}
		if (UEdGraphPin* VariablePin = GetLocalVariablePin(*Variable))
		{
			for (UEdGraphPin* Consumer : Consumers)
			{
				VariablePin->MakeLinkTo(Consumer);
			}
		}
	}
	GetLocalNodes.Reset();
}

void FCNAExpander::ReplaceSetLocalNodes()
{
	for (UK2Node_SetLocal* SetNode : SetLocalNodes)
	{
		UEdGraphPin* ExecPin = SetNode->GetExecPin();
		UEdGraphPin* ThenPin = SetNode->GetThenPin();
		UEdGraphPin* ValueInput = SetNode->GetValueInputPin();
		UEdGraphPin* ValueOutput = SetNode->GetValueOutputPin();

		const TArray<UEdGraphPin*> ExecSources = ExecPin ? ExecPin->LinkedTo : TArray<UEdGraphPin*>();
		const TArray<UEdGraphPin*> ThenTargets = ThenPin ? ThenPin->LinkedTo : TArray<UEdGraphPin*>();
		const TArray<UEdGraphPin*> Readers = ValueOutput ? ValueOutput->LinkedTo : TArray<UEdGraphPin*>();
		UEdGraphPin* ValueSource = (ValueInput && ValueInput->LinkedTo.Num() > 0) ? ValueInput->LinkedTo[0] : nullptr;
		const FCNALocalVariable* Variable = Asset->FindLocalVariable(SetNode->VariableGuid, SetNode->VariableName);
		BreakLinksQuietly(SetNode);

		if (!Variable)
		{
			FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
			Arguments.Add(TEXT("Node"), SetNode->GetNodeTitle(ENodeTitleType::ListView));
			Arguments.Add(TEXT("Variable"), FText::FromName(SetNode->VariableName));
			ReportCode(ECNAMessage::CNA017, Arguments);
			continue;
		}

		UEdGraphPin* VariablePin = GetLocalVariablePin(*Variable);
		if (!VariablePin)
		{
			continue;
		}

		// Set Local becomes Assign: exec in, exec out, the variable and the new value.
		const FAssignmentPins Assignment = SpawnAssignment(VariablePin, VariablePin->PinType);
		if (!Assignment.IsValid())
		{
			continue;
		}
		for (UEdGraphPin* Source : ExecSources)
		{
			Source->MakeLinkTo(Assignment.Exec);
		}
		for (UEdGraphPin* Target : ThenTargets)
		{
			Assignment.Then->MakeLinkTo(Target);
		}
		if (ValueSource)
		{
			ValueSource->MakeLinkTo(Assignment.Value);
		}
		else if (ValueInput)
		{
			FCNAPinUtils::CopyDefaultValue(ValueInput, Assignment.Value);
		}

		// The output of Set Local reads the variable, like the output of a standard Set node.
		for (UEdGraphPin* Reader : Readers)
		{
			VariablePin->MakeLinkTo(Reader);
		}
	}
	SetLocalNodes.Reset();
}

FCNAExpander::FAssignmentPins FCNAExpander::SpawnAssignment(UEdGraphPin* VariablePin, const FEdGraphPinType& Type)
{
	FAssignmentPins Result;
	if (!VariablePin)
	{
		return Result;
	}

	UK2Node_AssignmentStatement* Assignment = Context.SpawnIntermediateNode<UK2Node_AssignmentStatement>(Node, SourceGraph);
	Assignment->AllocateDefaultPins();
	Result.Exec = Assignment->FindPin(UEdGraphSchema_K2::PN_Execute);
	Result.Then = Assignment->FindPin(UEdGraphSchema_K2::PN_Then);
	// V28: the pins of UK2Node_AssignmentStatement are named "Variable" and "Value".
	Result.Variable = Assignment->FindPin(TEXT("Variable"));
	Result.Value = Assignment->FindPin(TEXT("Value"));
	if (!Result.IsValid())
	{
		ReportError(LOCTEXT("AssignmentFailed", "an assignment of a local variable could not be created."));
		return FAssignmentPins();
	}

	// The Wildcard pins of Assign take the type of the variable; nothing else would type them during compilation.
	const FEdGraphPinType ValueType = CNAExpanderLocal::MakeValueType(Type);
	Result.Variable->PinType = ValueType;
	Result.Value->PinType = ValueType;
	VariablePin->MakeLinkTo(Result.Variable);
	return Result;
}

bool FCNAExpander::BuildResetChain(UEdGraphPin*& OutHead, UEdGraphPin*& OutTail)
{
	OutHead = nullptr;
	OutTail = nullptr;
	for (const FCNALocalVariable& Variable : UsedVariables)
	{
		UEdGraphPin* const* VariablePin = LocalVariablePins.Find(Variable.Guid);
		if (!VariablePin || !*VariablePin)
		{
			continue;
		}
		const FAssignmentPins Assignment = SpawnAssignment(*VariablePin, (*VariablePin)->PinType);
		if (!Assignment.IsValid())
		{
			continue;
		}
		SetPinDefaultFromString(Assignment.Value, Variable.DefaultValue);
		if (OutTail)
		{
			OutTail->MakeLinkTo(Assignment.Exec);
		}
		else
		{
			OutHead = Assignment.Exec;
		}
		OutTail = Assignment.Then;
		++CNAExpanderLocal::Stats.ResetAssignments;
	}
	return OutHead != nullptr && OutTail != nullptr;
}

void FCNAExpander::SetPinDefaultFromString(UEdGraphPin* Pin, const FString& Value)
{
	if (!Pin)
	{
		return;
	}
	Pin->DefaultValue.Empty();
	Pin->DefaultObject = nullptr;
	Pin->DefaultTextValue = FText::GetEmpty();

	const FEdGraphPinType& Type = Pin->PinType;
	if (Type.IsContainer())
	{
		// Containers are reset to their literal text, normally empty.
		Pin->DefaultValue = Value;
		return;
	}
	if (CNAExpanderLocal::IsObjectCategory(Type.PinCategory))
	{
		if (!Value.IsEmpty() && Value != TEXT("None"))
		{
			const FSoftObjectPath Path(Value);
			UObject* Object = Path.ResolveObject();
			if (!Object)
			{
				Object = Path.TryLoad();
			}
			Pin->DefaultObject = Object;
		}
		return;
	}
	if (Type.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		Pin->DefaultTextValue = FText::FromString(Value);
		return;
	}
	if (Value.IsEmpty() && Type.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		// An enum needs a valid name; the first entry is the natural default.
		if (const UEnum* Enum = Cast<UEnum>(Type.PinSubCategoryObject.Get()))
		{
			if (Enum->NumEnums() > 0)
			{
				Pin->DefaultValue = Enum->GetNameStringByIndex(0);
				return;
			}
		}
	}
	Pin->DefaultValue = Value;
}

void FCNAExpander::ReplaceCallerNodes()
{
	for (UK2Node_GetCaller* CallerNode : CallerNodes)
	{
		UEdGraphPin* CallerPin = CallerNode->GetCallerPin();
		const TArray<UEdGraphPin*> Consumers = CallerPin ? CallerPin->LinkedTo : TArray<UEdGraphPin*>();
		BreakLinksQuietly(CallerNode);
		if (Consumers.Num() == 0)
		{
			continue;
		}
		// V29: Self of the compiled Blueprint is the caller; WorldContext pins accept it like any object.
		if (UEdGraphPin* Self = GetSelfPin())
		{
			for (UEdGraphPin* Consumer : Consumers)
			{
				Self->MakeLinkTo(Consumer);
			}
		}
	}
	CallerNodes.Reset();
}

UEdGraphPin* FCNAExpander::GetSelfPin()
{
	if (!SelfPin)
	{
		UK2Node_Self* SelfNode = Context.SpawnIntermediateNode<UK2Node_Self>(Node, SourceGraph);
		SelfNode->AllocateDefaultPins();
		SelfPin = FindFirstPin(SelfNode, EGPD_Output);
		++CNAExpanderLocal::Stats.SelfNodes;
	}
	return SelfPin;
}

// Boundary of the node (steps 3, 7, 8, 9)

void FCNAExpander::BuildDynamicArrays(TMap<FName, FInputSource>& OutOverrides)
{
	TArray<FCNASignaturePin> Signature;
	if (!FCNAGraphUtils::GetSignature(Asset, Signature))
	{
		return;
	}

	// V-check: UK2Node_MakeArray keeps its pin count in the property NumInputs (UK2Node_MakeContainer in 4.20).
	UIntProperty* NumInputsProperty = FindField<UIntProperty>(UK2Node_MakeArray::StaticClass(), TEXT("NumInputs"));

	for (const FCNASignaturePin& SignaturePin : Signature)
	{
		const FCNAPinOverride* Override = Asset->FindPinOverride(SignaturePin.Name);
		if (!SignaturePin.IsInput() || !Override || !Override->bDynamicGroup || !SignaturePin.Type.IsArray())
		{
			continue;
		}

		TArray<UEdGraphPin*> Elements;
		Node->GetPinsOfSignaturePin(SignaturePin.Name, Elements);
		Elements.RemoveAll([](const UEdGraphPin* Pin) { return FCNAPinUtils::GetDynamicIndex(Pin->PinName) == INDEX_NONE; });
		Elements.Sort([](const UEdGraphPin& A, const UEdGraphPin& B)
		{
			return FCNAPinUtils::GetDynamicIndex(A.PinName) < FCNAPinUtils::GetDynamicIndex(B.PinName);
		});

		// The subgraph always receives the array input; without elements its consumers keep the empty literal.
		FInputSource& Source = OutOverrides.Add(SignaturePin.Name);
		if (Elements.Num() == 0)
		{
			continue;
		}
		if (!NumInputsProperty)
		{
			ReportError(LOCTEXT("MakeArrayMissing", "Make Array of the dynamic pins could not be created (V-check: UK2Node_MakeArray::NumInputs)."));
			continue;
		}

		// Step 3: one Make Array per group; element I goes into input I (spec 4.4 item 5).
		UK2Node_MakeArray* MakeArray = Context.SpawnIntermediateNode<UK2Node_MakeArray>(Node, SourceGraph);
		NumInputsProperty->SetPropertyValue_InContainer(MakeArray, Elements.Num());
		MakeArray->AllocateDefaultPins();

		UEdGraphPin* ArrayPin = nullptr;
		TArray<UEdGraphPin*> InputPins;
		for (UEdGraphPin* Pin : MakeArray->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			if (Pin->Direction == EGPD_Output)
			{
				ArrayPin = Pin;
			}
			else
			{
				InputPins.Add(Pin);
			}
		}
		if (!ArrayPin || InputPins.Num() != Elements.Num())
		{
			ReportError(LOCTEXT("MakeArrayPins", "Make Array of the dynamic pins has an unexpected set of pins."));
			continue;
		}

		FEdGraphPinType ElementType = CNAExpanderLocal::MakeValueType(Elements[0]->PinType);
		ElementType.ContainerType = EPinContainerType::None;
		ArrayPin->PinType = ElementType;
		ArrayPin->PinType.ContainerType = EPinContainerType::Array;
		for (int32 Index = 0; Index < Elements.Num(); ++Index)
		{
			InputPins[Index]->PinType = ElementType;
			// Wires and values of the element pins; also records the pins for Watch (spec 5.3).
			Context.MovePinLinksToIntermediate(*Elements[Index], *InputPins[Index]);
		}

		Source.Sources.Add(ArrayPin);
		++CNAExpanderLocal::Stats.MakeArrayNodes;
		CNAExpanderLocal::Stats.MakeArrayElements += Elements.Num();
	}
}

void FCNAExpander::BuildDynamicOutputs(TMap<FName, TArray<UEdGraphPin*>>& OutConsumers)
{
	TArray<FCNASignaturePin> Signature;
	if (!FCNAGraphUtils::GetSignature(Asset, Signature))
	{
		return;
	}

	for (const FCNASignaturePin& SignaturePin : Signature)
	{
		const FCNAPinOverride* Override = Asset->FindPinOverride(SignaturePin.Name);
		if (SignaturePin.IsInput() || !Override || !Override->bDynamicGroup || !SignaturePin.Type.IsArray())
		{
			continue;
		}

		TArray<UEdGraphPin*> Elements;
		Node->GetPinsOfSignaturePin(SignaturePin.Name, Elements);
		for (UEdGraphPin* Element : Elements)
		{
			const int32 Index = FCNAPinUtils::GetDynamicIndex(Element->PinName);
			if (Element->Direction != EGPD_Output || Index == INDEX_NONE || Element->LinkedTo.Num() == 0)
			{
				continue;
			}
			FEdGraphPinType ElementType = CNAExpanderLocal::MakeValueType(Element->PinType);
			ElementType.ContainerType = EPinContainerType::None;

			// Element I of the array output of the subgraph: Array Get with the literal index I.
			UEdGraphPin* ArrayPin = nullptr;
			UEdGraphPin* IndexPin = nullptr;
			UEdGraphPin* ItemPin = SpawnArrayGet(ElementType, ArrayPin, IndexPin);
			if (!ItemPin)
			{
				continue;
			}
			IndexPin->DefaultValue = FString::FromInt(Index);
			// Wires of the element pin; also records the pin for Watch (spec 5.3).
			Context.MovePinLinksToIntermediate(*Element, *ItemPin);
			OutConsumers.FindOrAdd(SignaturePin.Name).Add(ArrayPin);
		}
	}
}

void FCNAExpander::BuildDefaultToSelf(TMap<FName, FInputSource>& OutOverrides)
{
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->ParentPin || Pin->Direction != EGPD_Input || Pin->LinkedTo.Num() > 0 || Pin->DefaultObject)
		{
			continue;
		}
		const FName Category = Pin->PinType.PinCategory;
		if (Category != UEdGraphSchema_K2::PC_Object && Category != UEdGraphSchema_K2::PC_Interface)
		{
			continue;
		}
		const FCNAPinOverride* Override = Asset->FindPinOverride(Pin->PinName);
		if (!Override || !Override->bDefaultToSelf)
		{
			continue;
		}
		// Step 9 (spec 4.6): an object pin without a wire reads Self of the caller.
		if (UEdGraphPin* Self = GetSelfPin())
		{
			FInputSource& Source = OutOverrides.Add(Pin->PinName);
			Source.Sources.Add(Self);
		}
	}
}

void FCNAExpander::BuildPassThrough(TMap<FName, FName>& OutPassThrough) const
{
	for (const FCNAPinOverride& Override : Asset->PinOverrides)
	{
		if (!Override.PassThroughOf.IsNone() && FCNAPinUtils::FindTopLevelPin(Node, Override.PinName, EGPD_Output))
		{
			OutPassThrough.Add(Override.PinName, Override.PassThroughOf);
		}
	}
}

void FCNAExpander::CollapseBoundary(UEdGraphNode* Instance, UK2Node_Tunnel* InnerEntry, UK2Node_Tunnel* InnerExit, const FBoundaryOptions* Options)
{
	if (!Instance)
	{
		return;
	}

	// Everything is captured before the first wire changes.
	TMap<FName, FInputSource> InputSources;
	TMap<FName, UEdGraphPin*> OutputPins;
	TMap<FName, TArray<UEdGraphPin*>> OutputConsumers;
	for (UEdGraphPin* Pin : Instance->Pins)
	{
		if (!Pin || Pin->ParentPin)
		{
			continue;
		}
		if (Pin->Direction == EGPD_Input)
		{
			FInputSource& Source = InputSources.Add(Pin->PinName);
			Source.Sources = Pin->LinkedTo;
			Source.DefaultFrom = Pin;
		}
		else
		{
			OutputPins.Add(Pin->PinName, Pin);
			OutputConsumers.Add(Pin->PinName, Pin->LinkedTo);
		}
	}
	if (Options)
	{
		for (const TPair<FName, FInputSource>& Override : Options->InputOverrides)
		{
			InputSources.Add(Override.Key, Override.Value);
		}
		for (const TPair<FName, TArray<UEdGraphPin*>>& Extra : Options->OutputOverrides)
		{
			OutputConsumers.FindOrAdd(Extra.Key).Append(Extra.Value);
		}
	}

	struct FTunnelPinWires
	{
		UEdGraphPin* TunnelPin;
		TArray<UEdGraphPin*> Links;
	};
	TArray<FTunnelPinWires> EntryWires;
	TArray<UEdGraphPin*> ExitPins;
	TMap<UEdGraphPin*, TArray<UEdGraphPin*>> ExitSources;
	if (InnerEntry)
	{
		for (UEdGraphPin* Pin : InnerEntry->Pins)
		{
			if (Pin && !Pin->ParentPin && Pin->Direction == EGPD_Output)
			{
				EntryWires.Add(FTunnelPinWires{ Pin, Pin->LinkedTo });
			}
		}
	}
	if (InnerExit)
	{
		for (UEdGraphPin* Pin : InnerExit->Pins)
		{
			if (Pin && !Pin->ParentPin && Pin->Direction == EGPD_Input)
			{
				ExitPins.Add(Pin);
				ExitSources.Add(Pin, Pin->LinkedTo);
			}
		}
	}

	BreakLinksQuietly(Instance);
	BreakLinksQuietly(InnerEntry);
	BreakLinksQuietly(InnerExit);

	auto IsOnNode = [](const UEdGraphPin* Pin, const UEdGraphNode* Owner)
	{
		return Owner && Pin && Pin->GetOwningNode() == Owner;
	};

	// Step 7: the consumers of every Entry pin get the source of the input of the same name.
	for (const FTunnelPinWires& Wires : EntryWires)
	{
		const FInputSource* Source = InputSources.Find(Wires.TunnelPin->PinName);

		// Spec 4.8 item 4: every entry through an exec input resets the local variables before the subgraph runs.
		UEdGraphPin* ResetHead = nullptr;
		UEdGraphPin* ResetTail = nullptr;
		const bool bReset = Options && Options->bResetLocalVariables && FCNAPinUtils::IsExecPin(Wires.TunnelPin)
			&& Source && Source->Sources.Num() > 0 && Wires.Links.Num() > 0;
		if (bReset && BuildResetChain(ResetHead, ResetTail))
		{
			for (UEdGraphPin* OuterSource : Source->Sources)
			{
				OuterSource->MakeLinkTo(ResetHead);
			}
			for (UEdGraphPin* Consumer : Wires.Links)
			{
				if (IsOnNode(Consumer, InnerExit))
				{
					// Entry wired straight to Exit: the end of the reset chain drives that Exit pin.
					TArray<UEdGraphPin*>& InnerSources = ExitSources.FindOrAdd(Consumer);
					InnerSources.Remove(Wires.TunnelPin);
					InnerSources.AddUnique(ResetTail);
				}
				else
				{
					ResetTail->MakeLinkTo(Consumer);
				}
			}
			continue;
		}

		for (UEdGraphPin* Consumer : Wires.Links)
		{
			// Wires from Entry straight to Exit are made together with the outputs.
			if (!IsOnNode(Consumer, InnerExit))
			{
				ConnectSource(Source, Consumer);
			}
		}
	}

	// Step 8: the consumers of every output get the inner source of the Exit pin of the same name.
	for (UEdGraphPin* ExitPin : ExitPins)
	{
		const TArray<UEdGraphPin*>* Consumers = OutputConsumers.Find(ExitPin->PinName);
		if (!Consumers || Consumers->Num() == 0)
		{
			continue;
		}
		UEdGraphPin* OuterPin = OutputPins.FindRef(ExitPin->PinName);

		// Pass-through output: a direct wire from the input, the subgraph is not involved (spec 4.6).
		const FName* ThroughName = Options ? Options->PassThrough.Find(ExitPin->PinName) : nullptr;
		if (ThroughName)
		{
			const FInputSource* Source = InputSources.Find(*ThroughName);
			for (UEdGraphPin* Consumer : *Consumers)
			{
				ConnectSource(Source, Consumer);
			}
			++CNAExpanderLocal::Stats.PassThroughWires;
			continue;
		}

		const TArray<UEdGraphPin*>& InnerSources = ExitSources.FindChecked(ExitPin);
		if (InnerSources.Num() == 0)
		{
			// Nothing drives the Exit pin: its own value is the result, an exec chain simply ends.
			if (!FCNAPinUtils::IsExecPin(ExitPin))
			{
				for (UEdGraphPin* Consumer : *Consumers)
				{
					FCNAPinUtils::CopyDefaultValue(ExitPin, Consumer);
				}
			}
			continue;
		}

		for (UEdGraphPin* InnerSource : InnerSources)
		{
			if (IsOnNode(InnerSource, InnerEntry))
			{
				const FInputSource* Source = InputSources.Find(InnerSource->PinName);
				for (UEdGraphPin* Consumer : *Consumers)
				{
					ConnectSource(Source, Consumer);
				}
				continue;
			}
			for (UEdGraphPin* Consumer : *Consumers)
			{
				InnerSource->MakeLinkTo(Consumer);
			}
			if (Options && Options->bMapOutputWatches && OuterPin)
			{
				// Watch on the output of the node shows the value of its inner source (spec 5.3).
				Context.MessageLog.NotifyIntermediatePinCreation(InnerSource, OuterPin);
			}
		}
	}
}

void FCNAExpander::ConnectSource(const FInputSource* Source, UEdGraphPin* Consumer)
{
	if (!Source || !Consumer)
	{
		return;
	}
	const bool bExec = FCNAPinUtils::IsExecPin(Consumer);
	if (Source->Sources.Num() > 0)
	{
		if (bExec)
		{
			// An exec input accepts any number of wires.
			for (UEdGraphPin* OuterSource : Source->Sources)
			{
				OuterSource->MakeLinkTo(Consumer);
			}
		}
		else
		{
			Source->Sources[0]->MakeLinkTo(Consumer);
		}
	}
	else if (Source->DefaultFrom && !bExec)
	{
		// No wire: the value typed into the pin of the node is used (spec 5.1 item 7).
		FCNAPinUtils::CopyDefaultValue(Source->DefaultFrom, Consumer);
	}
}

// Curves

FCNAExpander::FPinSource FCNAExpander::FPinSource::FromPin(UEdGraphPin* InPin)
{
	FPinSource Source;
	Source.Pin = InPin;
	return Source;
}

FCNAExpander::FPinSource FCNAExpander::FPinSource::FromLiteral(const FString& InLiteral)
{
	FPinSource Source;
	Source.Literal = InLiteral;
	return Source;
}

FCNAExpander::FPinSource FCNAExpander::FPinSource::FromFloat(float Value)
{
	return FromLiteral(FString::SanitizeFloat(Value));
}

FCNAExpander::FPinSource FCNAExpander::FPinSource::FromInt(int32 Value)
{
	return FromLiteral(FString::FromInt(Value));
}

void FCNAExpander::ConnectPinSource(const FPinSource& Source, UEdGraphPin* Consumer)
{
	if (!Consumer)
	{
		return;
	}
	if (Source.Pin)
	{
		Source.Pin->MakeLinkTo(Consumer);
	}
	else
	{
		Consumer->DefaultObject = nullptr;
		Consumer->DefaultValue = Source.Literal;
	}
}

void FCNAExpander::BuildCurveDrivers(FBoundaryOptions& Options)
{
	const FCNABodyWidget* Playback = FCNACurveUtils::FindPlaybackCurve(Asset);
	for (const FCNABodyWidget& Widget : Asset->BodyWidgets)
	{
		if (!Widget.IsCurve() || !Widget.WidgetGuid.IsValid() || &Widget == Playback)
		{
			continue;
		}
		// EvaluateAtTime: the wire of the Time pin of the instance, or its value.
		FPinSource Time = FPinSource::FromFloat(0.0f);
		if (const UEdGraphPin* TimePin = Node->FindCurvePin(Widget.WidgetGuid, ECNACurvePinRole::Time))
		{
			if (TimePin->LinkedTo.Num() > 0)
			{
				Time = FPinSource::FromPin(TimePin->LinkedTo[0]);
			}
			else if (!TimePin->DefaultValue.IsEmpty())
			{
				Time = FPinSource::FromLiteral(TimePin->DefaultValue);
			}
		}
		CurveTimeSources.Add(Widget.WidgetGuid, Time);
	}

	if (Playback && !BuildPlayback(*Playback, Options))
	{
		ReportError(FText::Format(LOCTEXT("Expander.PlaybackFailed", "the playback of curve {0} of {1} could not be built."),
			FCNACurveUtils::GetCurveDisplayName(*Playback), CNAExpanderLocal::GetAssetText(Asset)));
	}
}

bool FCNAExpander::BuildPlayback(const FCNABodyWidget& Widget, FBoundaryOptions& Options)
{
	const FCNACurveSettings& Curve = Widget.Curve;
	const float Length = FCNACurveUtils::GetPlaybackLength(Curve);
	const float PlayRate = FMath::Max(Curve.PlayRate, 0.001f);
	UClass* MathLibrary = UKismetMathLibrary::StaticClass();

	TArray<FCNASignaturePin> Signature;
	FCNAGraphUtils::GetSignature(Asset, Signature);
	const FName DrivenInput = FCNACurveUtils::GetPrimaryExecPinName(Signature, EGPD_Input);
	const FName DrivenOutput = FCNACurveUtils::GetPrimaryExecPinName(Signature, EGPD_Output);

	auto Link = [](UEdGraphPin* From, UEdGraphPin* To)
	{
		if (From && To)
		{
			From->MakeLinkTo(To);
		}
	};
	auto SpawnBranch = [this](const FPinSource& Condition) -> UK2Node_IfThenElse*
	{
		UK2Node_IfThenElse* Branch = Context.SpawnIntermediateNode<UK2Node_IfThenElse>(Node, SourceGraph);
		Branch->AllocateDefaultPins();
		ConnectPinSource(Condition, Branch->GetConditionPin());
		return Branch;
	};
	auto SpawnSequence = [this]() -> UK2Node_ExecutionSequence*
	{
		UK2Node_ExecutionSequence* Sequence = Context.SpawnIntermediateNode<UK2Node_ExecutionSequence>(Node, SourceGraph);
		Sequence->AllocateDefaultPins();
		return Sequence;
	};
	// Wires of a command pin of the instance (Play, Stop ...) go to the first node of its chain.
	auto MoveCurvePin = [this, &Widget](ECNACurvePinRole Role, UEdGraphPin* Target)
	{
		UEdGraphPin* Pin = Node->FindCurvePin(Widget.WidgetGuid, Role);
		if (Pin && Target)
		{
			Context.MovePinLinksToIntermediate(*Pin, *Target);
		}
	};

	// State of this instance between frames: position, playing, direction.
	UEdGraphPin* TimeVar = SpawnPersistentVariable(UEdGraphSchema_K2::PC_Float);
	UEdGraphPin* PlayingVar = SpawnPersistentVariable(UEdGraphSchema_K2::PC_Boolean);
	UEdGraphPin* ReverseVar = SpawnPersistentVariable(UEdGraphSchema_K2::PC_Boolean);
	UEdGraphPin* Self = GetSelfPin();
	if (!TimeVar || !PlayingVar || !ReverseVar || !Self)
	{
		return false;
	}
	CurveTimeSources.Add(Widget.WidgetGuid, FPinSource::FromPin(TimeVar));

	// The tick: Delay 0 resumes on the next frame (the latent action manager removes the finished action before it
	// runs the continuation, and processes an object once per frame). A Delay call while one waits is ignored
	// (same latent id), so Play on a playing node does not start a second loop.
	UK2Node_CallFunction* Delay = SpawnCall(UKismetSystemLibrary::StaticClass(), FName(TEXT("Delay")));
	if (!Delay)
	{
		return false;
	}
	Link(Self, Delay->FindPin(TEXT("WorldContextObject")));
	if (UEdGraphPin* Duration = Delay->FindPin(TEXT("Duration")))
	{
		Duration->DefaultValue = TEXT("0.0");
	}
	UEdGraphPin* DelayExec = Delay->GetExecPin();
	UEdGraphPin* DelayThen = Delay->GetThenPin();

	// Commands: Play and Reverse set the direction, then Playing, then arm the loop.
	const FAssignmentPins StartPlaying = SpawnAssignmentOf(PlayingVar, FPinSource::FromLiteral(TEXT("true")));
	const FAssignmentPins Forward = SpawnAssignmentOf(ReverseVar, FPinSource::FromLiteral(TEXT("false")));
	const FAssignmentPins StopPlaying = SpawnAssignmentOf(PlayingVar, FPinSource::FromLiteral(TEXT("false")));
	if (!StartPlaying.IsValid() || !Forward.IsValid() || !StopPlaying.IsValid() || !DelayExec || !DelayThen)
	{
		return false;
	}
	Link(StartPlaying.Then, DelayExec);
	Link(Forward.Then, StartPlaying.Exec);
	MoveCurvePin(ECNACurvePinRole::Play, Forward.Exec);
	MoveCurvePin(ECNACurvePinRole::Stop, StopPlaying.Exec);
	if (Node->FindCurvePin(Widget.WidgetGuid, ECNACurvePinRole::PlayFromStart))
	{
		const FAssignmentPins Rewind = SpawnAssignmentOf(TimeVar, FPinSource::FromFloat(0.0f));
		Link(Rewind.Then, Forward.Exec);
		MoveCurvePin(ECNACurvePinRole::PlayFromStart, Rewind.Exec);
	}
	if (Node->FindCurvePin(Widget.WidgetGuid, ECNACurvePinRole::Reverse))
	{
		const FAssignmentPins Backward = SpawnAssignmentOf(ReverseVar, FPinSource::FromLiteral(TEXT("true")));
		Link(Backward.Then, StartPlaying.Exec);
		MoveCurvePin(ECNACurvePinRole::Reverse, Backward.Exec);
	}

	// Every tick: stop when Playing was cleared, else NewTime = Time + WorldDeltaSeconds * (Reverse ? -Rate : Rate).
	UK2Node_IfThenElse* IfPlaying = SpawnBranch(FPinSource::FromPin(PlayingVar));
	Link(DelayThen, IfPlaying->GetExecPin());
	UEdGraphPin* DeltaSeconds = SpawnPureCall(UGameplayStatics::StaticClass(), FName(TEXT("GetWorldDeltaSeconds")),
		{ FCallArgument{ TEXT("WorldContextObject"), FPinSource::FromPin(Self) } });
	UEdGraphPin* SignedRate = SpawnPureCall(MathLibrary, FName(TEXT("SelectFloat")),
		{ FCallArgument{ TEXT("A"), FPinSource::FromFloat(-PlayRate) }, FCallArgument{ TEXT("B"), FPinSource::FromFloat(PlayRate) },
		  FCallArgument{ TEXT("bPickA"), FPinSource::FromPin(ReverseVar) } });
	UEdGraphPin* Step = SpawnPureCall(MathLibrary, FName(TEXT("Multiply_FloatFloat")),
		{ FCallArgument{ TEXT("A"), FPinSource::FromPin(DeltaSeconds) }, FCallArgument{ TEXT("B"), FPinSource::FromPin(SignedRate) } });
	UEdGraphPin* NewTime = SpawnPureCall(MathLibrary, FName(TEXT("Add_FloatFloat")),
		{ FCallArgument{ TEXT("A"), FPinSource::FromPin(TimeVar) }, FCallArgument{ TEXT("B"), FPinSource::FromPin(Step) } });
	if (!DeltaSeconds || !SignedRate || !Step || !NewTime)
	{
		return false;
	}

	// Running: re-arm the tick first (the subgraph may stop or restart the playback), then run the subgraph.
	UK2Node_ExecutionSequence* RunSequence = SpawnSequence();
	UK2Node_ExecutionSequence* RunBody = SpawnSequence();
	Link(RunSequence->GetThenPinGivenIndex(0), DelayExec);
	Link(RunSequence->GetThenPinGivenIndex(1), RunBody->GetExecPin());
	UEdGraphPin* FinishedSource = nullptr;

	if (Curve.bLoop)
	{
		// Time = ((NewTime % Length) + Length) % Length wraps both directions; a loop never finishes.
		UEdGraphPin* Rest = SpawnPureCall(MathLibrary, FName(TEXT("GenericPercent_FloatFloat")),
			{ FCallArgument{ TEXT("A"), FPinSource::FromPin(NewTime) }, FCallArgument{ TEXT("B"), FPinSource::FromFloat(Length) } });
		UEdGraphPin* Positive = SpawnPureCall(MathLibrary, FName(TEXT("Add_FloatFloat")),
			{ FCallArgument{ TEXT("A"), FPinSource::FromPin(Rest) }, FCallArgument{ TEXT("B"), FPinSource::FromFloat(Length) } });
		UEdGraphPin* Wrapped = SpawnPureCall(MathLibrary, FName(TEXT("GenericPercent_FloatFloat")),
			{ FCallArgument{ TEXT("A"), FPinSource::FromPin(Positive) }, FCallArgument{ TEXT("B"), FPinSource::FromFloat(Length) } });
		const FAssignmentPins SetWrapped = SpawnAssignmentOf(TimeVar, FPinSource::FromPin(Wrapped));
		if (!Wrapped || !SetWrapped.IsValid())
		{
			return false;
		}
		Link(IfPlaying->GetThenPin(), SetWrapped.Exec);
		Link(SetWrapped.Then, RunSequence->GetExecPin());
	}
	else
	{
		// The end in the current direction: NewTime <= 0 backwards, NewTime >= Length forwards.
		UEdGraphPin* PastStart = SpawnPureCall(MathLibrary, FName(TEXT("LessEqual_FloatFloat")),
			{ FCallArgument{ TEXT("A"), FPinSource::FromPin(NewTime) }, FCallArgument{ TEXT("B"), FPinSource::FromFloat(0.0f) } });
		UEdGraphPin* PastEnd = SpawnPureCall(MathLibrary, FName(TEXT("GreaterEqual_FloatFloat")),
			{ FCallArgument{ TEXT("A"), FPinSource::FromPin(NewTime) }, FCallArgument{ TEXT("B"), FPinSource::FromFloat(Length) } });
		UEdGraphPin* Clamped = SpawnPureCall(MathLibrary, FName(TEXT("FClamp")),
			{ FCallArgument{ TEXT("Value"), FPinSource::FromPin(NewTime) }, FCallArgument{ TEXT("Min"), FPinSource::FromFloat(0.0f) },
			  FCallArgument{ TEXT("Max"), FPinSource::FromFloat(Length) } });
		const FAssignmentPins SetTime = SpawnAssignmentOf(TimeVar, FPinSource::FromPin(NewTime));
		const FAssignmentPins SetEnd = SpawnAssignmentOf(TimeVar, FPinSource::FromPin(Clamped));
		const FAssignmentPins StopAtEnd = SpawnAssignmentOf(PlayingVar, FPinSource::FromLiteral(TEXT("false")));
		if (!PastStart || !PastEnd || !Clamped || !SetTime.IsValid() || !SetEnd.IsValid() || !StopAtEnd.IsValid())
		{
			return false;
		}
		UK2Node_IfThenElse* IfReverse = SpawnBranch(FPinSource::FromPin(ReverseVar));
		UK2Node_IfThenElse* IfPastStart = SpawnBranch(FPinSource::FromPin(PastStart));
		UK2Node_IfThenElse* IfPastEnd = SpawnBranch(FPinSource::FromPin(PastEnd));
		Link(IfPlaying->GetThenPin(), IfReverse->GetExecPin());
		Link(IfReverse->GetThenPin(), IfPastStart->GetExecPin());
		Link(IfReverse->GetElsePin(), IfPastEnd->GetExecPin());

		// Still inside: store the position and run.
		Link(IfPastStart->GetElsePin(), SetTime.Exec);
		Link(IfPastEnd->GetElsePin(), SetTime.Exec);
		Link(SetTime.Then, RunSequence->GetExecPin());

		// The end: clamp, stop, run the subgraph a last time (Update), then Finished, like a Timeline.
		Link(IfPastStart->GetThenPin(), SetEnd.Exec);
		Link(IfPastEnd->GetThenPin(), SetEnd.Exec);
		Link(SetEnd.Then, StopAtEnd.Exec);
		UK2Node_ExecutionSequence* EndSequence = SpawnSequence();
		Link(StopAtEnd.Then, EndSequence->GetExecPin());
		Link(EndSequence->GetThenPinGivenIndex(0), RunBody->GetExecPin());
		FinishedSource = EndSequence->GetThenPinGivenIndex(1);
	}

	// The subgraph runs from its first exec input; its first exec output leads to Update. Without one of them,
	// Update fires right after the subgraph.
	UEdGraphPin* BodyTrigger = RunBody->GetThenPinGivenIndex(0);
	UEdGraphPin* UpdatePin = Node->FindCurvePin(Widget.WidgetGuid, ECNACurvePinRole::Update);
	const TArray<UEdGraphPin*> UpdateConsumers = UpdatePin ? UpdatePin->LinkedTo : TArray<UEdGraphPin*>();
	if (!DrivenInput.IsNone() && BodyTrigger)
	{
		FInputSource& Source = Options.InputOverrides.FindOrAdd(DrivenInput);
		Source.Sources.Reset();
		Source.Sources.Add(BodyTrigger);
		Source.DefaultFrom = nullptr;
	}
	if (!DrivenOutput.IsNone())
	{
		Options.OutputOverrides.FindOrAdd(DrivenOutput).Append(UpdateConsumers);
	}
	if (DrivenInput.IsNone() || DrivenOutput.IsNone())
	{
		for (UEdGraphPin* Consumer : UpdateConsumers)
		{
			Link(RunBody->GetThenPinGivenIndex(1), Consumer);
		}
	}
	if (UpdatePin)
	{
		UpdatePin->BreakAllPinLinks();
	}

	UEdGraphPin* FinishedPin = Node->FindCurvePin(Widget.WidgetGuid, ECNACurvePinRole::Finished);
	if (FinishedPin && FinishedSource)
	{
		Context.MovePinLinksToIntermediate(*FinishedPin, *FinishedSource);
	}

	++CNAExpanderLocal::Stats.PlaybackLoops;
	return true;
}

void FCNAExpander::ReplaceCurveValueNodes()
{
	for (UK2Node_CNACurveValue* CurveNode : CurveValueNodes)
	{
		UEdGraphPin* ValuePin = CurveNode->GetValuePin();
		UEdGraphPin* TimePin = CurveNode->GetTimePin();
		const TArray<UEdGraphPin*> ValueConsumers = ValuePin ? ValuePin->LinkedTo : TArray<UEdGraphPin*>();
		const TArray<UEdGraphPin*> TimeConsumers = TimePin ? TimePin->LinkedTo : TArray<UEdGraphPin*>();
		const FEdGraphPinType NodeValueType = ValuePin ? ValuePin->PinType : FEdGraphPinType();
		// The clone has no asset of its own: the curve is looked up in the asset of the instance.
		const FCNABodyWidget* Widget = CurveNode->FindCurveIn(Asset);
		BreakLinksQuietly(CurveNode);
		++CNAExpanderLocal::Stats.CurveValueNodes;

		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Node"), CurveNode->GetNodeTitle(ENodeTitleType::ListView));
		Arguments.Add(TEXT("Curve"), CurveNode->CurveName.IsNone() ? LOCTEXT("Expander.NoCurve", "<none>") : FText::FromName(CurveNode->CurveName));
		if (!Widget)
		{
			ReportCode(ECNAMessage::CNA023, Arguments);
			continue;
		}

		if (TimeConsumers.Num() > 0)
		{
			const FPinSource* Time = CurveTimeSources.Find(Widget->WidgetGuid);
			const FPinSource TimeSource = Time ? *Time : FPinSource::FromFloat(0.0f);
			for (UEdGraphPin* Consumer : TimeConsumers)
			{
				ConnectPinSource(TimeSource, Consumer);
			}
		}
		if (ValueConsumers.Num() == 0)
		{
			continue;
		}
		const FEdGraphPinType Expected = FCNACurveUtils::GetValuePinType(Widget->Curve.ValueType);
		if (!FCNAPinUtils::AreTypesIdentical(NodeValueType, Expected))
		{
			Arguments.Add(TEXT("Type"), FCNAPinUtils::GetTypeText(Expected));
			const FText Details = FText::Format(LOCTEXT("Expander.CurveValueOutdated", "{Node} is outdated: the curve {Curve} of {Asset} now gives {Type}. Use Refresh Node on it."), Arguments);
			FCNAMessages::ReportTextToCompiler(Context.MessageLog, Node, ECNAMessage::CNA023,
				FText::Format(NSLOCTEXT("NodeFactory", "Msg.WithCode", "{0}: {1}"), FText::FromString(FCNAMessages::GetCode(ECNAMessage::CNA023)), Details));
			continue;
		}
		if (const FCurveEvaluation* Evaluation = GetCurveEvaluation(*Widget))
		{
			for (UEdGraphPin* Consumer : ValueConsumers)
			{
				ConnectPinSource(Evaluation->Value, Consumer);
			}
		}
	}
	CurveValueNodes.Reset();
}

const FCNAExpander::FCurveEvaluation* FCNAExpander::GetCurveEvaluation(const FCNABodyWidget& Widget)
{
	if (const FCurveEvaluation* Found = CurveEvaluations.Find(Widget.WidgetGuid))
	{
		return Found;
	}

	FCurveEvaluation Evaluation;
	const FPinSource* Time = CurveTimeSources.Find(Widget.WidgetGuid);
	Evaluation.Time = Time ? *Time : FPinSource::FromFloat(0.0f);
	const bool bBuilt = Widget.Curve.bUseCurveAsset
		? BuildAssetEvaluation(Widget.Curve, Evaluation.Time, Evaluation.Value)
		: BuildBakedEvaluation(Widget.Curve, Evaluation.Time, Evaluation.Value);
	if (!bBuilt)
	{
		ReportError(FText::Format(LOCTEXT("Expander.CurveFailed", "the value of curve {0} of {1} could not be built."),
			FCNACurveUtils::GetCurveDisplayName(Widget), CNAExpanderLocal::GetAssetText(Asset)));
		return nullptr;
	}
	++CNAExpanderLocal::Stats.CurveEvaluations;
	// The evaluation is used right away; a later Add may move the elements of the map.
	return &CurveEvaluations.Add(Widget.WidgetGuid, Evaluation);
}

bool FCNAExpander::BuildAssetEvaluation(const FCNACurveSettings& Curve, const FPinSource& Time, FPinSource& OutValue)
{
	UCurveBase* CurveAsset = FCNACurveUtils::LoadCurveAsset(Curve);
	if (!CurveAsset)
	{
		// The validator reports the reason (CNA022) before the expansion.
		return false;
	}
	UK2Node_CallFunction* Call = SpawnCall(FCNACurveUtils::GetCurveAssetClass(Curve.ValueType), FCNACurveUtils::GetCurveAssetGetterName(Curve.ValueType));
	if (!Call)
	{
		return false;
	}
	// V-check: a const BlueprintCallable member is pure in 4.20.3; an impure call would need an exec wire here.
	if (!Call->IsNodePure())
	{
		ReportError(FText::Format(LOCTEXT("Expander.CurveGetterImpure", "{0} is not pure in this engine version; the Curve asset cannot be read here."),
			FText::FromName(FCNACurveUtils::GetCurveAssetGetterName(Curve.ValueType))));
		return false;
	}
	UEdGraphPin* TargetPin = Call->FindPin(UEdGraphSchema_K2::PN_Self, EGPD_Input);
	UEdGraphPin* TimePin = Call->FindPin(TEXT("InTime"), EGPD_Input);
	UEdGraphPin* ResultPin = Call->GetReturnValuePin();
	if (!TargetPin || !TimePin || !ResultPin)
	{
		ReportError(LOCTEXT("Expander.CurveGetterPins", "the getter of the Curve asset has an unexpected set of pins (V-check: Target, InTime, ReturnValue)."));
		return false;
	}
	// The asset is the target as an object literal: the compiled class references it, so it is cooked with the game.
	TargetPin->DefaultObject = CurveAsset;
	ConnectPinSource(Time, TimePin);
	OutValue = FPinSource::FromPin(ResultPin);
	++CNAExpanderLocal::Stats.CurveAssetCalls;
	return true;
}

bool FCNAExpander::BuildBakedEvaluation(const FCNACurveSettings& Curve, const FPinSource& Time, FPinSource& OutValue)
{
	UClass* MathLibrary = UKismetMathLibrary::StaticClass();
	const int32 NumChannels = Curve.GetChannelCount();
	const int32 NumSamples = FMath::Clamp(Curve.BakeSamples, 2, 1024);
	float MinTime = 0.0f;
	float MaxTime = 0.0f;
	Curve.GetOwnTimeRange(MinTime, MaxTime);
	const float Span = MaxTime - MinTime;

	TArray<FPinSource> Channels;
	if (!Time.Pin || Span <= KINDA_SMALL_NUMBER)
	{
		// A literal time or a flat curve: the value is known now.
		const float LiteralTime = Time.Pin ? MinTime : FCString::Atof(*Time.Literal);
		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			Channels.Add(FPinSource::FromFloat(FCNACurveUtils::EvaluateChannel(Curve, Channel, LiteralTime)));
		}
	}
	else
	{
		// Position among the samples: U = clamp((Time - Min) * (N - 1) / Span, 0, N - 1), the samples I0 = min(floor(U), N - 2)
		// and I0 + 1, and Alpha = U - I0 between them. Shared by all channels.
		UEdGraphPin* Offset = SpawnPureCall(MathLibrary, FName(TEXT("Subtract_FloatFloat")),
			{ FCallArgument{ TEXT("A"), Time }, FCallArgument{ TEXT("B"), FPinSource::FromFloat(MinTime) } });
		UEdGraphPin* Scaled = SpawnPureCall(MathLibrary, FName(TEXT("Multiply_FloatFloat")),
			{ FCallArgument{ TEXT("A"), FPinSource::FromPin(Offset) }, FCallArgument{ TEXT("B"), FPinSource::FromFloat(static_cast<float>(NumSamples - 1) / Span) } });
		UEdGraphPin* Position = SpawnPureCall(MathLibrary, FName(TEXT("FClamp")),
			{ FCallArgument{ TEXT("Value"), FPinSource::FromPin(Scaled) }, FCallArgument{ TEXT("Min"), FPinSource::FromFloat(0.0f) },
			  FCallArgument{ TEXT("Max"), FPinSource::FromFloat(static_cast<float>(NumSamples - 1)) } });
		UEdGraphPin* Floor = SpawnPureCall(MathLibrary, FName(TEXT("FFloor")),
			{ FCallArgument{ TEXT("A"), FPinSource::FromPin(Position) } });
		UEdGraphPin* Lower = SpawnPureCall(MathLibrary, FName(TEXT("Clamp")),
			{ FCallArgument{ TEXT("Value"), FPinSource::FromPin(Floor) }, FCallArgument{ TEXT("Min"), FPinSource::FromInt(0) },
			  FCallArgument{ TEXT("Max"), FPinSource::FromInt(NumSamples - 2) } });
		UEdGraphPin* Upper = SpawnPureCall(MathLibrary, FName(TEXT("Add_IntInt")),
			{ FCallArgument{ TEXT("A"), FPinSource::FromPin(Lower) }, FCallArgument{ TEXT("B"), FPinSource::FromInt(1) } });
		UEdGraphPin* LowerAsFloat = SpawnPureCall(MathLibrary, FName(TEXT("Conv_IntToFloat")),
			{ FCallArgument{ TEXT("InInt"), FPinSource::FromPin(Lower) } });
		UEdGraphPin* Alpha = SpawnPureCall(MathLibrary, FName(TEXT("Subtract_FloatFloat")),
			{ FCallArgument{ TEXT("A"), FPinSource::FromPin(Position) }, FCallArgument{ TEXT("B"), FPinSource::FromPin(LowerAsFloat) } });
		if (!Offset || !Scaled || !Position || !Floor || !Lower || !Upper || !LowerAsFloat || !Alpha)
		{
			return false;
		}

		FEdGraphPinType FloatType;
		FloatType.PinCategory = UEdGraphSchema_K2::PC_Float;
		TArray<float> Samples;
		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			FCNACurveUtils::BakeChannel(Curve, Channel, MinTime, MaxTime, NumSamples, Samples);
			const bool bConstant = !Samples.ContainsByPredicate([&Samples](float Sample) { return !FMath::IsNearlyEqual(Sample, Samples[0]); });
			if (bConstant)
			{
				Channels.Add(FPinSource::FromFloat(Samples[0]));
				continue;
			}
			// Value = Lerp(Samples[I0], Samples[I0 + 1], Alpha), the samples as a Make Array of literals.
			UEdGraphPin* SampleArray = SpawnFloatArray(Samples);
			UEdGraphPin* LowerArray = nullptr;
			UEdGraphPin* LowerIndex = nullptr;
			UEdGraphPin* UpperArray = nullptr;
			UEdGraphPin* UpperIndex = nullptr;
			UEdGraphPin* LowerItem = SpawnArrayGet(FloatType, LowerArray, LowerIndex);
			UEdGraphPin* UpperItem = SpawnArrayGet(FloatType, UpperArray, UpperIndex);
			if (!SampleArray || !LowerItem || !UpperItem)
			{
				return false;
			}
			SampleArray->MakeLinkTo(LowerArray);
			SampleArray->MakeLinkTo(UpperArray);
			Lower->MakeLinkTo(LowerIndex);
			Upper->MakeLinkTo(UpperIndex);
			UEdGraphPin* Value = SpawnPureCall(MathLibrary, FName(TEXT("Lerp")),
				{ FCallArgument{ TEXT("A"), FPinSource::FromPin(LowerItem) }, FCallArgument{ TEXT("B"), FPinSource::FromPin(UpperItem) },
				  FCallArgument{ TEXT("Alpha"), FPinSource::FromPin(Alpha) } });
			if (!Value)
			{
				return false;
			}
			Channels.Add(FPinSource::FromPin(Value));
			++CNAExpanderLocal::Stats.BakedChannels;
		}
	}

	if (Channels.Num() != NumChannels)
	{
		return false;
	}
	switch (Curve.ValueType)
	{
	case ECNACurveValueType::Vector:
		OutValue = FPinSource::FromPin(SpawnPureCall(MathLibrary, FName(TEXT("MakeVector")),
			{ FCallArgument{ TEXT("X"), Channels[0] }, FCallArgument{ TEXT("Y"), Channels[1] }, FCallArgument{ TEXT("Z"), Channels[2] } }));
		return OutValue.Pin != nullptr;
	case ECNACurveValueType::LinearColor:
		// V-check: UKismetMathLibrary::MakeColor(float R, float G, float B, float A) returns FLinearColor in 4.20.3.
		OutValue = FPinSource::FromPin(SpawnPureCall(MathLibrary, FName(TEXT("MakeColor")),
			{ FCallArgument{ TEXT("R"), Channels[0] }, FCallArgument{ TEXT("G"), Channels[1] }, FCallArgument{ TEXT("B"), Channels[2] },
			  FCallArgument{ TEXT("A"), Channels[3] } }));
		return OutValue.Pin != nullptr;
	case ECNACurveValueType::Float:
	default:
		OutValue = Channels[0];
		return true;
	}
}

// Engine nodes spawned by the expansion

UK2Node_CallFunction* FCNAExpander::SpawnCall(UClass* Class, FName FunctionName)
{
	UFunction* Function = Class ? Class->FindFunctionByName(FunctionName) : nullptr;
	if (!Function)
	{
		ReportError(FText::Format(LOCTEXT("Expander.FunctionMissing", "the engine function {0}::{1} was not found (V-check of UE 4.20.3)."),
			FText::FromString(GetNameSafe(Class)), FText::FromName(FunctionName)));
		return nullptr;
	}
	UK2Node_CallFunction* Call = Context.SpawnIntermediateNode<UK2Node_CallFunction>(Node, SourceGraph);
	Call->SetFromFunction(Function);
	Call->AllocateDefaultPins();
	return Call;
}

UEdGraphPin* FCNAExpander::SpawnPureCall(UClass* Class, FName FunctionName, const TArray<FCallArgument>& Arguments)
{
	UK2Node_CallFunction* Call = SpawnCall(Class, FunctionName);
	if (!Call)
	{
		return nullptr;
	}
	for (const FCallArgument& Argument : Arguments)
	{
		UEdGraphPin* Pin = Call->FindPin(Argument.Name, EGPD_Input);
		if (!Pin)
		{
			ReportError(FText::Format(LOCTEXT("Expander.CallPinMissing", "the pin {0} of the engine function {1} was not found (V-check of UE 4.20.3)."),
				FText::FromName(Argument.Name), FText::FromName(FunctionName)));
			return nullptr;
		}
		ConnectPinSource(Argument.Source, Pin);
	}
	return Call->GetReturnValuePin();
}

UEdGraphPin* FCNAExpander::SpawnArrayGet(const FEdGraphPinType& ElementType, UEdGraphPin*& OutArrayPin, UEdGraphPin*& OutIndexPin)
{
	OutArrayPin = nullptr;
	OutIndexPin = nullptr;
	// V-check: UKismetArrayLibrary::Array_Get(const TArray<int32>& TargetArray, int32 Index, int32& Item), pure and
	// called through UK2Node_CallArrayFunction like the Get node of the editor.
	UFunction* Function = UKismetArrayLibrary::StaticClass()->FindFunctionByName(FName(TEXT("Array_Get")));
	if (!Function)
	{
		ReportError(LOCTEXT("Expander.ArrayGetMissing", "Array Get could not be created (V-check: UKismetArrayLibrary::Array_Get)."));
		return nullptr;
	}
	UK2Node_CallArrayFunction* Get = Context.SpawnIntermediateNode<UK2Node_CallArrayFunction>(Node, SourceGraph);
	Get->SetFromFunction(Function);
	Get->AllocateDefaultPins();
	OutArrayPin = Get->FindPin(TEXT("TargetArray"), EGPD_Input);
	OutIndexPin = Get->FindPin(TEXT("Index"), EGPD_Input);
	UEdGraphPin* ItemPin = Get->FindPin(TEXT("Item"), EGPD_Output);
	if (!OutArrayPin || !OutIndexPin || !ItemPin)
	{
		ReportError(LOCTEXT("Expander.ArrayGetPins", "Array Get has an unexpected set of pins (V-check: TargetArray, Index, Item)."));
		return nullptr;
	}
	// The Wildcard pins take the element type; MakeLinkTo does not propagate types. Container and reference flags
	// of the function parameters stay.
	for (UEdGraphPin* Pin : { OutArrayPin, ItemPin })
	{
		Pin->PinType.PinCategory = ElementType.PinCategory;
		Pin->PinType.PinSubCategory = ElementType.PinSubCategory;
		Pin->PinType.PinSubCategoryObject = ElementType.PinSubCategoryObject;
	}
	++CNAExpanderLocal::Stats.ArrayGetNodes;
	return ItemPin;
}

UEdGraphPin* FCNAExpander::SpawnFloatArray(const TArray<float>& Values)
{
	// V-check: UK2Node_MakeArray keeps its pin count in the property NumInputs (UK2Node_MakeContainer in 4.20).
	UIntProperty* NumInputsProperty = FindField<UIntProperty>(UK2Node_MakeArray::StaticClass(), TEXT("NumInputs"));
	if (!NumInputsProperty || Values.Num() == 0)
	{
		ReportError(LOCTEXT("Expander.SampleArrayFailed", "the samples of a curve could not be stored (V-check: UK2Node_MakeArray::NumInputs)."));
		return nullptr;
	}
	UK2Node_MakeArray* MakeArray = Context.SpawnIntermediateNode<UK2Node_MakeArray>(Node, SourceGraph);
	NumInputsProperty->SetPropertyValue_InContainer(MakeArray, Values.Num());
	MakeArray->AllocateDefaultPins();

	FEdGraphPinType FloatType;
	FloatType.PinCategory = UEdGraphSchema_K2::PC_Float;
	UEdGraphPin* ArrayPin = nullptr;
	int32 Index = 0;
	for (UEdGraphPin* Pin : MakeArray->Pins)
	{
		if (!Pin)
		{
			continue;
		}
		if (Pin->Direction == EGPD_Output)
		{
			ArrayPin = Pin;
			ArrayPin->PinType = FloatType;
			ArrayPin->PinType.ContainerType = EPinContainerType::Array;
		}
		else if (Values.IsValidIndex(Index))
		{
			Pin->PinType = FloatType;
			Pin->DefaultValue = FString::SanitizeFloat(Values[Index++]);
		}
	}
	if (!ArrayPin || Index != Values.Num())
	{
		ReportError(LOCTEXT("Expander.SampleArrayPins", "Make Array of the curve samples has an unexpected set of pins."));
		return nullptr;
	}
	++CNAExpanderLocal::Stats.MakeArrayNodes;
	CNAExpanderLocal::Stats.MakeArrayElements += Values.Num();
	return ArrayPin;
}

UEdGraphPin* FCNAExpander::SpawnPersistentVariable(FName Category)
{
	// V-check: FKismetCompilerContext::SpawnInternalVariable(UEdGraphNode*, FName Category, FName SubCategory, ...)
	// exists since 4.18 (Do Once Multi Input keeps its state with it). It spawns a UK2Node_TemporaryVariable in the
	// graph of the node; bIsPersistent makes it a member of the ubergraph frame, so the value survives between frames
	// (Event Graph only, CNA020).
	UK2Node_TemporaryVariable* Variable = Context.SpawnInternalVariable(Node, Category);
	if (!Variable || Variable->GetGraph() != SourceGraph)
	{
		ReportError(LOCTEXT("Expander.PersistentFailed", "the state of the curve playback could not be created."));
		return nullptr;
	}
	Variable->bIsPersistent = true;
	if (Variable->Pins.Num() == 0)
	{
		Variable->AllocateDefaultPins();
	}
	++CNAExpanderLocal::Stats.TemporaryVariables;
	return FindFirstPin(Variable, EGPD_Output);
}

FCNAExpander::FAssignmentPins FCNAExpander::SpawnAssignmentOf(UEdGraphPin* VariablePin, const FPinSource& Value)
{
	const FAssignmentPins Assignment = SpawnAssignment(VariablePin, VariablePin ? VariablePin->PinType : FEdGraphPinType());
	if (Assignment.IsValid())
	{
		ConnectPinSource(Value, Assignment.Value);
	}
	return Assignment;
}

// Nested nodes and cleanup (steps 10 and 11)

void FCNAExpander::ExpandNestedNodes()
{
	for (UK2Node_CustomNode* Nested : NestedNodes)
	{
		if (!Nested || Nested->bExpanded)
		{
			continue;
		}
		// V3: the nested node is expanded right away with the stack of assets around it, so cycles stop with
		// CNA001 and the depth limit applies (spec 5.2).
		Nested->ExpansionStack = Node->ExpansionStack;
		Nested->ExpansionStack.Add(Asset);
		++CNAExpanderLocal::Stats.NestedInstances;
		Expand(Nested, Context, SourceGraph);
		ReplacedNodes.Add(Nested);
	}
	NestedNodes.Reset();
}

void FCNAExpander::RemoveReplacedNodes()
{
	using namespace CNAExpanderLocal;
	const bool bCanRemove = SourceGraph == RootGraph && RootIndex != INDEX_NONE;
	for (UEdGraphNode* Replaced : ReplacedNodes)
	{
		if (!Replaced)
		{
			continue;
		}
		BreakLinksQuietly(Replaced);
		if (bCanRemove)
		{
			const int32 Index = SourceGraph->Nodes.IndexOfByKey(Replaced);
			if (Index > RootIndex)
			{
				// Appended by this expansion and not visited by the compiler yet: safe to take out again.
				SourceGraph->Nodes.RemoveAt(Index);
			}
		}
	}
	ReplacedNodes.Reset();
}

// Helpers

void FCNAExpander::BreakLinksQuietly(UEdGraphNode* GraphNode)
{
	if (!GraphNode)
	{
		return;
	}
	// Without notifications: a compiled copy must not react to the rewiring (no type propagation, no Behavior).
	for (UEdGraphPin* Pin : GraphNode->Pins)
	{
		if (Pin)
		{
			Pin->BreakAllPinLinks();
		}
	}
}

UEdGraphPin* FCNAExpander::FindFirstPin(const UEdGraphNode* GraphNode, EEdGraphPinDirection Direction)
{
	if (!GraphNode)
	{
		return nullptr;
	}
	for (UEdGraphPin* Pin : GraphNode->Pins)
	{
		if (Pin && !Pin->ParentPin && Pin->Direction == Direction)
		{
			return Pin;
		}
	}
	return nullptr;
}

bool FCNAExpander::IsBoundaryTunnel(const UEdGraphNode* GraphNode)
{
	// Exactly UK2Node_Tunnel: the Inputs and Outputs nodes of a macro or a collapsed graph. Macro instances and
	// collapsed graph nodes derive from it but are inlined instead.
	return GraphNode && GraphNode->GetClass() == UK2Node_Tunnel::StaticClass();
}

void FCNAExpander::ReportError(const FText& Message) const
{
	Context.MessageLog.Error(*(FString(TEXT("@@: ")) + Message.ToString()), Node);
}

void FCNAExpander::ReportCode(ECNAMessage Code, const FFormatNamedArguments& Arguments) const
{
	FCNAMessages::ReportToCompiler(Context.MessageLog, Node, Code, Arguments);
}

#undef LOCTEXT_NAMESPACE
