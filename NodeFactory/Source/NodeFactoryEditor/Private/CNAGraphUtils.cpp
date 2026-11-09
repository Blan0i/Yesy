// Copyright (c) NodeFactory. Graph helpers (spec 4.1, 5.2, 10.2).

#include "CNAGraphUtils.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "K2Node_GetCaller.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Tunnel.h"
#include "Internationalization/Text.h"
#include "Misc/Crc.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNAGraphUtilsLocal
{
	/** Source string of a text: stable across editor languages. */
	FString GetSourceString(const FText& Text)
	{
		const FString* Source = FTextInspector::GetSourceString(Text);
		return Source ? *Source : Text.ToString();
	}
	bool IsExactTunnel(const UEdGraphNode* Node)
	{
		return Node && Node->GetClass() == UK2Node_Tunnel::StaticClass();
	}
	/** Default value of a macro input as entered in the Details of the Tunnel node. */
	void ReadTunnelDefault(const UK2Node_Tunnel* Tunnel, const UEdGraphPin* Pin, FCNASignaturePin& OutPin)
	{
		OutPin.DefaultValue = Pin->DefaultValue;
		OutPin.DefaultObject = Pin->DefaultObject;
		OutPin.DefaultText = Pin->DefaultTextValue;

		// The pin wins: the user pin info is only read when the pin itself carries no value.
		if (!Pin->DefaultValue.IsEmpty() || Pin->DefaultObject || !Pin->DefaultTextValue.IsEmpty())
		{
			return;
		}
		for (const TSharedPtr<FUserPinInfo>& Info : Tunnel->UserDefinedPins)
		{
			if (Info.IsValid() && Info->PinName == Pin->PinName && !Info->PinDefaultValue.IsEmpty())
			{
				OutPin.DefaultValue = Info->PinDefaultValue;
				break;
			}
		}
	}
	bool FindLatentInGraph(const UEdGraph* Graph, TArray<FString>* OutNames, TSet<const UObject*>& Visited)
	{
		if (!Graph || Visited.Contains(Graph))
		{
			return false;
		}
		Visited.Add(Graph);

		bool bFound = false;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			bool bNodeIsLatent = false;
			if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				bNodeIsLatent = Call->IsLatentFunction();
			}
			else if (Cast<UK2Node_BaseAsyncTask>(Node))
			{
				bNodeIsLatent = true;
			}
			else if (const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
			{
				bNodeIsLatent = FindLatentInGraph(Macro->GetMacroGraph(), nullptr, Visited);
			}
			else if (const UK2Node_CustomNode* Custom = Cast<UK2Node_CustomNode>(Node))
			{
				const UCustomNodeAsset* Nested = Custom->GetCustomNodeAsset();
				if (Nested && !Visited.Contains(Nested))
				{
					Visited.Add(Nested);
					TArray<UEdGraph*> NestedGraphs;
					FCNAGraphUtils::CollectExecuteGraphs(Nested, NestedGraphs);
					for (UEdGraph* NestedGraph : NestedGraphs)
					{
						bNodeIsLatent |= FindLatentInGraph(NestedGraph, nullptr, Visited);
					}
				}
				else if (Nested)
				{
					bNodeIsLatent = Nested->bContainsLatent;
				}
			}
			if (bNodeIsLatent)
			{
				bFound = true;
				if (OutNames)
				{
					OutNames->AddUnique(FCNAGraphUtils::GetNodeName(Node));
				}
				else
				{
					return true;
				}
			}
		}
		for (const UEdGraph* SubGraph : Graph->SubGraphs)
		{
			if (FindLatentInGraph(SubGraph, OutNames, Visited))
			{
				bFound = true;
				if (!OutNames)
				{
					return true;
				}
			}
		}
		return bFound;
	}
	bool FindCycleRecursive(const UCustomNodeAsset* Asset, TArray<const UCustomNodeAsset*>& Stack, TArray<FString>& OutChain)
	{
		const int32 StackIndex = Stack.IndexOfByKey(Asset);
		if (StackIndex != INDEX_NONE)
		{
			OutChain.Reset();
			for (int32 Index = StackIndex; Index < Stack.Num(); ++Index)
			{
				OutChain.Add(Stack[Index]->GetName());
			}
			OutChain.Add(Asset->GetName());
			return true;
		}
		Stack.Push(Asset);
		TArray<UK2Node_CustomNode*> Nested;
		FCNAGraphUtils::GetNestedCustomNodes(Asset, Nested);
		TSet<const UCustomNodeAsset*> Seen;
		for (const UK2Node_CustomNode* Node : Nested)
		{
			const UCustomNodeAsset* Child = Node ? Node->GetCustomNodeAsset() : nullptr;
			if (Child && !Seen.Contains(Child))
			{
				Seen.Add(Child);
				if (FindCycleRecursive(Child, Stack, OutChain))
				{
					return true;
				}
			}
		}
		Stack.Pop();
		return false;
	}
	int32 DepthRecursive(const UCustomNodeAsset* Asset, TArray<const UCustomNodeAsset*>& Stack, int32 Limit)
	{
		if (Stack.Contains(Asset) || Stack.Num() > Limit)
		{
			// Cycles are reported as CNA001; the depth search only has to terminate.
			return Stack.Num();
		}
		Stack.Push(Asset);
		int32 MaxDepth = Stack.Num();
		TArray<UK2Node_CustomNode*> Nested;
		FCNAGraphUtils::GetNestedCustomNodes(Asset, Nested);
		TSet<const UCustomNodeAsset*> Seen;
		for (const UK2Node_CustomNode* Node : Nested)
		{
			const UCustomNodeAsset* Child = Node ? Node->GetCustomNodeAsset() : nullptr;
			if (Child && !Seen.Contains(Child))
			{
				Seen.Add(Child);
				MaxDepth = FMath::Max(MaxDepth, DepthRecursive(Child, Stack, Limit));
			}
		}
		Stack.Pop();
		return MaxDepth;
	}
}
// Execute macro and its signature

UK2Node_Tunnel* FCNAGraphUtils::FindEntryTunnel(const UEdGraph* Graph)
{
	if (!Graph)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (CNAGraphUtilsLocal::IsExactTunnel(Node))
		{
			UK2Node_Tunnel* Tunnel = CastChecked<UK2Node_Tunnel>(Node);
			if (Tunnel->bCanHaveOutputs && !Tunnel->bCanHaveInputs)
			{
				return Tunnel;
			}
		}
	}
	return nullptr;
}
UK2Node_Tunnel* FCNAGraphUtils::FindExitTunnel(const UEdGraph* Graph)
{
	if (!Graph)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (CNAGraphUtilsLocal::IsExactTunnel(Node))
		{
			UK2Node_Tunnel* Tunnel = CastChecked<UK2Node_Tunnel>(Node);
			if (Tunnel->bCanHaveInputs && !Tunnel->bCanHaveOutputs)
			{
				return Tunnel;
			}
		}
	}
	return nullptr;
}
bool FCNAGraphUtils::HasValidExecuteGraph(const UCustomNodeAsset* Asset)
{
	const UEdGraph* Execute = Asset ? Asset->FindExecuteGraph() : nullptr;
	return Execute && FindEntryTunnel(Execute) && FindExitTunnel(Execute);
}
bool FCNAGraphUtils::GetSignature(const UCustomNodeAsset* Asset, TArray<FCNASignaturePin>& OutPins)
{
	OutPins.Reset();

	const UEdGraph* Execute = Asset ? Asset->FindExecuteGraph() : nullptr;
	const UK2Node_Tunnel* Entry = FindEntryTunnel(Execute);
	const UK2Node_Tunnel* Exit = FindExitTunnel(Execute);
	if (!Entry || !Exit)
	{
		return false;
	}
	int32 InputIndex = 0;
	for (const UEdGraphPin* Pin : Entry->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->ParentPin == nullptr)
		{
			FCNASignaturePin& Out = OutPins[OutPins.AddDefaulted()];
			Out.Name = Pin->PinName;
			Out.Type = Pin->PinType;
			Out.Direction = EGPD_Input;
			Out.Index = InputIndex++;
			CNAGraphUtilsLocal::ReadTunnelDefault(Entry, Pin, Out);
		}
	}
	int32 OutputIndex = 0;
	for (const UEdGraphPin* Pin : Exit->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->ParentPin == nullptr)
		{
			FCNASignaturePin& Out = OutPins[OutPins.AddDefaulted()];
			Out.Name = Pin->PinName;
			Out.Type = Pin->PinType;
			Out.Direction = EGPD_Output;
			Out.Index = OutputIndex++;
		}
	}
	return true;
}
const FCNASignaturePin* FCNAGraphUtils::FindSignaturePin(const TArray<FCNASignaturePin>& Pins, FName Name, EEdGraphPinDirection Direction)
{
	return Pins.FindByPredicate([Name, Direction](const FCNASignaturePin& Pin)
	{
		return Pin.Name == Name && (Direction == EGPD_MAX || Pin.Direction == Direction);
	});
}
uint32 FCNAGraphUtils::ComputeSignatureHash(const UCustomNodeAsset* Asset)
{
	if (!Asset)
	{
		return 0;
	}
	FString Text;
	TArray<FCNASignaturePin> Pins;
	if (!GetSignature(Asset, Pins))
	{
		Text += TEXT("NoExecute;");
	}
	for (const FCNASignaturePin& Pin : Pins)
	{
		Text += FString::Printf(TEXT("%d:%s:%s;"), Pin.IsInput() ? 0 : 1, *Pin.Name.ToString(), *FCNAPinUtils::GetTypeSignatureString(Pin.Type));
	}
	// Settings that change which pins the node creates or what is stored on them when they are created
	// (spec 4.2, 4.4): a difference means the instances must be rebuilt. Colors and connection rules are read
	// live by the widgets and the schema and are not part of the hash. Texts use their source string, so a
	// change of the editor language does not change the hash.
	for (const FCNAPinOverride& Override : Asset->PinOverrides)
	{
		Text += FString::Printf(TEXT("Ovr:%s:%s:%s:%d%d%d:%s;"),
			*Override.PinName.ToString(),
			*CNAGraphUtilsLocal::GetSourceString(Override.DisplayName),
			*CNAGraphUtilsLocal::GetSourceString(Override.Tooltip),
			Override.bAdvanced ? 1 : 0,
			Override.bHiddenByDefault ? 1 : 0,
			Override.bDefaultToSelf ? 1 : 0,
			*Override.PassThroughOf.ToString());
		if (Override.bDynamicGroup)
		{
			Text += FString::Printf(TEXT("Dyn:%s:%d:%d:%s;"), *Override.PinName.ToString(), Override.DynamicMin, Override.DynamicMax,
				*CNAGraphUtilsLocal::GetSourceString(Override.DynamicNameFormat));
		}
	}
	Text += FString::Printf(TEXT("Pure:%d;Dev:%d;"), Asset->bPure ? 1 : 0, Asset->bDevelopmentOnly ? 1 : 0);
	// Pins of the curve widgets; empty without curves, so the hash of older assets does not change.
	Text += FCNACurveUtils::GetPinSignatureText(Asset);

	return FCrc::StrCrc32(*Text);
}
// Graphs

void FCNAGraphUtils::CollectGraphs(UEdGraph* Root, TArray<UEdGraph*>& OutGraphs)
{
	if (!Root || OutGraphs.Contains(Root))
	{
		return;
	}
	OutGraphs.Add(Root);
	for (UEdGraph* SubGraph : Root->SubGraphs)
	{
		CollectGraphs(SubGraph, OutGraphs);
	}
}
void FCNAGraphUtils::CollectExecuteGraphs(const UCustomNodeAsset* Asset, TArray<UEdGraph*>& OutGraphs)
{
	if (Asset)
	{
		CollectGraphs(Asset->FindExecuteGraph(), OutGraphs);
	}
}
UEdGraph* FCNAGraphUtils::GetTopLevelGraph(const UEdGraph* Graph)
{
	UEdGraph* Top = const_cast<UEdGraph*>(Graph);
	for (UObject* Outer = Graph ? Graph->GetOuter() : nullptr; Outer; Outer = Outer->GetOuter())
	{
		if (Outer->IsA<UBlueprint>())
		{
			break;
		}
		if (UEdGraph* OuterGraph = Cast<UEdGraph>(Outer))
		{
			Top = OuterGraph;
		}
	}
	return Top;
}
ECNAGraphKind FCNAGraphUtils::ClassifyGraph(const UEdGraph* Graph)
{
	const UEdGraph* Top = GetTopLevelGraph(Graph);
	if (Top && Top->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript)
	{
		return ECNAGraphKind::ConstructionScript;
	}
	const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
	const EGraphType Type = Schema ? Schema->GetGraphType(Graph) : GT_Function;
	switch (Type)
	{
	case GT_Ubergraph:
		return ECNAGraphKind::EventGraph;
	case GT_Macro:
		return ECNAGraphKind::Macro;
	default:
		break;
	}
	// Intermediate graphs of the compiler are not listed in the Blueprint (see IsLatentAllowedIn).
	if (Top && Top->GetName().StartsWith(UEdGraphSchema_K2::FN_ExecuteUbergraphBase.ToString()))
	{
		return ECNAGraphKind::EventGraph;
	}
	return ECNAGraphKind::Function;
}
bool FCNAGraphUtils::IsLatentAllowedIn(const UEdGraph* Graph)
{
	if (!Graph)
	{
		return false;
	}
	const UEdGraph* Top = GetTopLevelGraph(Graph);
	if (Top && Top->GetName().StartsWith(UEdGraphSchema_K2::FN_ExecuteUbergraphBase.ToString()))
	{
		return true;
	}
	switch (ClassifyGraph(Graph))
	{
	case ECNAGraphKind::EventGraph:
	case ECNAGraphKind::Macro:
		return true;
	default:
		break;
	}
	// A compiled clone of a graph without a function entry but with events is an event graph.
	TArray<UK2Node_FunctionEntry*> Entries;
	Top->GetNodesOfClass(Entries);
	if (Entries.Num() == 0)
	{
		TArray<UK2Node_Event*> Events;
		Top->GetNodesOfClass(Events);
		return Events.Num() > 0;
	}
	return false;
}
UBlueprint* FCNAGraphUtils::GetBlueprint(const UObject* GraphOrNode)
{
	for (const UObject* Outer = GraphOrNode; Outer; Outer = Outer->GetOuter())
	{
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Outer))
		{
			return const_cast<UBlueprint*>(Blueprint);
		}
	}
	return nullptr;
}
UCustomNodeAsset* FCNAGraphUtils::GetOwningAsset(const UObject* GraphOrNode)
{
	return Cast<UCustomNodeAsset>(GetBlueprint(GraphOrNode));
}
UCustomNodeAsset* FCNAGraphUtils::GetExecuteGraphOwner(const UEdGraph* Graph)
{
	UCustomNodeAsset* Asset = GetOwningAsset(Graph);
	if (!Asset)
	{
		return nullptr;
	}
	const UEdGraph* Top = GetTopLevelGraph(Graph);
	if (Top && (Top == Asset->FindExecuteGraph() || Top->GetFName() == UCustomNodeAsset::ExecuteGraphName))
	{
		return Asset;
	}
	return nullptr;
}
bool FCNAGraphUtils::IsBehaviorGraph(const UEdGraph* Graph)
{
	const UCustomNodeAsset* Asset = GetOwningAsset(Graph);
	if (!Asset)
	{
		return false;
	}
	const UEdGraph* Top = GetTopLevelGraph(Graph);
	return Top && Asset->UbergraphPages.Contains(const_cast<UEdGraph*>(Top));
}
bool FCNAGraphUtils::ContainsCustomNodes(const UEdGraph* Graph)
{
	if (!Graph)
	{
		return false;
	}
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (Cast<UK2Node_CustomNode>(Node))
		{
			return true;
		}
	}
	return false;
}
bool FCNAGraphUtils::DoesBlueprintSatisfyContext(const UBlueprint* Blueprint, const UClass* ContextClass)
{
	if (!Blueprint)
	{
		return false;
	}
	if (Blueprint->IsA<UCustomNodeAsset>())
	{
		// Nested inside another Execute graph: the context is checked where the outer node expands.
		return true;
	}
	if (Blueprint->BlueprintType == BPTYPE_FunctionLibrary || Blueprint->BlueprintType == BPTYPE_Interface)
	{
		// No Self object, so no caller and no world context (spec 10.1, CNA012).
		return ContextClass == nullptr;
	}
	if (!ContextClass)
	{
		return true;
	}
	const UClass* ParentClass = Blueprint->ParentClass;
	return ParentClass && ParentClass->IsChildOf(ContextClass);
}
// Analysis

bool FCNAGraphUtils::FindLatentNodes(const UCustomNodeAsset* Asset, TArray<FString>* OutNodeNames)
{
	TSet<const UObject*> Visited;
	Visited.Add(Asset);

	bool bFound = false;
	TArray<UEdGraph*> Graphs;
	CollectExecuteGraphs(Asset, Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		// Sub graphs are part of Graphs already, the recursion skips visited graphs.
		bFound |= CNAGraphUtilsLocal::FindLatentInGraph(Graph, OutNodeNames, Visited);
		if (bFound && !OutNodeNames)
		{
			return true;
		}
	}
	return bFound;
}
bool FCNAGraphUtils::FindExecutionNodes(const UCustomNodeAsset* Asset, TArray<FString>* OutNodeNames)
{
	bool bFound = false;
	TArray<UEdGraph*> Graphs;
	CollectExecuteGraphs(Asset, Graphs);
	for (const UEdGraph* Graph : Graphs)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || Node->IsA<UEdGraphNode_Comment>())
			{
				continue;
			}
			const bool bHasExecPin = Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin)
			{
				return FCNAPinUtils::IsExecPin(Pin);
			});
			if (bHasExecPin)
			{
				bFound = true;
				if (!OutNodeNames)
				{
					return true;
				}
				OutNodeNames->AddUnique(GetNodeName(Node));
			}
		}
	}
	return bFound;
}
void FCNAGraphUtils::GetNestedCustomNodes(const UCustomNodeAsset* Asset, TArray<UK2Node_CustomNode*>& OutNodes)
{
	GetExecuteNodesOfClass<UK2Node_CustomNode>(Asset, OutNodes);
}
bool FCNAGraphUtils::FindCycle(const UCustomNodeAsset* Asset, TArray<FString>& OutChain)
{
	if (!Asset)
	{
		return false;
	}
	TArray<const UCustomNodeAsset*> Stack;
	return CNAGraphUtilsLocal::FindCycleRecursive(Asset, Stack, OutChain);
}
int32 FCNAGraphUtils::GetNestingDepth(const UCustomNodeAsset* Asset, int32 Limit)
{
	if (!Asset)
	{
		return 0;
	}
	TArray<const UCustomNodeAsset*> Stack;
	return CNAGraphUtilsLocal::DepthRecursive(Asset, Stack, Limit);
}
bool FCNAGraphUtils::UsesGetCaller(const UCustomNodeAsset* Asset)
{
	TArray<UK2Node_GetCaller*> Nodes;
	GetExecuteNodesOfClass<UK2Node_GetCaller>(Asset, Nodes);
	return Nodes.Num() > 0;
}
FString FCNAGraphUtils::DescribeDynamicPins(const UCustomNodeAsset* Asset)
{
	if (!Asset)
	{
		return TEXT("None");
	}
	TArray<FCNASignaturePin> Pins;
	GetSignature(Asset, Pins);
	for (const FCNAPinOverride& Override : Asset->PinOverrides)
	{
		if (!Override.bDynamicGroup)
		{
			continue;
		}
		// Array inputs and array outputs can both be dynamic groups.
		const FCNASignaturePin* Pin = FindSignaturePin(Pins, Override.PinName);
		if (Pin && Pin->Type.IsArray())
		{
			const FText ElementText = FCNAPinUtils::GetTypeText(FCNAPinUtils::GetElementType(Pin->Type));
			return FString::Printf(TEXT("Array of %s"), *ElementText.ToString());
		}
	}
	return TEXT("None");
}
bool FCNAGraphUtils::RefreshCachedAssetData(UCustomNodeAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}
	const bool bLatent = FindLatentNodes(Asset, nullptr);
	const FString DynamicDescription = DescribeDynamicPins(Asset);
	const bool bChanged = bLatent != Asset->bContainsLatent || DynamicDescription != Asset->CachedDynamicPinsDescription;

	// Cached editor data: no transaction, the values are derived from the graphs (spec 9.4 applies to user edits).
	Asset->bContainsLatent = bLatent;
	Asset->CachedDynamicPinsDescription = DynamicDescription;
	return bChanged;
}
// Text

FString FCNAGraphUtils::GetNodeName(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return TEXT("<none>");
	}
	FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
	int32 NewLine = INDEX_NONE;
	if (Title.FindChar(TEXT('\n'), NewLine))
	{
		Title = Title.Left(NewLine);
	}
	return Title.IsEmpty() ? Node->GetName() : Title;
}
FString FCNAGraphUtils::JoinNames(const TArray<FString>& Names, int32 MaxItems)
{
	FString Result;
	const int32 Shown = FMath::Min(Names.Num(), FMath::Max(1, MaxItems));
	for (int32 Index = 0; Index < Shown; ++Index)
	{
		if (Index > 0)
		{
			Result += TEXT(", ");
		}
		Result += Names[Index];
	}
	if (Names.Num() > Shown)
	{
		Result += FString::Printf(TEXT(" (+%d)"), Names.Num() - Shown);
	}
	return Result;
}
#undef LOCTEXT_NAMESPACE
