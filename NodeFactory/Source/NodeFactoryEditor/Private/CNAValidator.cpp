// Copyright (c) NodeFactory. Checks of custom nodes at compile time (spec 5.1, item 1) and of assets (spec 10.2).

#include "CNAValidator.h"
#include "CNAEditorSettings.h"
#include "CNAGraphUtils.h"
#include "CNAPinUtils.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "K2Node_GetLocal.h"
#include "NodeFactoryModule.h"
#include "NodeFactoryStyle.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Toolkits/AssetEditorManager.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNAValidatorLocal
{
	/** Same limit as FCNAPinOverride::GetDynamicRange (CNATypes.cpp). */
	const int32 DynamicLimit = 64;

	FFormatNamedArguments PinArgs(const UCustomNodeAsset* Asset, FName PinName)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Pin"), FText::FromName(PinName));
		return Arguments;
	}

	bool HasAnyLink(const UEdGraphNode* Node)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->LinkedTo.Num() > 0)
			{
				return true;
			}
		}
		return false;
	}

	bool IsObjectCategory(FName Category)
	{
		return Category == UEdGraphSchema_K2::PC_Object
			|| Category == UEdGraphSchema_K2::PC_Class
			|| Category == UEdGraphSchema_K2::PC_Interface
			|| Category == UEdGraphSchema_K2::PC_SoftObject
			|| Category == UEdGraphSchema_K2::PC_SoftClass;
	}

	FText GetDirectionText(EEdGraphPinDirection Direction)
	{
		return Direction == EGPD_Input ? LOCTEXT("Validator.Input", "input") : LOCTEXT("Validator.Output", "output");
	}

	FText GetBodyWidgetTypeText(ECNABodyWidgetType Type)
	{
		switch (Type)
		{
		case ECNABodyWidgetType::CheckBox:
			return LOCTEXT("Validator.CheckBox", "CheckBox");
		case ECNABodyWidgetType::ComboBox:
			return LOCTEXT("Validator.ComboBox", "ComboBox");
		case ECNABodyWidgetType::ScrollList:
			return FText::FromString(TEXT("ScrollList"));
		case ECNABodyWidgetType::Slider:
			return FText::FromString(TEXT("Slider"));
		case ECNABodyWidgetType::TextBox:
			return LOCTEXT("Validator.TextBox", "TextBox");
		default:
			return FText::GetEmpty();
		}
	}

	/** The Blueprint a node is compiled for. Compiled copies may live below the generated class. */
	const UBlueprint* FindCompilingBlueprint(const UK2Node_CustomNode* Node)
	{
		if (const UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(Node))
		{
			return Blueprint;
		}
		for (const UObject* Outer = Node ? Node->GetOuter() : nullptr; Outer; Outer = Outer->GetOuter())
		{
			if (const UClass* Class = Cast<UClass>(Outer))
			{
				if (const UBlueprint* Generator = Cast<UBlueprint>(Class->ClassGeneratedBy))
				{
					return Generator;
				}
			}
		}
		return nullptr;
	}

	bool HasDefaultToSelfPins(const UCustomNodeAsset* Asset)
	{
		for (const FCNAPinOverride& Override : Asset->PinOverrides)
		{
			if (Override.bDefaultToSelf)
			{
				return true;
			}
		}
		return false;
	}

	FName GetFirstDynamicGroupName(const UCustomNodeAsset* Asset)
	{
		for (const FCNAPinOverride& Override : Asset->PinOverrides)
		{
			if (Override.bDynamicGroup)
			{
				return Override.PinName;
			}
		}
		return NAME_None;
	}

	/** Signature pin of a redirect target; element pins of a dynamic group are found by their group. */
	bool DoesRedirectTargetExist(const TArray<FCNASignaturePin>& Signature, FName NewName)
	{
		if (FCNAGraphUtils::FindSignaturePin(Signature, NewName))
		{
			return true;
		}
		const FName BaseName = FCNAPinUtils::GetBaseName(NewName);
		return BaseName != NewName && FCNAGraphUtils::FindSignaturePin(Signature, BaseName) != nullptr;
	}
}

// Node

void FCNAValidator::ValidateNodeForCompilation(const UK2Node_CustomNode* Node, FCNADiagnosticList& Out)
{
	if (!Node)
	{
		return;
	}

	// CNA005: without the asset, its macro Execute and its Tunnel nodes nothing else can be checked.
	const UCustomNodeAsset* Asset = Node->GetCustomNodeAsset();
	if (!Asset)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("Asset"), FText::FromString(Node->GetAssetNameForMessages()));
		Out.Add(ECNAMessage::CNA005, Arguments, Node);
		return;
	}
	const FFormatNamedArguments AssetArguments = FCNAMessages::AssetArgs(Asset);
	TArray<FCNASignaturePin> Signature;
	if (!FCNAGraphUtils::HasValidExecuteGraph(Asset) || !FCNAGraphUtils::GetSignature(Asset, Signature))
	{
		Out.Add(ECNAMessage::CNA005, AssetArguments, Node);
		return;
	}

	// CNA006 (warning): the pins were built from another version of the asset (spec 9.1).
	if (Node->IsOutdated())
	{
		FFormatNamedArguments Arguments = AssetArguments;
		Arguments.Add(TEXT("NodeVersion"), FText::AsNumber(Node->NodeVersion));
		Arguments.Add(TEXT("AssetVersion"), FText::AsNumber(Asset->AssetVersion));
		Out.Add(ECNAMessage::CNA006, Arguments, Node);
	}

	// CNA009 (warning)
	if (Asset->bDeprecated)
	{
		FFormatNamedArguments Arguments = AssetArguments;
		Arguments.Add(TEXT("Message"), Asset->DeprecationMessage.IsEmpty()
			? LOCTEXT("Validator.NoDeprecationMessage", "no replacement was named.")
			: Asset->DeprecationMessage);
		Out.Add(ECNAMessage::CNA009, Arguments, Node);
	}

	// CNA001, CNA002: a nested clone carries the stack of the assets expanded around it (spec 5.2).
	CheckNesting(Asset, Node->GetExpansionStack(), Node, Out);

	// CNA003: latent logic only where latent nodes are allowed (spec 5.2).
	TArray<FString> LatentNames;
	if (FCNAGraphUtils::FindLatentNodes(Asset, &LatentNames) && !FCNAGraphUtils::IsLatentAllowedIn(Node->GetGraph()))
	{
		FFormatNamedArguments Arguments = AssetArguments;
		Arguments.Add(TEXT("Nodes"), FText::FromString(FCNAGraphUtils::JoinNames(LatentNames)));
		Out.Add(ECNAMessage::CNA003, Arguments, Node);
	}

	// CNA004, CNA018
	CheckPurity(Asset, Node, Out);

	// CNA007 (spec 4.5, item 4)
	if (Node->HasWildcardPins() && !Node->IsWildcardResolved())
	{
		Out.Add(ECNAMessage::CNA007, AssetArguments, Node);
	}

	// CNA008
	CheckRequiredPins(Node, Asset, Out);

	// CNA010: every class, structure and enumeration of the pins and the local variables must exist.
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && !Pin->ParentPin && FCNAPinUtils::IsTypeObjectMissing(Pin->PinType))
		{
			FFormatNamedArguments Arguments = AssetArguments;
			Arguments.Add(TEXT("Type"), FCNAPinUtils::GetTypeText(Pin->PinType));
			Arguments.Add(TEXT("Pin"), FCNAPinUtils::GetPinDisplayText(Pin));
			Out.Add(ECNAMessage::CNA010, Arguments, Node);
		}
	}
	for (const FCNALocalVariable& Variable : Asset->LocalVariables)
	{
		if (FCNAPinUtils::IsTypeObjectMissing(Variable.Type))
		{
			FFormatNamedArguments Arguments = AssetArguments;
			Arguments.Add(TEXT("Type"), FCNAPinUtils::GetTypeText(Variable.Type));
			Arguments.Add(TEXT("Pin"), FText::FromName(Variable.Name));
			Out.Add(ECNAMessage::CNA010, Arguments, Node);
		}
	}

	// CNA012
	CheckContextOfBlueprint(Node, Asset, Out);

	// CNA013
	CheckDynamicCount(Node, Asset, Out);

	// CNA015: a broken pass-through would leave the output without a value.
	CheckPassThrough(Asset, Signature, Node, Out);

	// CNA017: only wired Get Local and Set Local nodes take part in the expansion.
	CheckLocalVariableNodes(Asset, true, Node, Out);

	// CNA020-CNA022: the curves the expansion evaluates; Playback ticks with a latent loop (Event Graphs only).
	CheckCurves(Asset, Node, false, Out);
	if (const FCNABodyWidget* Playback = FCNACurveUtils::FindPlaybackCurve(Asset))
	{
		if (!FCNAGraphUtils::IsLatentAllowedIn(Node->GetGraph()))
		{
			FFormatNamedArguments Arguments = AssetArguments;
			Arguments.Add(TEXT("Curve"), FCNACurveUtils::GetCurveDisplayName(*Playback));
			Out.Add(ECNAMessage::CNA020, Arguments, Node);
		}
	}

	// CNA023: only wired Curve Value nodes take part in the expansion.
	CheckCurveValueNodes(Asset, true, Node, Out);
}

void FCNAValidator::CheckRequiredPins(const UK2Node_CustomNode* Node, const UCustomNodeAsset* Asset, FCNADiagnosticList& Out)
{
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->ParentPin || Pin->Direction != EGPD_Input || FCNAPinUtils::IsExecPin(Pin))
		{
			continue;
		}
		const FCNAPinOverride* Override = FCNAPinUtils::FindOverrideForPin(Asset, Pin->PinName);
		if (!Override || !Override->bRequired)
		{
			continue;
		}
		// Self fills a Default To Self pin; a hidden pin cannot be wired or edited (spec 4.6); the members of a
		// split pin carry their own values (V21).
		if (Override->bDefaultToSelf || Pin->bHidden || Pin->SubPins.Num() > 0)
		{
			continue;
		}
		if (FCNAPinUtils::IsPinOrSubPinLinked(Pin) || !FCNAPinUtils::IsValueEmpty(Pin))
		{
			continue;
		}
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Pin"), FCNAPinUtils::GetPinDisplayText(Pin));
		Out.Add(ECNAMessage::CNA008, Arguments, Node);
	}
}

void FCNAValidator::CheckContextOfBlueprint(const UK2Node_CustomNode* Node, const UCustomNodeAsset* Asset, FCNADiagnosticList& Out)
{
	const UBlueprint* Blueprint = CNAValidatorLocal::FindCompilingBlueprint(Node);
	if (!Blueprint)
	{
		// Without a Blueprint there is nothing to compare; the expansion reports what it cannot build.
		return;
	}

	const UClass* ContextClass = Asset->ContextClass.Get();
	bool bSatisfied = FCNAGraphUtils::DoesBlueprintSatisfyContext(Blueprint, ContextClass);

	// Get Caller and Default To Self become a Self node (spec 4.6, 4.9); function libraries and interfaces
	// have no Self object, so they cannot give a caller or a world context.
	const bool bNeedsSelf = FCNAGraphUtils::UsesGetCaller(Asset) || CNAValidatorLocal::HasDefaultToSelfPins(Asset);
	const bool bWithoutSelf = Blueprint->BlueprintType == BPTYPE_FunctionLibrary || Blueprint->BlueprintType == BPTYPE_Interface;
	if (bSatisfied && bNeedsSelf && bWithoutSelf && !Blueprint->IsA<UCustomNodeAsset>())
	{
		bSatisfied = false;
	}

	if (!bSatisfied)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Blueprint"), FText::FromString(Blueprint->GetName()));
		Arguments.Add(TEXT("ContextClass"), ContextClass
			? ContextClass->GetDisplayNameText()
			: LOCTEXT("Validator.SelfObject", "a class with a Self object"));
		Out.Add(ECNAMessage::CNA012, Arguments, Node);
	}
}

void FCNAValidator::CheckDynamicCount(const UK2Node_CustomNode* Node, const UCustomNodeAsset* Asset, FCNADiagnosticList& Out)
{
	if (!Node->HasDynamicPins())
	{
		return;
	}
	int32 Min = 0;
	int32 Max = 0;
	Node->GetDynamicRange(Min, Max);
	const int32 Count = Node->GetDynamicPinCount();
	if (Count < Min || Count > Max)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Count"), FText::AsNumber(Count));
		Arguments.Add(TEXT("Pin"), FText::FromName(CNAValidatorLocal::GetFirstDynamicGroupName(Asset)));
		Arguments.Add(TEXT("Min"), FText::AsNumber(Min));
		Arguments.Add(TEXT("Max"), FText::AsNumber(Max));
		Out.Add(ECNAMessage::CNA013, Arguments, Node);
	}
}

// Shared checks

bool FCNAValidator::CheckNesting(const UCustomNodeAsset* Asset, const TArray<const UCustomNodeAsset*>& Stack, const UObject* Context, FCNADiagnosticList& Out)
{
	if (!Asset)
	{
		return true;
	}

	// The asset is already being expanded around this node.
	if (Stack.Contains(Asset))
	{
		TArray<FString> Chain;
		for (const UCustomNodeAsset* StackAsset : Stack)
		{
			Chain.Add(GetNameSafe(StackAsset));
		}
		Chain.Add(Asset->GetName());
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Chain"), FText::FromString(FString::Join(Chain, TEXT(" -> "))));
		Out.Add(ECNAMessage::CNA001, Arguments, Context);
		return false;
	}

	// A cycle further down: A contains B, B contains A (spec 10.1).
	TArray<FString> Chain;
	if (FCNAGraphUtils::FindCycle(Asset, Chain))
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Chain"), FText::FromString(FString::Join(Chain, TEXT(" -> "))));
		Out.Add(ECNAMessage::CNA001, Arguments, Context);
		return false;
	}

	const int32 Limit = FMath::Max(1, GetDefault<UCNAEditorSettings>()->MaxExpansionDepth);
	const int32 Depth = Stack.Num() + FCNAGraphUtils::GetNestingDepth(Asset, Limit + 1);
	if (Depth > Limit)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Depth"), FText::AsNumber(Depth));
		Arguments.Add(TEXT("Limit"), FText::AsNumber(Limit));
		Out.Add(ECNAMessage::CNA002, Arguments, Context);
		return false;
	}
	return true;
}

void FCNAValidator::CheckPurity(const UCustomNodeAsset* Asset, const UObject* Context, FCNADiagnosticList& Out)
{
	if (!Asset || !Asset->bPure)
	{
		return;
	}

	// Pure: no exec pins and no Sequence, loops or calls of impure functions (spec 5.2).
	TArray<FString> Names;
	if (FCNAGraphUtils::FindExecutionNodes(Asset, &Names))
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Nodes"), FText::FromString(FCNAGraphUtils::JoinNames(Names)));
		Out.Add(ECNAMessage::CNA004, Arguments, Context);
	}

	// A pure node is evaluated again for every consumer, so it cannot keep state (spec 4.8, item 6).
	TArray<UK2Node_CNALocalVariableBase*> VariableNodes;
	FCNAGraphUtils::GetExecuteNodesOfClass<UK2Node_CNALocalVariableBase>(Asset, VariableNodes);
	if (Asset->LocalVariables.Num() > 0 || VariableNodes.Num() > 0)
	{
		Out.Add(ECNAMessage::CNA018, FCNAMessages::AssetArgs(Asset), Context);
	}
}

void FCNAValidator::CheckPassThrough(const UCustomNodeAsset* Asset, const TArray<FCNASignaturePin>& Signature, const UObject* Context, FCNADiagnosticList& Out)
{
	if (!Asset)
	{
		return;
	}
	for (const FCNAPinOverride& Override : Asset->PinOverrides)
	{
		CheckPassThroughOverride(Asset, Signature, Override, Context, Out);
	}
}

void FCNAValidator::CheckPassThroughOverride(const UCustomNodeAsset* Asset, const TArray<FCNASignaturePin>& Signature, const FCNAPinOverride& Override, const UObject* Context, FCNADiagnosticList& Out)
{
	if (Override.PassThroughOf.IsNone())
	{
		return;
	}
	FFormatNamedArguments Arguments = CNAValidatorLocal::PinArgs(Asset, Override.PinName);
	Arguments.Add(TEXT("Target"), FText::FromName(Override.PassThroughOf));

	const FCNASignaturePin* Source = FCNAGraphUtils::FindSignaturePin(Signature, Override.PinName, EGPD_Output);
	if (!Source)
	{
		// A missing pin is CNA014 already; an input cannot pass a value through.
		if (FCNAGraphUtils::FindSignaturePin(Signature, Override.PinName, EGPD_Input))
		{
			Out.AddText(ECNAMessage::CNA015, FText::Format(LOCTEXT("Validator.PassThroughOnInput",
				"PassThroughOf of pin {Pin} of {Asset} points to {Target}, but {Pin} is an input; only an output can pass the value of an input through."),
				Arguments), Context);
		}
		return;
	}

	const FCNASignaturePin* Target = FCNAGraphUtils::FindSignaturePin(Signature, Override.PassThroughOf, EGPD_Input);
	bool bCompatible = Target != nullptr;
	if (Target)
	{
		if (Source->IsExec() || Target->IsExec())
		{
			bCompatible = false;
		}
		else if (Source->IsWildcard() || Target->IsWildcard())
		{
			// The Wildcard pins share one element type (spec 4.5); only the containers can be compared here.
			bCompatible = Source->Type.ContainerType == Target->Type.ContainerType;
		}
		else
		{
			// The value flows from the wire of the input to the consumers of the output.
			bCompatible = FCNAPinUtils::AreTypesCompatible(Target->Type, Source->Type);
		}
	}
	if (!bCompatible)
	{
		Out.Add(ECNAMessage::CNA015, Arguments, Context);
	}
}

void FCNAValidator::CheckLocalVariableNodes(const UCustomNodeAsset* Asset, bool bOnlyWired, const UObject* Context, FCNADiagnosticList& Out)
{
	if (!Asset)
	{
		return;
	}
	TArray<UK2Node_CNALocalVariableBase*> Nodes;
	FCNAGraphUtils::GetExecuteNodesOfClass<UK2Node_CNALocalVariableBase>(Asset, Nodes);
	for (const UK2Node_CNALocalVariableBase* VariableNode : Nodes)
	{
		if (!VariableNode || VariableNode->IsVariableValid())
		{
			continue;
		}
		if (bOnlyWired && !CNAValidatorLocal::HasAnyLink(VariableNode))
		{
			continue;
		}
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Node"), FText::FromString(FCNAGraphUtils::GetNodeName(VariableNode)));
		Arguments.Add(TEXT("Variable"), VariableNode->VariableName.IsNone()
			? LOCTEXT("Validator.NoVariable", "<none>")
			: FText::FromName(VariableNode->VariableName));
		// Without a context the message points to the Get Local or Set Local node itself.
		Out.Add(ECNAMessage::CNA017, Arguments, Context ? Context : VariableNode);
	}
}

void FCNAValidator::CheckDynamicRanges(const UCustomNodeAsset* Asset, FCNADiagnosticList& Out)
{
	if (!Asset)
	{
		return;
	}
	int32 SharedMin = 0;
	int32 SharedMax = 0;
	int32 Groups = 0;
	TArray<FString> Ranges;
	for (const FCNAPinOverride& Override : Asset->PinOverrides)
	{
		if (!Override.bDynamicGroup)
		{
			continue;
		}
		int32 Min = 0;
		int32 Max = 1;
		Override.GetDynamicRange(Min, Max);
		Ranges.Add(FString::Printf(TEXT("%s %d..%d"), *Override.PinName.ToString(), Min, Max));
		SharedMin = Groups == 0 ? Min : FMath::Max(SharedMin, Min);
		SharedMax = Groups == 0 ? Max : FMath::Min(SharedMax, Max);
		++Groups;
	}

	// All groups share one DynamicPinCount (README, "Dynamic pins"), so their ranges must overlap.
	if (Groups > 1 && SharedMin > SharedMax)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Ranges"), FText::FromString(FString::Join(Ranges, TEXT(", "))));
		Out.AddText(ECNAMessage::CNA013, FText::Format(LOCTEXT("Validator.RangesDoNotOverlap",
			"The dynamic groups of {Asset} share one pin count, but their ranges do not overlap: {Ranges}."), Arguments), Asset);
	}
}

void FCNAValidator::CheckCurves(const UCustomNodeAsset* Asset, const UObject* Context, bool bIncludeWarnings, FCNADiagnosticList& Out)
{
	if (!Asset)
	{
		return;
	}
	TArray<FString> PlaybackNames;
	TSet<FName> Names;
	for (const FCNABodyWidget& Widget : Asset->BodyWidgets)
	{
		if (!Widget.IsCurve())
		{
			continue;
		}
		const FCNACurveSettings& Curve = Widget.Curve;
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Curve"), FCNACurveUtils::GetCurveDisplayName(Widget));

		auto AddProblem = [&Out, &Arguments, Context](const FText& Details, bool bWarning)
		{
			FFormatNamedArguments ProblemArguments = Arguments;
			ProblemArguments.Add(TEXT("Details"), Details);
			if (bWarning)
			{
				Out.AddText(ECNAMessage::CNA022, FText::Format(FCNAMessages::GetTemplate(ECNAMessage::CNA022), ProblemArguments), Context, true);
			}
			else
			{
				Out.Add(ECNAMessage::CNA022, ProblemArguments, Context);
			}
		};

		// CNA022: where the values come from.
		const UClass* NeededClass = FCNACurveUtils::GetCurveAssetClass(Curve.ValueType);
		FFormatNamedArguments DetailArguments;
		DetailArguments.Add(TEXT("Class"), NeededClass->GetDisplayNameText());
		DetailArguments.Add(TEXT("Type"), FCNAPinUtils::GetTypeText(FCNACurveUtils::GetValuePinType(Curve.ValueType)));
		DetailArguments.Add(TEXT("Path"), FText::FromString(Curve.CurveAsset.ToString()));
		if (Curve.bUseCurveAsset)
		{
			const UCurveBase* Loaded = Curve.CurveAsset.IsNull() ? nullptr : Curve.CurveAsset.LoadSynchronous();
			if (Curve.CurveAsset.IsNull())
			{
				AddProblem(FText::Format(LOCTEXT("Validator.CurveAssetNone", "no Curve asset is set; choose a {Class} or use the own curve."), DetailArguments), false);
			}
			else if (!Loaded)
			{
				AddProblem(FText::Format(LOCTEXT("Validator.CurveAssetMissing", "the Curve asset {Path} was not found."), DetailArguments), false);
			}
			else if (!FCNACurveUtils::IsCurveAssetCompatible(Loaded, Curve.ValueType))
			{
				DetailArguments.Add(TEXT("Actual"), Loaded->GetClass()->GetDisplayNameText());
				AddProblem(FText::Format(LOCTEXT("Validator.CurveAssetClass", "the Curve asset {Path} is a {Actual}, but the value type {Type} needs a {Class}."), DetailArguments), false);
			}
		}
		else if (bIncludeWarnings && FCNACurveUtils::IsOwnCurveEmpty(Curve))
		{
			AddProblem(LOCTEXT("Validator.CurveEmpty", "the curve has no keys; its value is 0."), true);
		}

		if (Curve.Mode == ECNACurveMode::Playback)
		{
			PlaybackNames.Add(FCNACurveUtils::GetCurveDisplayName(Widget).ToString());
			// CNA020: a pure node has no exec pins to start and stop the playback.
			if (Asset->bPure)
			{
				Out.Add(ECNAMessage::CNA020, Arguments, Context);
			}
			else if (bIncludeWarnings)
			{
				// Intermediate latent nodes of one instance share its latent action id (V-check:
				// FCompilerResultsLog::CalculateStableIdentifierForLatentActionManager), so a latent node of the
				// subgraph is ignored while the playback loop waits for the next frame.
				TArray<FString> LatentNames;
				if (FCNAGraphUtils::FindLatentNodes(Asset, &LatentNames))
				{
					FFormatNamedArguments LatentArguments = Arguments;
					LatentArguments.Add(TEXT("Nodes"), FText::FromString(FCNAGraphUtils::JoinNames(LatentNames)));
					Out.AddText(ECNAMessage::CNA020, FText::Format(LOCTEXT("Validator.PlaybackLatent",
						"Curve {Curve} of {Asset} uses Playback, but the subgraph contains latent nodes ({Nodes}); they do not run while the curve plays."),
						LatentArguments), Context, true);
				}
			}
		}

		if (bIncludeWarnings)
		{
			if (Widget.PinName.IsNone())
			{
				AddProblem(LOCTEXT("Validator.CurveNoName", "the curve has no name; give it one so Curve Value nodes can be told apart."), true);
			}
			else if (Names.Contains(Widget.PinName))
			{
				AddProblem(LOCTEXT("Validator.CurveNameTwice", "another curve has the same name; Curve Value nodes pasted from other assets use the first one."), true);
			}
			Names.Add(Widget.PinName);
		}
	}

	// CNA021: the node has one Play ... Finished set and drives its exec flow from one loop.
	if (PlaybackNames.Num() > 1)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Curves"), FText::FromString(FString::Join(PlaybackNames, TEXT(", "))));
		Out.Add(ECNAMessage::CNA021, Arguments, Context);
	}
}

void FCNAValidator::CheckCurveValueNodes(const UCustomNodeAsset* Asset, bool bOnlyWired, const UObject* Context, FCNADiagnosticList& Out)
{
	if (!Asset)
	{
		return;
	}
	TArray<UK2Node_CNACurveValue*> Nodes;
	FCNAGraphUtils::GetExecuteNodesOfClass<UK2Node_CNACurveValue>(Asset, Nodes);
	for (const UK2Node_CNACurveValue* CurveNode : Nodes)
	{
		if (!CurveNode || (bOnlyWired && !CNAValidatorLocal::HasAnyLink(CurveNode)))
		{
			continue;
		}
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Node"), FText::FromString(FCNAGraphUtils::GetNodeName(CurveNode)));
		Arguments.Add(TEXT("Curve"), CurveNode->GetCurveDisplayName());
		// Without a context the message points to the Curve Value node itself.
		const UObject* Target = Context ? Context : CurveNode;

		const FCNABodyWidget* Widget = CurveNode->FindCurveIn(Asset);
		if (!Widget)
		{
			Out.Add(ECNAMessage::CNA023, Arguments, Target);
			continue;
		}
		const UEdGraphPin* ValuePin = CurveNode->GetValuePin();
		const FEdGraphPinType Expected = FCNACurveUtils::GetValuePinType(Widget->Curve.ValueType);
		if (ValuePin && !FCNAPinUtils::AreTypesIdentical(ValuePin->PinType, Expected))
		{
			Arguments.Add(TEXT("Type"), FCNAPinUtils::GetTypeText(Expected));
			Out.AddText(ECNAMessage::CNA023, FText::Format(LOCTEXT("Validator.CurveValueOutdated",
				"{Node} is outdated: the curve {Curve} of {Asset} now gives {Type}. Use Refresh Node on it."), Arguments), Target);
		}
	}
}

void FCNAValidator::CheckSignatureTypes(const UCustomNodeAsset* Asset, const TArray<FCNASignaturePin>& Signature, const UObject* Context, FCNADiagnosticList& Out)
{
	for (const FCNASignaturePin& Pin : Signature)
	{
		if (FCNAPinUtils::IsTypeObjectMissing(Pin.Type))
		{
			FFormatNamedArguments Arguments = CNAValidatorLocal::PinArgs(Asset, Pin.Name);
			Arguments.Add(TEXT("Type"), FCNAPinUtils::GetTypeText(Pin.Type));
			Out.Add(ECNAMessage::CNA010, Arguments, Context);
		}
	}
}

void FCNAValidator::CheckPinRedirects(const UCustomNodeAsset* Asset, const TArray<FCNASignaturePin>& Signature, FCNADiagnosticList& Out)
{
	TSet<FName> OldNames;
	for (const FCNAPinRedirect& Redirect : Asset->PinRedirects)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Old"), FText::FromName(Redirect.OldName));
		Arguments.Add(TEXT("New"), FText::FromName(Redirect.NewName));

		if (Redirect.OldName.IsNone() || Redirect.NewName.IsNone() || Redirect.OldName == Redirect.NewName)
		{
			Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.RedirectIncomplete",
				"Pin redirect {Old} -> {New} of {Asset} is incomplete or points to itself; it is ignored."), Arguments), Asset);
		}
		else if (!CNAValidatorLocal::DoesRedirectTargetExist(Signature, Redirect.NewName))
		{
			Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.RedirectTargetMissing",
				"Pin redirect {Old} -> {New} of {Asset} points to a pin that does not exist in the macro Execute."), Arguments), Asset);
		}

		if (!Redirect.OldName.IsNone() && OldNames.Contains(Redirect.OldName))
		{
			Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.RedirectTwice",
				"Pin {Old} of {Asset} has more than one redirect; the first one is used."), Arguments), Asset);
		}
		OldNames.Add(Redirect.OldName);
	}
}

// Asset

bool FCNAValidator::ValidateAsset(const UCustomNodeAsset* Asset, FCNADiagnosticList& Out)
{
	if (!Asset)
	{
		Out.Add(ECNAMessage::CNA005, FCNAMessages::AssetArgs(nullptr), nullptr);
		return false;
	}
	const int32 ErrorsBefore = Out.NumErrors();

	// The macro Execute with its Tunnel nodes.
	TArray<FCNASignaturePin> Signature;
	if (!FCNAGraphUtils::HasValidExecuteGraph(Asset) || !FCNAGraphUtils::GetSignature(Asset, Signature))
	{
		Out.Add(ECNAMessage::CNA005, FCNAMessages::AssetArgs(Asset), Asset);
	}
	else
	{
		CheckSignatureTypes(Asset, Signature, Asset, Out);

		// Pin overrides (CNA014, dynamic groups, PassThroughOf).
		TSet<FName> Seen;
		for (const FCNAPinOverride& Override : Asset->PinOverrides)
		{
			if (Seen.Contains(Override.PinName))
			{
				Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.OverrideTwice",
					"Pin {Pin} of {Asset} has more than one override; the first one is used."),
					CNAValidatorLocal::PinArgs(Asset, Override.PinName)), Asset);
				continue;
			}
			Seen.Add(Override.PinName);
			ValidatePinOverride(Asset, Signature, Override, Out);
		}
		CheckDynamicRanges(Asset, Out);
		CheckPinRedirects(Asset, Signature, Out);

		// Nesting and purity as the compiler of a Blueprint would see them.
		const TArray<const UCustomNodeAsset*> EmptyStack;
		CheckNesting(Asset, EmptyStack, Asset, Out);
		CheckPurity(Asset, Asset, Out);
	}

	ValidateLocalVariables(Asset, Out);
	ValidateAppearance(Asset, Out);
	ValidateContextClass(Asset, Out);
	CheckCurves(Asset, Asset, true, Out);
	CheckCurveValueNodes(Asset, false, nullptr, Out);
	return Out.NumErrors() == ErrorsBefore;
}

void FCNAValidator::ValidatePinOverride(const UCustomNodeAsset* Asset, const TArray<FCNASignaturePin>& Signature, const FCNAPinOverride& Override, FCNADiagnosticList& Out)
{
	if (!Asset)
	{
		return;
	}
	FFormatNamedArguments Arguments = CNAValidatorLocal::PinArgs(Asset, Override.PinName);
	const FCNASignaturePin* Pin = FCNAGraphUtils::FindSignaturePin(Signature, Override.PinName);
	if (!Pin)
	{
		Out.Add(ECNAMessage::CNA014, Arguments, Asset);
		return;
	}
	Arguments.Add(TEXT("Type"), FCNAPinUtils::GetTypeText(Pin->Type));
	Arguments.Add(TEXT("Direction"), CNAValidatorLocal::GetDirectionText(Pin->Direction));

	// Dynamic group: an input array whose elements become pins (spec 4.4).
	if (Override.bDynamicGroup)
	{
		if (!Pin->IsInput() || !Pin->Type.IsArray())
		{
			Out.AddText(ECNAMessage::CNA013, FText::Format(LOCTEXT("Validator.DynamicNotArray",
				"Pin {Pin} of {Asset} is a dynamic group, but only an input array can be one (it is an {Direction} of type {Type})."),
				Arguments), Asset);
		}
		const bool bRangeValid = Override.DynamicMin >= 0 && Override.DynamicMax >= 1
			&& Override.DynamicMin <= Override.DynamicMax && Override.DynamicMax <= CNAValidatorLocal::DynamicLimit;
		if (!bRangeValid)
		{
			int32 FixedMin = 0;
			int32 FixedMax = 1;
			Override.GetDynamicRange(FixedMin, FixedMax);
			FFormatNamedArguments RangeArguments = Arguments;
			RangeArguments.Add(TEXT("Min"), FText::AsNumber(Override.DynamicMin));
			RangeArguments.Add(TEXT("Max"), FText::AsNumber(Override.DynamicMax));
			RangeArguments.Add(TEXT("FixedMin"), FText::AsNumber(FixedMin));
			RangeArguments.Add(TEXT("FixedMax"), FText::AsNumber(FixedMax));
			RangeArguments.Add(TEXT("Limit"), FText::AsNumber(CNAValidatorLocal::DynamicLimit));
			Out.AddText(ECNAMessage::CNA013, FText::Format(LOCTEXT("Validator.DynamicRange",
				"Range {Min}..{Max} of dynamic group {Pin} of {Asset} is invalid and is used as {FixedMin}..{FixedMax}. Use 0 <= DynamicMin <= DynamicMax <= {Limit}, DynamicMax >= 1."),
				RangeArguments), Asset, true);
		}
	}

	CheckPassThroughOverride(Asset, Signature, Override, Asset, Out);

	// Flags that only some pins can use: they are ignored elsewhere, so a warning is enough.
	const FEdGraphPinType ElementType = FCNAPinUtils::GetElementType(Pin->Type);
	if (Override.bDefaultToSelf && (!Pin->IsInput() || Pin->Type.IsContainer() || !CNAValidatorLocal::IsObjectCategory(Pin->Type.PinCategory)))
	{
		Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.DefaultToSelfIgnored",
			"bDefaultToSelf of pin {Pin} of {Asset} only applies to a single object input; it is ignored."), Arguments), Asset);
	}
	if (Override.bRequired && (!Pin->IsInput() || Pin->IsExec()))
	{
		Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.RequiredIgnored",
			"bRequired of pin {Pin} of {Asset} only applies to a data input; it is ignored."), Arguments), Asset);
	}
	else if (Override.bRequired && Override.bHiddenByDefault)
	{
		Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.RequiredHidden",
			"Pin {Pin} of {Asset} is required but hidden by default; CNA008 is only reported while the Behavior shows it."), Arguments), Asset);
	}
	if (Override.ConnectionRule == ECNAConnectionRule::ClassFilter)
	{
		if (Override.ClassFilter.Get() == nullptr)
		{
			Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.ClassFilterEmpty",
				"ConnectionRule ClassFilter of pin {Pin} of {Asset} has no class; the rule is ignored."), Arguments), Asset);
		}
		else if (!CNAValidatorLocal::IsObjectCategory(ElementType.PinCategory) && !FCNAPinUtils::IsWildcardType(ElementType))
		{
			Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.ClassFilterNotObject",
				"ConnectionRule ClassFilter of pin {Pin} of {Asset} needs an object or class pin (the pin is {Type}); the rule is ignored."), Arguments), Asset);
		}
	}
}

bool FCNAValidator::GetPinOverrideProblem(const UCustomNodeAsset* Asset, FName PinName, FText& OutProblem)
{
	const FCNAPinOverride* Override = Asset ? Asset->FindPinOverride(PinName) : nullptr;
	if (!Override)
	{
		return false;
	}
	TArray<FCNASignaturePin> Signature;
	FCNAGraphUtils::GetSignature(Asset, Signature);
	FCNADiagnosticList Diagnostics;
	ValidatePinOverride(Asset, Signature, *Override, Diagnostics);
	if (Diagnostics.Num() == 0)
	{
		return false;
	}
	TArray<FString> Lines;
	for (const FCNADiagnostic& Item : Diagnostics.Items)
	{
		Lines.Add(Item.Text.ToString());
	}
	OutProblem = FText::FromString(FString::Join(Lines, TEXT("\n")));
	return true;
}

void FCNAValidator::ValidateLocalVariables(const UCustomNodeAsset* Asset, FCNADiagnosticList& Out)
{
	if (!Asset)
	{
		return;
	}
	TSet<FName> Names;
	TSet<FGuid> Guids;
	for (const FCNALocalVariable& Variable : Asset->LocalVariables)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Variable"), FText::FromName(Variable.Name));
		Arguments.Add(TEXT("Type"), FCNAPinUtils::GetTypeText(Variable.Type));

		if (Variable.Name.IsNone())
		{
			Out.AddText(ECNAMessage::CNA017, FText::Format(LOCTEXT("Validator.VariableWithoutName",
				"A local variable of {Asset} has no name; Get Local and Set Local cannot select it."), Arguments), Asset, true);
		}
		else if (Names.Contains(Variable.Name))
		{
			Out.AddText(ECNAMessage::CNA017, FText::Format(LOCTEXT("Validator.VariableTwice",
				"The name of local variable {Variable} is used twice in {Asset}; rename one of them."), Arguments), Asset, true);
		}
		Names.Add(Variable.Name);

		if (!FCNALocalVariable::IsTypeAllowed(Variable.Type))
		{
			Out.AddText(ECNAMessage::CNA017, FText::Format(LOCTEXT("Validator.VariableType",
				"Local variable {Variable} of {Asset} has type {Type}; any pin type except Exec and Wildcard is allowed."), Arguments), Asset);
		}
		else if (FCNAPinUtils::IsTypeObjectMissing(Variable.Type))
		{
			FFormatNamedArguments TypeArguments = Arguments;
			TypeArguments.Add(TEXT("Pin"), FText::FromName(Variable.Name));
			Out.Add(ECNAMessage::CNA010, TypeArguments, Asset);
		}

		if (!Variable.Guid.IsValid() || Guids.Contains(Variable.Guid))
		{
			Out.AddText(ECNAMessage::CNA017, FText::Format(LOCTEXT("Validator.VariableGuid",
				"Local variable {Variable} of {Asset} has no unique id; it gets a new one when the asset is saved."), Arguments), Asset, true);
		}
		Guids.Add(Variable.Guid);
	}

	// Get Local and Set Local of the Execute graph (spec 4.8, item 6).
	CheckLocalVariableNodes(Asset, false, nullptr, Out);
}

void FCNAValidator::ValidateAppearance(const UCustomNodeAsset* Asset, FCNADiagnosticList& Out)
{
	if (!Asset)
	{
		return;
	}

	// CNA019: the icon brush must exist in EditorStyle or in the style of the plugin (V20).
	if (!Asset->Icon.IsNone() && !FNodeFactoryStyle::DoesBrushExist(Asset->Icon))
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Texture"), FText::FromName(Asset->Icon));
		Out.Add(ECNAMessage::CNA019, Arguments, Asset);
	}
	if (!Asset->IconTexture.IsNull() && Asset->IconTexture.LoadSynchronous() == nullptr)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
		Arguments.Add(TEXT("Texture"), FText::FromString(Asset->IconTexture.ToString()));
		Out.Add(ECNAMessage::CNA019, Arguments, Asset);
	}

	// bCompact only for pure nodes (spec 10.2). The node simply keeps its title, so this is a warning.
	if (Asset->bCompact && !Asset->bPure)
	{
		Out.AddText(ECNAMessage::CNA004, FText::Format(LOCTEXT("Validator.CompactNotPure",
			"{Asset} uses bCompact, but only a pure node can be drawn compact; the node keeps its normal title."),
			FCNAMessages::AssetArgs(Asset)), Asset, true);
	}

	// Body widgets need an input pin of a type the widget can edit (spec 3.2).
	TArray<FCNASignaturePin> Signature;
	FCNAGraphUtils::GetSignature(Asset, Signature);
	TSet<FName> WidgetPins;
	for (const FCNABodyWidget& Widget : Asset->BodyWidgets)
	{
		// A Curve widget belongs to no pin; CheckCurves validates it.
		if (Widget.IsCurve())
		{
			continue;
		}
		FFormatNamedArguments Arguments = CNAValidatorLocal::PinArgs(Asset, Widget.PinName);
		Arguments.Add(TEXT("Widget"), CNAValidatorLocal::GetBodyWidgetTypeText(Widget.Type));
		const FCNASignaturePin* Pin = FCNAGraphUtils::FindSignaturePin(Signature, Widget.PinName, EGPD_Input);
		if (!Pin)
		{
			Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.WidgetPinMissing",
				"{Widget} on the body of {Asset} refers to input pin {Pin}, which does not exist."), Arguments), Asset);
		}
		else if (!FCNAPinUtils::CanBodyWidgetEditType(Widget.Type, Pin->Type))
		{
			Arguments.Add(TEXT("Type"), FCNAPinUtils::GetTypeText(Pin->Type));
			Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.WidgetType",
				"{Widget} on the body of {Asset} cannot edit pin {Pin} of type {Type}; the pin keeps its normal value field."), Arguments), Asset);
		}
		else if ((Widget.Type == ECNABodyWidgetType::ComboBox || Widget.Type == ECNABodyWidgetType::ScrollList) && Widget.Options.Num() == 0 && !FCNAPinUtils::IsEnumType(Pin->Type))
		{
			Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.WidgetNoOptions",
				"ComboBox of pin {Pin} of {Asset} has no options."), Arguments), Asset);
		}
		if (Widget.Type == ECNABodyWidgetType::Slider && Widget.SliderMax <= Widget.SliderMin)
			Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.SliderRange", "Slider of pin {Pin} of {Asset} requires Slider Max greater than Slider Min."), Arguments), Asset);
		if (WidgetPins.Contains(Widget.PinName))
		{
			Out.AddText(ECNAMessage::CNA014, FText::Format(LOCTEXT("Validator.WidgetTwice",
				"Pin {Pin} of {Asset} has more than one body widget; the first one is used."), Arguments), Asset);
		}
		WidgetPins.Add(Widget.PinName);
	}
}

void FCNAValidator::ValidateContextClass(const UCustomNodeAsset* Asset, FCNADiagnosticList& Out)
{
	const UClass* ContextClass = Asset ? Asset->ContextClass.Get() : nullptr;
	if (!ContextClass)
	{
		return;
	}
	FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
	Arguments.Add(TEXT("Class"), FText::FromString(ContextClass->GetName()));

	const FString ClassName = ContextClass->GetName();
	if (ContextClass->HasAnyClassFlags(CLASS_Interface))
	{
		Out.AddText(ECNAMessage::CNA012, FText::Format(LOCTEXT("Validator.ContextInterface",
			"ContextClass {Class} of {Asset} is an interface; Get Caller needs a class the calling Blueprint derives from."), Arguments), Asset);
	}
	else if (ContextClass->HasAnyClassFlags(CLASS_NewerVersionExists) || ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")))
	{
		Out.AddText(ECNAMessage::CNA012, FText::Format(LOCTEXT("Validator.ContextStale",
			"ContextClass {Class} of {Asset} is an outdated copy of a class; select the class again."), Arguments), Asset);
	}
	else if (ContextClass->HasAnyClassFlags(CLASS_Deprecated))
	{
		Out.AddText(ECNAMessage::CNA012, FText::Format(LOCTEXT("Validator.ContextDeprecated",
			"ContextClass {Class} of {Asset} is deprecated."), Arguments), Asset, true);
	}
}

// Message Log

bool FCNAValidator::ValidateAssetToLog(const UCustomNodeAsset* Asset, bool bOpenLog)
{
	FCNAMessages::NewPage(FText::Format(LOCTEXT("Validator.Page", "Validate {0}"), FText::FromString(GetNameSafe(Asset))));

	FCNADiagnosticList Diagnostics;
	const bool bValid = ValidateAsset(Asset, Diagnostics);
	Diagnostics.FlushToMessageLog();

	FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(Asset);
	Arguments.Add(TEXT("Errors"), FText::AsNumber(Diagnostics.NumErrors()));
	Arguments.Add(TEXT("Warnings"), FText::AsNumber(Diagnostics.NumWarnings()));
	FCNAMessages::LogInfo(Diagnostics.Num() == 0
		? FText::Format(LOCTEXT("Validator.Clean", "{Asset}: no problems found."), Arguments)
		: FText::Format(LOCTEXT("Validator.Summary", "{Asset}: {Errors} error(s), {Warnings} warning(s)."), Arguments), Asset);

	if (bOpenLog)
	{
		FCNAMessages::OpenLog(EMessageSeverity::Info);
	}
	return bValid;
}

int32 FCNAValidator::ValidateAssetsToLog(const TArray<UCustomNodeAsset*>& Assets, bool bOpenLog)
{
	FCNAMessages::NewPage(FText::Format(LOCTEXT("Validator.PageMany", "Validate {0} Custom Node Assets"), FText::AsNumber(Assets.Num())));

	FScopedSlowTask SlowTask(static_cast<float>(Assets.Num()), LOCTEXT("Validator.Progress", "Validating Custom Node Assets..."));
	if (Assets.Num() > 1)
	{
		SlowTask.MakeDialog();
	}

	int32 AssetsWithErrors = 0;
	int32 TotalWarnings = 0;
	for (const UCustomNodeAsset* Asset : Assets)
	{
		SlowTask.EnterProgressFrame(1.0f, FText::FromString(GetNameSafe(Asset)));
		FCNADiagnosticList Diagnostics;
		if (!ValidateAsset(Asset, Diagnostics))
		{
			++AssetsWithErrors;
		}
		TotalWarnings += Diagnostics.NumWarnings();
		Diagnostics.FlushToMessageLog();
	}

	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("Count"), FText::AsNumber(Assets.Num()));
	Arguments.Add(TEXT("Errors"), FText::AsNumber(AssetsWithErrors));
	Arguments.Add(TEXT("Warnings"), FText::AsNumber(TotalWarnings));
	FCNAMessages::LogInfo(FText::Format(LOCTEXT("Validator.SummaryMany",
		"Validated {Count} asset(s): {Errors} with errors, {Warnings} warning(s) in total."), Arguments));

	if (bOpenLog)
	{
		FCNAMessages::OpenLog(EMessageSeverity::Info);
	}
	return AssetsWithErrors;
}

int32 FCNAValidator::ValidateAllAssetsToLog(bool bOpenLog)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssetsByClass(UCustomNodeAsset::StaticClass()->GetFName(), AssetDataList, true);

	TArray<UCustomNodeAsset*> Assets;
	for (const FAssetData& AssetData : AssetDataList)
	{
		if (UCustomNodeAsset* Asset = Cast<UCustomNodeAsset>(AssetData.GetAsset()))
		{
			Assets.Add(Asset);
		}
	}
	return ValidateAssetsToLog(Assets, bOpenLog);
}

// Registration

bool FCNAValidator::HookValidateAsset(UCustomNodeAsset* Asset, TArray<FText>& OutErrors)
{
	// IsDataValid lists errors only; warnings stay for Validate and Compile.
	FCNADiagnosticList Diagnostics;
	const bool bValid = ValidateAsset(Asset, Diagnostics);
	for (const FCNADiagnostic& Item : Diagnostics.Items)
	{
		if (Item.IsError())
		{
			OutErrors.Add(Item.Text);
		}
	}
	return bValid;
}

void FCNAValidator::Register()
{
	FCNAEditorHooks::ValidateAsset.BindStatic(&FCNAValidator::HookValidateAsset);

#if CNA_WITH_ASSET_COMPILER
	// There is no way to remove a factory again in 4.20.3; the module registers it once per session (V-check).
	static bool bCompilerRegistered = false;
	if (!bCompilerRegistered)
	{
		bCompilerRegistered = true;
		FKismetCompilerContext::RegisterCompilerForBP(UCustomNodeAsset::StaticClass(), &FCNAAssetCompilerContext::MakeCompilerContext);
	}
#endif
}

void FCNAValidator::Unregister()
{
	FCNAEditorHooks::ValidateAsset.Unbind();
}

// Compiler of the asset

#if CNA_WITH_ASSET_COMPILER

FCNAAssetCompilerContext::FCNAAssetCompilerContext(UBlueprint* SourceSketch, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompilerOptions)
	: FKismetCompilerContext(SourceSketch, InMessageLog, InCompilerOptions, nullptr)
	, bAssetChecksReported(false)
{
}

TSharedPtr<FKismetCompilerContext> FCNAAssetCompilerContext::MakeCompilerContext(UBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
{
	return MakeShareable(new FCNAAssetCompilerContext(InBlueprint, InMessageLog, InCompileOptions));
}

void FCNAAssetCompilerContext::CreateFunctionList()
{
	FKismetCompilerContext::CreateFunctionList();

	UCustomNodeAsset* Asset = Cast<UCustomNodeAsset>(Blueprint);
	if (!Asset || bAssetChecksReported)
	{
		return;
	}
	// The first compile of FKismetEditorUtilities::CreateBlueprint runs before the factory adds the macro Execute.
	if (Asset->bIsNewlyCreated && !Asset->FindExecuteGraph())
	{
		return;
	}
	bAssetChecksReported = true;

	// The cached tags (latent, dynamic pins) follow the graphs; the menu reads them without loading (spec 9.8).
	FCNAGraphUtils::RefreshCachedAssetData(Asset);

	FCNADiagnosticList Diagnostics;
	FCNAValidator::ValidateAsset(Asset, Diagnostics);
	for (const FCNADiagnostic& Item : Diagnostics.Items)
	{
		FCNAMessages::ReportTextToCompilerWithSeverity(MessageLog, nullptr, Item.IsError(), Item.Text);
	}

	// Compile in the asset window also fills the Message Log "NodeFactory" (spec 7.1, 7.3 item 5).
	if (Diagnostics.Num() > 0 && FAssetEditorManager::Get().FindEditorForAsset(Asset, false) != nullptr)
	{
		FCNAMessages::NewPage(FText::Format(LOCTEXT("Validator.CompilePage", "Compile {0}"), FText::FromString(Asset->GetName())));
		Diagnostics.FlushToMessageLog();
	}
	CNA_VERBOSE(TEXT("Compiled %s: %d error(s), %d warning(s) from the asset checks."), *Asset->GetName(), Diagnostics.NumErrors(), Diagnostics.NumWarnings());
}

#endif

#undef LOCTEXT_NAMESPACE
