// Copyright (c) NodeFactory. The Blueprint node of a Custom Node Asset (spec 2.4, 3, 4, 5, 6).

#include "K2Node_CustomNode.h"
#include "CNABehaviorHost.h"
#include "CNACurveUtils.h"
#include "CNAExpander.h"
#include "CNAGraphUtils.h"
#include "CNAMessages.h"
#include "CNANodeMenu.h"
#include "CNAPinUtils.h"
#include "CNAValidator.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryModule.h"
#include "NodeFactoryStyle.h"

#include "AssetData.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EditorStyleSet.h"
#include "Engine/Blueprint.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditorSettings.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "KismetCompiler.h"
#include "ScopedTransaction.h"
#include "Textures/SlateIcon.h"
#include "Toolkits/AssetEditorManager.h"
#include "UObject/UnrealType.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNACustomNodeLocal
{
	/** Default menu category of assets without Category. */
	const FText DefaultCategory = NSLOCTEXT("NodeFactory", "DefaultMenuCategory", "Custom Nodes");

	/** The node may raise Behavior events: it is a real node of an editor graph or of the Live Preview. */
	bool CanRaiseEvents(const UK2Node_CustomNode* Node)
	{
		if (!Node || Node->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || Node->IsTemplate())
		{
			return false;
		}
		// Loading a Blueprint rebuilds its nodes; that is not an edit and must not run Behavior (spec 6.1).
		if (GIsEditorLoadingPackage)
		{
			return false;
		}
		// Compiled copies of the node are transient; only the node in the editor graph or the Live Preview raises events.
		if (Node->HasAnyFlags(RF_Transient) && !Node->IsPreviewNode())
		{
			return false;
		}
		const UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(Node);
		return !(Blueprint && Blueprint->bIsRegeneratingOnLoad);
	}

	/** All Wildcard pins share one type; a container is kept from the signature (spec 4.5, V14). */
	FEdGraphPinType MakeWildcardType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		return Type;
	}

	/** Display text of a pin type for error messages. */
	FText DescribeType(const FEdGraphPinType& Type)
	{
		return FCNAPinUtils::GetTypeText(Type);
	}

	/** Collects the names of the split pins (top level pins that have sub pins). */
	void CollectSplitPins(const UEdGraphNode* Node, TArray<FName>& OutNames)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->ParentPin == nullptr && Pin->SubPins.Num() > 0)
			{
				OutNames.AddUnique(Pin->PinName);
			}
		}
		OutNames.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });
	}
}

// FCNATemplateData

void FCNATemplateData::ReadFromAssetData(const FAssetData& AssetData)
{
	bValid = true;
	FString Value;
	DisplayName = AssetData.GetTagValue(CNATags::DisplayName, Value) && !Value.IsEmpty()
		? FText::FromString(Value)
		: FText::FromName(AssetData.AssetName);
	Category = AssetData.GetTagValue(CNATags::Category, Value) ? FText::FromString(Value) : FText::GetEmpty();
	Keywords = AssetData.GetTagValue(CNATags::Keywords, Value) ? FText::FromString(Value) : FText::GetEmpty();
	Tooltip = AssetData.GetTagValue(CNATags::Tooltip, Value) ? FText::FromString(Value) : FText::GetEmpty();
	AllowedGraphs = AssetData.GetTagValue(CNATags::AllowedGraphs, Value) && Value.IsNumeric() ? FCString::Atoi(*Value) : CNATypes::AllGraphKindsMask();
	ContextClassPath = AssetData.GetTagValue(CNATags::ContextClass, Value) ? Value : FString();
	bLatent = AssetData.GetTagValue(CNATags::Latent, Value) && CNATags::ToBool(Value);
	bPure = AssetData.GetTagValue(CNATags::Purity, Value) && Value == TEXT("Pure");
	bDeprecated = AssetData.GetTagValue(CNATags::Deprecated, Value) && CNATags::ToBool(Value);
}

// Construction and asset

UK2Node_CustomNode::UK2Node_CustomNode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CNAAsset(nullptr)
	, NodeVersion(0)
	, DynamicPinCount(INDEX_NONE)
	, bBehaviorLock(false)
	, bReconstructing(false)
	, bBehaviorMessageActive(false)
	, bIsPreviewNode(false)
	, bExpanded(false)
	, bPendingPlacedEvent(false)
	, BehaviorChangeDepth(0)
	, bBehaviorChangedPins(false)
	, bCompileDiagnosticsReported(false)
	, bCompileHasErrors(false)
{
}

UCustomNodeAsset* UK2Node_CustomNode::GetCustomNodeAsset() const
{
	if (CNAAsset && !CNAAsset->IsPendingKill())
	{
		return CNAAsset;
	}
	// The asset was renamed or reloaded under the stored path: resolve without loading (spec 9.8).
	if (AssetPath.IsValid())
	{
		return Cast<UCustomNodeAsset>(AssetPath.ResolveObject());
	}
	return nullptr;
}

void UK2Node_CustomNode::SetCustomNodeAsset(UCustomNodeAsset* InAsset)
{
	CNAAsset = InAsset;
	AssetPath = InAsset ? FSoftObjectPath(InAsset) : FSoftObjectPath();
	NodeVersion = InAsset ? InAsset->AssetVersion : 0;
}

FString UK2Node_CustomNode::GetAssetNameForMessages() const
{
	if (const UCustomNodeAsset* Asset = GetCustomNodeAsset())
	{
		return Asset->GetName();
	}
	return AssetPath.IsValid() ? AssetPath.GetAssetName() : FString(TEXT("<none>"));
}

bool UK2Node_CustomNode::IsOutdated() const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	return Asset && NodeVersion != Asset->AssetVersion;
}

bool UK2Node_CustomNode::IsBeingCompiled() const
{
	const UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(this);
	return Blueprint && Blueprint->bBeingCompiled;
}

// Pins

void UK2Node_CustomNode::AllocateDefaultPins()
{
	UCustomNodeAsset* Asset = GetCustomNodeAsset();
	bCanRenameNode = Asset && Asset->bAllowInstanceLabel;

	TArray<FCNASignaturePin> Signature;
	if (!Asset || !FCNAGraphUtils::GetSignature(Asset, Signature))
	{
		// No pins can be built; ReconstructNode keeps the old pins in that case (spec 2.4, item 6).
		return;
	}

	// Number of element pins: DynamicMin for a new node, the stored count otherwise (spec 4.4, item 1).
	if (HasDynamicPins())
	{
		int32 Min = 0;
		int32 Max = 1;
		GetDynamicRange(Min, Max);
		DynamicPinCount = DynamicPinCount == INDEX_NONE ? Min : DynamicPinCount;
	}
	else
	{
		DynamicPinCount = FMath::Max(DynamicPinCount, 0);
	}

	// A Playback curve drives the first exec input and output of the macro itself: the node shows Play ... Update
	// instead, and ReallocatePinsDuringReconstruction moves their wires (GetCurveTakeoverPinName).
	const bool bPlayback = FCNACurveUtils::FindPlaybackCurve(Asset) != nullptr;
	const FName DrivenInput = bPlayback ? FCNACurveUtils::GetPrimaryExecPinName(Signature, EGPD_Input) : NAME_None;
	const FName DrivenOutput = bPlayback ? FCNACurveUtils::GetPrimaryExecPinName(Signature, EGPD_Output) : NAME_None;

	// Inputs first, then outputs, each in the order of the Tunnel nodes (spec 4.1). Curve exec pins come first
	// like on a Timeline, the Time inputs of the curves after the inputs of the macro.
	CreateCurvePins(*Asset, EGPD_Input, true);
	for (const FCNASignaturePin& SignaturePin : Signature)
	{
		if (SignaturePin.IsInput() && !(SignaturePin.IsExec() && SignaturePin.Name == DrivenInput))
		{
			CreatePinsForSignaturePin(*Asset, SignaturePin);
		}
	}
	CreateCurvePins(*Asset, EGPD_Input, false);
	CreateCurvePins(*Asset, EGPD_Output, true);
	for (const FCNASignaturePin& SignaturePin : Signature)
	{
		if (!SignaturePin.IsInput() && !(SignaturePin.IsExec() && SignaturePin.Name == DrivenOutput))
		{
			CreatePinsForSignaturePin(*Asset, SignaturePin);
		}
	}

	// Advanced arrow: shown only when at least one pin is advanced (spec 4.6).
	bool bHasAdvancedPins = false;
	for (const UEdGraphPin* Pin : Pins)
	{
		bHasAdvancedPins |= Pin && Pin->bAdvancedView;
	}
	if (bHasAdvancedPins && AdvancedPinDisplay == ENodeAdvancedPins::NoPins)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
	}
	else if (!bHasAdvancedPins)
	{
		AdvancedPinDisplay = ENodeAdvancedPins::NoPins;
	}

	NodeVersion = Asset->AssetVersion;

	if (bPendingPlacedEvent)
	{
		bPendingPlacedEvent = false;
		FireNodePlaced();
	}
}

void UK2Node_CustomNode::CreatePinsForSignaturePin(const UCustomNodeAsset& Asset, const FCNASignaturePin& SignaturePin)
{
	// A pure node has no exec pins (spec 3.1, bPure); the validator reports exec nodes inside (CNA004).
	if (Asset.bPure && SignaturePin.IsExec())
	{
		return;
	}

	const FCNAPinOverride* Override = Asset.FindPinOverride(SignaturePin.Name);
	// An array input is gathered by Make Array, an array output is read element by element (Array Get).
	const bool bDynamic = Override && Override->bDynamicGroup && SignaturePin.Type.IsArray();

	if (bDynamic)
	{
		// Element pins "Name__0".."Name__N-1" of the element type (spec 4.4).
		FEdGraphPinType ElementType = FCNAPinUtils::GetElementType(SignaturePin.Type);
		if (FCNAPinUtils::IsWildcardType(ElementType) && IsWildcardResolved())
		{
			FCNAPinUtils::ApplyResolvedWildcard(ElementType, ResolvedWildcardType);
		}
		for (int32 Index = 0; Index < DynamicPinCount; ++Index)
		{
			UEdGraphPin* Pin = CreatePin(SignaturePin.Direction, ElementType, CNATypes::MakeDynamicPinName(SignaturePin.Name, Index));
			if (SignaturePin.IsInput())
			{
				GetDefault<UEdGraphSchema_K2>()->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			}
			ApplyPinOverrides(Asset, Pin, Override, Index);
		}
		return;
	}

	FEdGraphPinType PinType = SignaturePin.Type;
	if (SignaturePin.IsWildcard() && IsWildcardResolved())
	{
		FCNAPinUtils::ApplyResolvedWildcard(PinType, ResolvedWildcardType);
	}

	UEdGraphPin* Pin = CreatePin(SignaturePin.Direction, PinType, SignaturePin.Name);
	if (SignaturePin.IsInput() && !SignaturePin.IsExec())
	{
		FCNAPinUtils::ApplySignatureDefault(SignaturePin, Pin);
	}
	ApplyPinOverrides(Asset, Pin, Override, INDEX_NONE);
}

void UK2Node_CustomNode::CreateCurvePins(const UCustomNodeAsset& Asset, EEdGraphPinDirection Direction, bool bExecPins)
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	TArray<ECNACurvePinRole> Roles;
	for (const FCNABodyWidget& Widget : Asset.BodyWidgets)
	{
		if (!Widget.IsCurve() || !Widget.WidgetGuid.IsValid())
		{
			continue;
		}
		FCNACurveUtils::GetPinRoles(Widget.Curve, Asset.bPure, Roles);
		for (ECNACurvePinRole Role : Roles)
		{
			if (FCNACurveUtils::GetRoleDirection(Role) != Direction || FCNACurveUtils::IsExecRole(Role) != bExecPins)
			{
				continue;
			}
			FEdGraphPinType Type;
			Type.PinCategory = bExecPins ? UEdGraphSchema_K2::PC_Exec : UEdGraphSchema_K2::PC_Float;
			UEdGraphPin* Pin = CreatePin(Direction, Type, CNATypes::MakeCurvePinName(Widget.WidgetGuid, Role));
			if (!bExecPins)
			{
				Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			}
			// The label is only the display name: renaming it keeps the wires (spec of the curve widget).
			Pin->PinFriendlyName = Widget.Curve.GetPinLabel(Role);
			Pin->PinToolTip = FText::Format(LOCTEXT("CurvePinTooltip", "{0} of the curve {1}."),
				CNATypes::GetCurvePinRoleDefaultName(Role), FCNACurveUtils::GetCurveDisplayName(Widget)).ToString();
			ApplyPinOverrides(Asset, Pin, nullptr, INDEX_NONE);
		}
	}
}

UEdGraphPin* UK2Node_CustomNode::FindCurvePin(const FGuid& WidgetGuid, ECNACurvePinRole Role) const
{
	return WidgetGuid.IsValid() ? FCNAPinUtils::FindTopLevelPin(this, CNATypes::MakeCurvePinName(WidgetGuid, Role), FCNACurveUtils::GetRoleDirection(Role)) : nullptr;
}

FName UK2Node_CustomNode::GetCurveTakeoverPinName(FName OldName, EEdGraphPinDirection Direction) const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if (!Asset)
	{
		return NAME_None;
	}
	FGuid WidgetGuid;
	ECNACurvePinRole Role = ECNACurvePinRole::Time;
	const bool bOldCurvePin = CNATypes::ParseCurvePinName(OldName, WidgetGuid, Role);
	// Assets without curves (and pins that are no curve pins of them) never take anything over.
	if (!bOldCurvePin && !Asset->BodyWidgets.ContainsByPredicate([](const FCNABodyWidget& Widget) { return Widget.IsCurve(); }))
	{
		return NAME_None;
	}
	TArray<FCNASignaturePin> Signature;
	if (!FCNAGraphUtils::GetSignature(Asset, Signature))
	{
		return NAME_None;
	}
	const FName PrimaryExec = FCNACurveUtils::GetPrimaryExecPinName(Signature, Direction);
	if (PrimaryExec.IsNone())
	{
		return NAME_None;
	}
	const ECNACurvePinRole TakeoverRole = Direction == EGPD_Input ? ECNACurvePinRole::Play : ECNACurvePinRole::Update;

	// Playback switched on: the wires of the exec flow of the macro go to Play and Update.
	if (const FCNABodyWidget* Playback = FCNACurveUtils::FindPlaybackCurve(Asset))
	{
		return OldName == PrimaryExec ? CNATypes::MakeCurvePinName(Playback->WidgetGuid, TakeoverRole) : NAME_None;
	}

	// Playback switched off (or the curve removed): Play and Update give their wires back to the exec flow.
	return bOldCurvePin && Role == TakeoverRole ? PrimaryExec : NAME_None;
}

void UK2Node_CustomNode::ApplyPinOverrides(const UCustomNodeAsset& Asset, UEdGraphPin* Pin, const FCNAPinOverride* Override, int32 DynamicIndex)
{
	if (!Pin)
	{
		return;
	}

	if (Override)
	{
		if (DynamicIndex != INDEX_NONE)
		{
			Pin->PinFriendlyName = Override->FormatDynamicName(DynamicIndex);
		}
		else if (!Override->DisplayName.IsEmpty())
		{
			Pin->PinFriendlyName = Override->DisplayName;
		}
		Pin->PinToolTip = Override->Tooltip.ToString();
		Pin->bAdvancedView = Override->bAdvanced;
		Pin->bHidden = Override->bHiddenByDefault;
	}

	// Behavior changes of this instance win over the asset (spec 6.1).
	if (const FCNAInstanceOverride* InstanceOverride = FindInstanceOverride(Pin->PinName))
	{
		if (InstanceOverride->bOverrideVisibility)
		{
			Pin->bHidden = !InstanceOverride->bVisible;
		}
		if (InstanceOverride->bOverrideDisplayName)
		{
			Pin->PinFriendlyName = InstanceOverride->DisplayName;
		}
	}

	// The type of a missing struct, enum or class makes the pin unusable (CNA010, reported by the validator).
	if (FCNAPinUtils::IsTypeObjectMissing(Pin->PinType))
	{
		Pin->bNotConnectable = true;
	}
}

void UK2Node_CustomNode::GetPinsOfSignaturePin(FName SignatureName, TArray<UEdGraphPin*>& OutPins) const
{
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin->ParentPin == nullptr && (Pin->PinName == SignatureName || FCNAPinUtils::GetBaseName(Pin->PinName) == SignatureName))
		{
			OutPins.Add(Pin);
		}
	}
}

void UK2Node_CustomNode::GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	Schema->ConstructBasicPinTooltip(Pin, FText::FromString(Pin.PinToolTip), HoverTextOut);
	if (Pin.bHidden)
	{
		HoverTextOut += TEXT("\n");
		HoverTextOut += LOCTEXT("HiddenPinHover", "(hidden: cannot be connected)").ToString();
	}
}

// Dynamic pins (spec 4.4)

bool UK2Node_CustomNode::HasDynamicPins() const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	return Asset && Asset->HasDynamicGroup();
}

void UK2Node_CustomNode::GetDynamicRange(int32& OutMin, int32& OutMax) const
{
	OutMin = 0;
	OutMax = 0;
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if (!Asset)
	{
		return;
	}
	bool bFirst = true;
	for (const FCNAPinOverride& Override : Asset->PinOverrides)
	{
		if (!Override.bDynamicGroup)
		{
			continue;
		}
		int32 Min = 0;
		int32 Max = 1;
		Override.GetDynamicRange(Min, Max);
		// All groups share one count, so the range is the intersection (README, "Dynamic pins").
		OutMin = bFirst ? Min : FMath::Max(OutMin, Min);
		OutMax = bFirst ? Max : FMath::Min(OutMax, Max);
		bFirst = false;
	}
	OutMax = FMath::Max(OutMax, OutMin);
}

int32 UK2Node_CustomNode::GetDynamicPinCount() const
{
	return HasDynamicPins() ? FMath::Max(DynamicPinCount, 0) : 0;
}

FName UK2Node_CustomNode::GetDynamicPinName(int32 Index) const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if (!Asset || Index < 0 || Index >= GetDynamicPinCount())
	{
		return NAME_None;
	}
	for (const FCNAPinOverride& Override : Asset->PinOverrides)
	{
		if (Override.bDynamicGroup)
		{
			return CNATypes::MakeDynamicPinName(Override.PinName, Index);
		}
	}
	return NAME_None;
}

bool UK2Node_CustomNode::CanAddDynamicElement() const
{
	int32 Min = 0;
	int32 Max = 0;
	GetDynamicRange(Min, Max);
	return HasDynamicPins() && GetDynamicPinCount() < Max;
}

bool UK2Node_CustomNode::CanRemoveDynamicElement() const
{
	int32 Min = 0;
	int32 Max = 0;
	GetDynamicRange(Min, Max);
	return HasDynamicPins() && GetDynamicPinCount() > Min;
}

void UK2Node_CustomNode::AddDynamicElement()
{
	if (!CanAddDynamicElement())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("AddDynamicPinTransaction", "Add Pin"));
	Modify();
	SetDynamicPinCount(GetDynamicPinCount() + 1, true);
}

void UK2Node_CustomNode::RemoveDynamicElement(int32 Index)
{
	const int32 Count = GetDynamicPinCount();
	if (!CanRemoveDynamicElement() || Index < 0 || Index >= Count)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("RemoveDynamicPinTransaction", "Remove Pin"));
	Modify();

	const FName RemovedName = GetDynamicPinName(Index);
	UCustomNodeAsset* Asset = GetCustomNodeAsset();

	// Elements after the removed one move down by one: wires and values follow their element (spec 4.4, item 3).
	for (const FCNAPinOverride& Override : Asset->PinOverrides)
	{
		if (!Override.bDynamicGroup)
		{
			continue;
		}
		// Element pins are inputs of an array input group or outputs of an array output group.
		const EEdGraphPinDirection Directions[] = { EGPD_Input, EGPD_Output };
		for (EEdGraphPinDirection Direction : Directions)
		{
			UEdGraphPin* Removed = FindPin(CNATypes::MakeDynamicPinName(Override.PinName, Index), Direction);
			if (Removed)
			{
				FCNAPinUtils::BreakAllLinksRecursive(Removed);
			}
			for (int32 Next = Index + 1; Next < Count; ++Next)
			{
				UEdGraphPin* From = FindPin(CNATypes::MakeDynamicPinName(Override.PinName, Next), Direction);
				UEdGraphPin* To = FindPin(CNATypes::MakeDynamicPinName(Override.PinName, Next - 1), Direction);
				if (From && To)
				{
					To->Modify();
					From->Modify();
					if (Direction == EGPD_Input)
					{
						FCNAPinUtils::CopyDefaultValue(From, To);
					}
					TArray<UEdGraphPin*> Linked = From->LinkedTo;
					From->BreakAllPinLinks();
					for (UEdGraphPin* Other : Linked)
					{
						if (Other)
						{
							To->MakeLinkTo(Other);
						}
					}
				}
			}
		}
	}

	DynamicPinCount = Count - 1;
	ReconstructNode();
	if (UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(this))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
	FCNABehaviorHost::Get().NotifyRemovePin(this, GetDynamicPinCount(), RemovedName);
}

void UK2Node_CustomNode::RemoveDynamicElementPin(UEdGraphPin* Pin)
{
	if (Pin && Pin->GetOwningNode() == this)
	{
		const int32 Index = FCNAPinUtils::GetDynamicIndex(Pin->ParentPin ? Pin->ParentPin->PinName : Pin->PinName);
		if (Index != INDEX_NONE)
		{
			RemoveDynamicElement(Index);
		}
	}
}

void UK2Node_CustomNode::SetDynamicPinCount(int32 NewCount, bool bFireEvents)
{
	int32 Min = 0;
	int32 Max = 0;
	GetDynamicRange(Min, Max);
	// The pins tell the old count: DynamicPinCount may already hold the new value (Details panel).
	const int32 OldCount = CountElementPins();
	// Values outside the range are kept and reported by CNA013 only when they come from old data.
	const int32 Clamped = FMath::Clamp(NewCount, Min, Max);
	if (Clamped == OldCount && DynamicPinCount == Clamped)
	{
		return;
	}

	const FName LastName = OldCount > 0 ? CNATypes::MakeDynamicPinName(FCNAPinUtils::GetBaseName(GetDynamicPinName(0)), OldCount - 1) : NAME_None;
	Modify();
	DynamicPinCount = Clamped;
	ReconstructNode();
	if (UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(this))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	if (!bFireEvents)
	{
		return;
	}
	// One event per added or removed pin (spec 4.4, item 6).
	if (Clamped > OldCount)
	{
		for (int32 Count = OldCount + 1; Count <= Clamped; ++Count)
		{
			FCNABehaviorHost::Get().NotifyAddPin(this, Count);
		}
	}
	else
	{
		const UCustomNodeAsset* Asset = GetCustomNodeAsset();
		for (int32 Count = OldCount - 1; Count >= Clamped; --Count)
		{
			FName Removed = LastName;
			if (Asset)
			{
				for (const FCNAPinOverride& Override : Asset->PinOverrides)
				{
					if (Override.bDynamicGroup)
					{
						Removed = CNATypes::MakeDynamicPinName(Override.PinName, Count);
						break;
					}
				}
			}
			FCNABehaviorHost::Get().NotifyRemovePin(this, Count, Removed);
		}
	}
}

// Wildcard (spec 4.5)

bool UK2Node_CustomNode::HasWildcardPins() const
{
	TArray<FCNASignaturePin> Signature;
	if (FCNAGraphUtils::GetSignature(GetCustomNodeAsset(), Signature))
	{
		for (const FCNASignaturePin& SignaturePin : Signature)
		{
			if (SignaturePin.IsWildcard())
			{
				return true;
			}
		}
	}
	return false;
}

bool UK2Node_CustomNode::IsSignatureWildcardPin(const UEdGraphPin* Pin) const
{
	if (!Pin || Pin->GetOwningNodeUnchecked() != this)
	{
		return false;
	}
	const UEdGraphPin* TopPin = Pin;
	while (TopPin->ParentPin)
	{
		TopPin = TopPin->ParentPin;
	}
	TArray<FCNASignaturePin> Signature;
	if (!FCNAGraphUtils::GetSignature(GetCustomNodeAsset(), Signature))
	{
		return false;
	}
	const FName SignatureName = FCNAPinUtils::GetBaseName(TopPin->PinName);
	const FCNASignaturePin* SignaturePin = FCNAGraphUtils::FindSignaturePin(Signature, SignatureName, TopPin->Direction);
	if (!SignaturePin)
	{
		SignaturePin = FCNAGraphUtils::FindSignaturePin(Signature, TopPin->PinName, TopPin->Direction);
	}
	return SignaturePin && SignaturePin->IsWildcard();
}

FEdGraphPinType UK2Node_CustomNode::MakeResolvedTypeFrom(const UEdGraphPin* OtherPin)
{
	FEdGraphPinType Resolved = FCNAPinUtils::GetElementType(OtherPin->PinType);
	Resolved.bIsReference = false;
	Resolved.bIsConst = false;
	Resolved.PinValueType = FEdGraphTerminalType();
	return Resolved;
}

bool UK2Node_CustomNode::UpdateResolvedWildcardFromLinks()
{
	const FEdGraphPinType Previous = ResolvedWildcardType;
	const UEdGraphPin* FirstLinked = nullptr;
	bool bAnyLinked = false;

	for (const UEdGraphPin* Pin : Pins)
	{
		if (!Pin || Pin->ParentPin != nullptr || !IsSignatureWildcardPin(Pin))
		{
			continue;
		}
		for (const UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (Linked && !FCNAPinUtils::IsWildcardType(Linked->PinType))
			{
				bAnyLinked = true;
				FirstLinked = FirstLinked ? FirstLinked : Linked;
			}
		}
	}

	if (!bAnyLinked)
	{
		// The last wire is gone: back to Wildcard (spec 4.5, item 3). A type set by the Behavior stays until then.
		bool bAnyWire = false;
		for (const UEdGraphPin* Pin : Pins)
		{
			bAnyWire |= Pin && IsSignatureWildcardPin(Pin) && Pin->LinkedTo.Num() > 0;
		}
		if (!bAnyWire)
		{
			ResolvedWildcardType = FEdGraphPinType();
		}
	}
	else if (!IsWildcardResolved())
	{
		// The first wire resolves the type of all Wildcard pins (spec 4.5, item 2).
		ResolvedWildcardType = MakeResolvedTypeFrom(FirstLinked);
	}

	return !FCNAPinUtils::AreTypesIdentical(Previous, ResolvedWildcardType) || Previous.PinCategory != ResolvedWildcardType.PinCategory;
}

void UK2Node_CustomNode::RefreshWildcardTypes()
{
	TArray<FCNASignaturePin> Signature;
	if (!FCNAGraphUtils::GetSignature(GetCustomNodeAsset(), Signature))
	{
		return;
	}
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	for (UEdGraphPin* Pin : Pins)
	{
		if (!Pin || Pin->ParentPin != nullptr)
		{
			continue;
		}
		const FName SignatureName = FCNAPinUtils::GetDynamicIndex(Pin->PinName) != INDEX_NONE ? FCNAPinUtils::GetBaseName(Pin->PinName) : Pin->PinName;
		const FCNASignaturePin* SignaturePin = FCNAGraphUtils::FindSignaturePin(Signature, SignatureName, Pin->Direction);
		if (!SignaturePin || !SignaturePin->IsWildcard())
		{
			continue;
		}

		FEdGraphPinType NewType = FCNAPinUtils::GetDynamicIndex(Pin->PinName) != INDEX_NONE
			? FCNAPinUtils::GetElementType(SignaturePin->Type)
			: SignaturePin->Type;
		if (IsWildcardResolved())
		{
			FCNAPinUtils::ApplyResolvedWildcard(NewType, ResolvedWildcardType);
		}
		if (FCNAPinUtils::AreTypesIdentical(NewType, Pin->PinType))
		{
			continue;
		}

		Pin->Modify();
		Pin->PinType = NewType;
		Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);

		// Wires that do not fit the new type are broken (the K2 schema decides).
		TArray<UEdGraphPin*> Linked = Pin->LinkedTo;
		for (UEdGraphPin* Other : Linked)
		{
			if (!Other)
			{
				continue;
			}
			const UEdGraphPin* OutputPin = Pin->Direction == EGPD_Output ? Pin : Other;
			const UEdGraphPin* InputPin = Pin->Direction == EGPD_Output ? Other : Pin;
			if (!Schema->ArePinTypesCompatible(OutputPin->PinType, InputPin->PinType))
			{
				Pin->BreakLinkTo(Other);
			}
		}
	}
}

// Instance overrides and appearance

const FCNAInstanceOverride* UK2Node_CustomNode::FindInstanceOverride(FName PinName) const
{
	return InstanceOverrides.FindByPredicate([PinName](const FCNAInstanceOverride& Item) { return Item.PinName == PinName; });
}

FCNAInstanceOverride& UK2Node_CustomNode::FindOrAddInstanceOverride(FName PinName)
{
	if (FCNAInstanceOverride* Existing = InstanceOverrides.FindByPredicate([PinName](const FCNAInstanceOverride& Item) { return Item.PinName == PinName; }))
	{
		return *Existing;
	}
	const int32 Index = InstanceOverrides.AddDefaulted();
	InstanceOverrides[Index].PinName = PinName;
	return InstanceOverrides[Index];
}

void UK2Node_CustomNode::ResetInstanceOverrides()
{
	const FScopedTransaction Transaction(LOCTEXT("ResetInstanceOverridesTransaction", "Reset Behavior Changes"));
	Modify();
	InstanceOverrides.Reset();
	TitleOverride = FText::GetEmpty();
	ReconstructNode();
	if (UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(this))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

ECNAConnectionRule UK2Node_CustomNode::GetEffectiveConnectionRule(const UEdGraphPin* Pin, UClass*& OutClassFilter) const
{
	OutClassFilter = nullptr;
	if (!Pin)
	{
		return ECNAConnectionRule::None;
	}
	const UEdGraphPin* TopPin = Pin;
	while (TopPin->ParentPin)
	{
		TopPin = TopPin->ParentPin;
	}
	if (const FCNAInstanceOverride* InstanceOverride = FindInstanceOverride(TopPin->PinName))
	{
		if (InstanceOverride->bOverrideRule)
		{
			OutClassFilter = InstanceOverride->ClassFilter.Get();
			return InstanceOverride->Rule;
		}
	}
	if (const FCNAPinOverride* Override = FCNAPinUtils::FindOverrideForPin(GetCustomNodeAsset(), TopPin->PinName))
	{
		OutClassFilter = Override->ClassFilter.Get();
		return Override->ConnectionRule;
	}
	return ECNAConnectionRule::None;
}

bool UK2Node_CustomNode::GetPinColorOverride(const UEdGraphPin* Pin, FLinearColor& OutColor) const
{
	const UEdGraphPin* TopPin = Pin;
	while (TopPin && TopPin->ParentPin)
	{
		TopPin = TopPin->ParentPin;
	}
	const FCNAPinOverride* Override = TopPin ? FCNAPinUtils::FindOverrideForPin(GetCustomNodeAsset(), TopPin->PinName) : nullptr;
	if (Override && Override->bOverridePinColor)
	{
		OutColor = Override->PinColor;
		return true;
	}
	return false;
}

FLinearColor UK2Node_CustomNode::GetPinLabelColor(const UEdGraphPin* Pin) const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	const UEdGraphPin* TopPin = Pin;
	while (TopPin && TopPin->ParentPin)
	{
		TopPin = TopPin->ParentPin;
	}
	const FCNAPinOverride* Override = TopPin ? FCNAPinUtils::FindOverrideForPin(Asset, TopPin->PinName) : nullptr;
	if (Override && Override->bOverrideLabelColor)
	{
		return Override->LabelColor;
	}
	return Asset ? Asset->PinLabelColor : FLinearColor::White;
}

const FCNABodyWidget* UK2Node_CustomNode::FindBodyWidget(FName PinName) const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if (!Asset || PinName.IsNone())
	{
		return nullptr;
	}
	// The PinName of a Curve widget is the name of the curve, not a pin.
	return Asset->BodyWidgets.FindByPredicate([PinName](const FCNABodyWidget& Widget) { return !Widget.IsCurve() && Widget.PinName == PinName; });
}

void UK2Node_CustomNode::SetAdvancedViewExpanded(bool bInExpanded)
{
	const bool bCurrentlyExpanded = AdvancedPinDisplay == ENodeAdvancedPins::Shown;
	if (AdvancedPinDisplay == ENodeAdvancedPins::NoPins || bCurrentlyExpanded == bInExpanded)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("ToggleAdvancedTransaction", "Toggle Advanced Pins"));
	Modify();
	AdvancedPinDisplay = bInExpanded ? ENodeAdvancedPins::Shown : ENodeAdvancedPins::Hidden;
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
	FCNABehaviorHost::Get().NotifyAdvancedViewToggled(this, bInExpanded);
}

// Title, colors, icons (spec 3.1)

FText UK2Node_CustomNode::GetMainTitle() const
{
	if (!TitleOverride.IsEmpty())
	{
		return TitleOverride;
	}
	if (const UCustomNodeAsset* Asset = GetCustomNodeAsset())
	{
		return Asset->GetEffectiveDisplayName();
	}
	if (TemplateData.bValid)
	{
		return TemplateData.DisplayName;
	}
	if (AssetPath.IsValid())
	{
		return FText::Format(LOCTEXT("MissingAssetTitle", "Missing Custom Node ({0})"), FText::FromString(AssetPath.GetAssetName()));
	}
	return LOCTEXT("UnboundTitle", "Custom Node");
}

FText UK2Node_CustomNode::GetSubtitle() const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	return Asset ? Asset->Subtitle : FText::GetEmpty();
}

FText UK2Node_CustomNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::EditableTitle)
	{
		return InstanceLabel;
	}
	const FText MainTitle = GetMainTitle();
	if (TitleType != ENodeTitleType::FullTitle)
	{
		return MainTitle;
	}

	// The engine draws every line after the first one in a smaller font (V8).
	FString Title = MainTitle.ToString();
	const FText Subtitle = GetSubtitle();
	if (!Subtitle.IsEmpty())
	{
		Title += TEXT("\n") + Subtitle.ToString();
	}
	if (!InstanceLabel.IsEmpty())
	{
		Title += TEXT("\n") + InstanceLabel.ToString();
	}
	return FText::FromString(Title);
}

FLinearColor UK2Node_CustomNode::GetNodeTitleColor() const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if (!Asset)
	{
		return TemplateData.bValid ? GetDefault<UGraphEditorSettings>()->FunctionCallNodeTitleColor : FLinearColor(0.6f, 0.05f, 0.05f);
	}
	// HeaderColor, or the title color of the Animation or Niagara style family.
	FLinearColor StyleColor;
	if (Asset->GetStyleHeaderColor(StyleColor))
	{
		return StyleColor;
	}
	if (Asset->bUseHeaderColor)
	{
		return Asset->HeaderColor;
	}
	const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
	return Asset->bPure ? Settings->PureFunctionCallNodeTitleColor : Settings->FunctionCallNodeTitleColor;
}

FText UK2Node_CustomNode::GetTooltipText() const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if (!Asset)
	{
		if (TemplateData.bValid)
		{
			return TemplateData.Tooltip.IsEmpty() ? TemplateData.DisplayName : TemplateData.Tooltip;
		}
		return FText::Format(LOCTEXT("MissingAssetTooltip", "The Custom Node Asset {0} was not found (CNA005)."), FText::FromString(GetAssetNameForMessages()));
	}

	FTextBuilder Builder;
	Builder.AppendLine(Asset->Tooltip.IsEmpty() ? Asset->GetEffectiveDisplayName() : Asset->Tooltip);
	if (Asset->bDeprecated)
	{
		Builder.AppendLine(FText::GetEmpty());
		Builder.AppendLine(FText::Format(LOCTEXT("DeprecatedTooltipLine", "Deprecated: {0}"), Asset->DeprecationMessage));
	}
	if (!Asset->SourceUrl.IsEmpty())
	{
		// No clickable links in node tooltips (spec 3.3): the address is plain text.
		Builder.AppendLine(FText::GetEmpty());
		Builder.AppendLine(FText::Format(LOCTEXT("SourceUrlTooltipLine", "Documentation: {0}"), FText::FromString(Asset->SourceUrl)));
	}
	Builder.AppendLine(FText::GetEmpty());
	Builder.AppendLine(FText::Format(LOCTEXT("AssetTooltipLine", "Custom Node Asset: {0}"), FText::FromString(Asset->GetPathName())));
	return Builder.ToText();
}

FText UK2Node_CustomNode::GetKeywords() const
{
	if (const UCustomNodeAsset* Asset = GetCustomNodeAsset())
	{
		return Asset->Keywords;
	}
	return TemplateData.Keywords;
}

FText UK2Node_CustomNode::GetMenuCategory() const
{
	FText Category;
	if (const UCustomNodeAsset* Asset = GetCustomNodeAsset())
	{
		Category = Asset->Category;
	}
	else if (TemplateData.bValid)
	{
		Category = TemplateData.Category;
	}
	return Category.IsEmpty() ? CNACustomNodeLocal::DefaultCategory : Category;
}

FSlateIcon UK2Node_CustomNode::GetIconAndTint(FLinearColor& OutColor) const
{
	static const FName DefaultIcon(TEXT("Kismet.AllClasses.FunctionIcon"));
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	OutColor = Asset ? Asset->IconTint : FLinearColor::White;

	// A loaded IconTexture replaces the brush: SGraphNode_NodeFactory draws it in front of the title (spec 3.2).
	// The empty icon keeps the default image of the title area at zero size.
	if (Asset && !Asset->IconTexture.IsNull() && Asset->IconTexture.Get() != nullptr)
	{
		return FSlateIcon();
	}

	const FName IconName = Asset && !Asset->Icon.IsNone() ? Asset->Icon : DefaultIcon;
	// Brushes of the plugin style first, then EditorStyle; a missing brush falls back to the function icon (CNA019).
	if (FNodeFactoryStyle::IsInitialized() && FNodeFactoryStyle::Get().GetOptionalBrush(IconName, nullptr, nullptr) != nullptr)
	{
		return FSlateIcon(FNodeFactoryStyle::GetStyleSetName(), IconName);
	}
	if (FEditorStyle::Get().GetOptionalBrush(IconName, nullptr, nullptr) != nullptr)
	{
		return FSlateIcon(FEditorStyle::GetStyleSetName(), IconName);
	}
	return FSlateIcon(FEditorStyle::GetStyleSetName(), DefaultIcon);
}

FName UK2Node_CustomNode::GetCornerIcon() const
{
	if (const UCustomNodeAsset* Asset = GetCustomNodeAsset())
	{
		if (!Asset->CornerIconTexture.IsNull() && Asset->CornerIconTexture.Get()) return NAME_None;
		return CNATypes::GetCornerIconBrushName(Asset->GetEffectiveCornerIcon());
	}
	return TemplateData.bLatent ? CNATypes::GetCornerIconBrushName(ECNACornerIcon::Latent) : NAME_None;
}

bool UK2Node_CustomNode::ShouldDrawCompact() const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	return Asset && Asset->IsCompactInEffect();
}

FText UK2Node_CustomNode::GetCompactNodeTitle() const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if (Asset && !Asset->CompactTitle.IsEmpty())
	{
		return Asset->CompactTitle;
	}
	return GetMainTitle();
}

bool UK2Node_CustomNode::IsDeprecated() const
{
	if (const UCustomNodeAsset* Asset = GetCustomNodeAsset())
	{
		return Asset->bDeprecated;
	}
	return TemplateData.bDeprecated;
}

FString UK2Node_CustomNode::GetDeprecationMessage() const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	return Asset ? Asset->DeprecationMessage.ToString() : FString();
}

bool UK2Node_CustomNode::IsNodePure() const
{
	if (const UCustomNodeAsset* Asset = GetCustomNodeAsset())
	{
		return Asset->bPure;
	}
	return TemplateData.bPure;
}

// Placement and connections

bool UK2Node_CustomNode::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	if (!Super::IsCompatibleWithGraph(TargetGraph))
	{
		return false;
	}
	const UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(TargetGraph);
	if (!Blueprint)
	{
		return false;
	}

	const UCustomNodeAsset* Asset = GetCustomNodeAsset();

	// Inside a Custom Node Asset: only its Execute graph accepts nodes, never the node of the asset itself
	// (cycle, CNA001); the final placement rules are checked when the outer node is expanded (spec 5.1, item 10).
	if (const UCustomNodeAsset* OwnerAsset = Cast<UCustomNodeAsset>(Blueprint))
	{
		if (FCNAGraphUtils::IsBehaviorGraph(TargetGraph) || !FCNAGraphUtils::GetExecuteGraphOwner(TargetGraph))
		{
			return false;
		}
		return OwnerAsset != Asset && !(Asset == nullptr && TemplateData.bValid && AssetPath == FSoftObjectPath(OwnerAsset));
	}

	const ECNAGraphKind Kind = FCNAGraphUtils::ClassifyGraph(TargetGraph);
	const int32 AllowedMask = Asset ? Asset->AllowedGraphs : TemplateData.AllowedGraphs;
	if (!CNATypes::IsGraphKindAllowed(AllowedMask, Kind))
	{
		return false;
	}

	// Latent logic only where latent nodes are allowed (spec 5.2, CNA003).
	const bool bLatent = Asset ? Asset->bContainsLatent : TemplateData.bLatent;
	if (bLatent && !FCNAGraphUtils::IsLatentAllowedIn(TargetGraph))
	{
		return false;
	}

	// Curve playback ticks with a latent loop: Event Graphs only, like latent logic (CNA020).
	if (Asset && FCNACurveUtils::FindPlaybackCurve(Asset) && !FCNAGraphUtils::IsLatentAllowedIn(TargetGraph))
	{
		return false;
	}

	// ContextClass (spec 3.1, CNA012).
	const UClass* ContextClass = nullptr;
	if (Asset)
	{
		ContextClass = Asset->ContextClass.Get();
	}
	else if (!TemplateData.ContextClassPath.IsEmpty())
	{
		ContextClass = FindObject<UClass>(nullptr, *TemplateData.ContextClassPath);
	}
	if (ContextClass && !FCNAGraphUtils::DoesBlueprintSatisfyContext(Blueprint, ContextClass))
	{
		return false;
	}
	return true;
}

bool UK2Node_CustomNode::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	if (!MyPin || !OtherPin)
	{
		return false;
	}

	// A hidden pin cannot be connected (spec 4.6).
	if (MyPin->bHidden && MyPin->SubPins.Num() == 0)
	{
		OutReason = LOCTEXT("HiddenPinDisallowed", "The pin is hidden and cannot be connected.").ToString();
		return true;
	}

	// Wildcard pins keep the container of the signature: Array of Wildcard only accepts arrays (spec 4.5).
	if (IsSignatureWildcardPin(MyPin) && FCNAPinUtils::IsWildcardType(MyPin->PinType) && !FCNAPinUtils::IsWildcardType(OtherPin->PinType)
		&& MyPin->PinType.ContainerType != OtherPin->PinType.ContainerType)
	{
		OutReason = LOCTEXT("WildcardContainerDisallowed", "The container of the wire does not match the container of the Wildcard pin.").ToString();
		return true;
	}

	if (FCNAPinUtils::IsExecPin(MyPin))
	{
		return false;
	}

	UClass* ClassFilter = nullptr;
	switch (GetEffectiveConnectionRule(MyPin, ClassFilter))
	{
	case ECNAConnectionRule::SameTypeOnly:
		if (!FCNAPinUtils::AreTypesIdentical(MyPin->PinType, OtherPin->PinType))
		{
			OutReason = FText::Format(LOCTEXT("SameTypeOnlyDisallowed", "Only wires of type {0} can be connected."), FCNAPinUtils::GetTypeText(MyPin->PinType)).ToString();
			return true;
		}
		break;

	case ECNAConnectionRule::ClassFilter:
		if (ClassFilter)
		{
			const UClass* OtherClass = Cast<UClass>(OtherPin->PinType.PinSubCategoryObject.Get());
			if (!FCNAPinUtils::IsReferenceCategory(OtherPin->PinType.PinCategory) || !OtherClass || !OtherClass->IsChildOf(ClassFilter))
			{
				OutReason = FText::Format(LOCTEXT("ClassFilterDisallowed", "Only references to {0} or its children can be connected."), ClassFilter->GetDisplayNameText()).ToString();
				return true;
			}
		}
		break;

	default:
		break;
	}
	return false;
}

void UK2Node_CustomNode::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	if (!Pin || bReconstructing)
	{
		return;
	}

	// Wildcard pins: the first wire resolves the type, the last removed wire resets it (spec 4.5).
	if (IsSignatureWildcardPin(Pin) && UpdateResolvedWildcardFromLinks())
	{
		Modify();
		RefreshWildcardTypes();
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}

	if (CNACustomNodeLocal::CanRaiseEvents(this) && FCNABehaviorHost::IsAvailable())
	{
		const UEdGraphPin* Other = Pin->LinkedTo.Num() > 0 ? Pin->LinkedTo.Last() : nullptr;
		const FEdGraphPinType OtherType = Other ? Other->PinType : FEdGraphPinType();
		const UEdGraphPin* TopPin = Pin;
		while (TopPin->ParentPin)
		{
			TopPin = TopPin->ParentPin;
		}
		FCNABehaviorHost::Get().NotifyWireChanged(this, TopPin->PinName, Pin->LinkedTo.Num() > 0, OtherType);
	}
}

void UK2Node_CustomNode::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	if (Pin && !bReconstructing && CNACustomNodeLocal::CanRaiseEvents(this) && FCNABehaviorHost::IsAvailable())
	{
		FCNABehaviorHost::Get().NotifyPinValueChanged(this, Pin->PinName, FCNAPinUtils::GetValueAsString(Pin));
	}
}

void UK2Node_CustomNode::PostPlacedNewNode()
{
	Super::PostPlacedNewNode();

	if (UCustomNodeAsset* Asset = GetCustomNodeAsset())
	{
		// DefaultComment appears as a comment bubble (spec 3.1).
		if (!Asset->DefaultComment.IsEmpty())
		{
			NodeComment = Asset->DefaultComment.ToString();
			bCommentBubbleVisible = true;
		}
		ApplyDevelopmentOnlyState(Asset->bDevelopmentOnly);
		bCanRenameNode = Asset->bAllowInstanceLabel;
	}

	// The spawner may call PostPlacedNewNode before AllocateDefaultPins; the event needs the pins (spec 2.4, item 1).
	if (Pins.Num() > 0)
	{
		FireNodePlaced();
	}
	else
	{
		bPendingPlacedEvent = true;
	}
}

void UK2Node_CustomNode::FireNodePlaced()
{
	if (CNACustomNodeLocal::CanRaiseEvents(this))
	{
		RefreshSplitPinState(false);
		FCNABehaviorHost::Get().NotifyNodePlaced(this);
	}
}

void UK2Node_CustomNode::PostPasteNode()
{
	Super::PostPasteNode();
	// Pasted data keeps DynamicPinCount, ResolvedWildcardType and InstanceOverrides (spec D6). Template data never
	// travels with the clipboard.
	TemplateData = FCNATemplateData();
	if (UCustomNodeAsset* Asset = GetCustomNodeAsset())
	{
		bCanRenameNode = Asset->bAllowInstanceLabel;
	}
}

void UK2Node_CustomNode::ApplyDevelopmentOnlyState(bool bDevelopmentOnly, bool bOnlyIfNotUserSet)
{
	// EnabledState and bUserSetEnabledState are private members of UEdGraphNode in 4.20; they are reflected, so
	// they are written through their properties (V5).
	static const FName EnabledStateName(TEXT("EnabledState"));
	static const FName UserSetName(TEXT("bUserSetEnabledState"));

	UProperty* StateProperty = FindField<UProperty>(UEdGraphNode::StaticClass(), EnabledStateName);
	if (!StateProperty)
	{
		UE_LOG(LogNodeFactory, Warning, TEXT("EnabledState not found on UEdGraphNode: Development Only is not applied (V5)."));
		return;
	}

	UBoolProperty* UserSetProperty = FindField<UBoolProperty>(UEdGraphNode::StaticClass(), UserSetName);
	if (bOnlyIfNotUserSet && UserSetProperty && UserSetProperty->GetPropertyValue_InContainer(this))
	{
		// The user chose Enabled, Disabled or Development Only in the node menu: the asset does not override it.
		return;
	}

	const int64 NewValue = static_cast<int64>(bDevelopmentOnly ? ENodeEnabledState::DevelopmentOnly : ENodeEnabledState::Enabled);
	void* ValuePtr = StateProperty->ContainerPtrToValuePtr<void>(this);
	if (UEnumProperty* EnumProperty = Cast<UEnumProperty>(StateProperty))
	{
		EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, NewValue);
	}
	else if (UByteProperty* ByteProperty = Cast<UByteProperty>(StateProperty))
	{
		ByteProperty->SetIntPropertyValue(ValuePtr, NewValue);
	}

	if (UserSetProperty)
	{
		// Not a user choice: the asset decides, so a later change of the asset flag can update the node.
		UserSetProperty->SetPropertyValue_InContainer(this, false);
	}
}

// Behavior actions (spec 6.3)

bool UK2Node_CustomNode::SetPinVisibilityFromBehavior(FName PinName, bool bVisible, FText& OutError)
{
	UEdGraphPin* Pin = FCNAPinUtils::FindTopLevelPin(this, PinName);
	if (!Pin)
	{
		OutError = LOCTEXT("BehaviorPinNotFound", "the node has no pin with this name");
		return false;
	}
	if (!bVisible && FCNAPinUtils::IsPinOrSubPinLinked(Pin))
	{
		OutError = LOCTEXT("BehaviorHideConnected", "a connected pin cannot be hidden");
		return false;
	}

	Modify();
	FCNAInstanceOverride& InstanceOverride = FindOrAddInstanceOverride(PinName);
	InstanceOverride.bOverrideVisibility = true;
	InstanceOverride.bVisible = bVisible;

	Pin->Modify();
	if (Pin->SubPins.Num() > 0)
	{
		// A split pin stays hidden itself; its sub pins carry the visibility.
		for (UEdGraphPin* SubPin : Pin->SubPins)
		{
			SubPin->Modify();
			SubPin->bHidden = !bVisible;
		}
	}
	else
	{
		Pin->bHidden = !bVisible;
	}
	bBehaviorChangedPins = true;
	return true;
}

bool UK2Node_CustomNode::RenamePinFromBehavior(FName PinName, const FText& NewDisplayName, FText& OutError)
{
	UEdGraphPin* Pin = FCNAPinUtils::FindTopLevelPin(this, PinName);
	if (!Pin)
	{
		OutError = LOCTEXT("BehaviorPinNotFound", "the node has no pin with this name");
		return false;
	}

	Modify();
	FCNAInstanceOverride& InstanceOverride = FindOrAddInstanceOverride(PinName);
	// An empty name returns to the name of the asset.
	InstanceOverride.bOverrideDisplayName = !NewDisplayName.IsEmpty();
	InstanceOverride.DisplayName = NewDisplayName;

	Pin->Modify();
	if (NewDisplayName.IsEmpty())
	{
		const FCNAPinOverride* Override = FCNAPinUtils::FindOverrideForPin(GetCustomNodeAsset(), PinName);
		const int32 DynamicIndex = FCNAPinUtils::GetDynamicIndex(PinName);
		Pin->PinFriendlyName = Override
			? (DynamicIndex != INDEX_NONE && Override->bDynamicGroup ? Override->FormatDynamicName(DynamicIndex) : Override->DisplayName)
			: FText::GetEmpty();
	}
	else
	{
		Pin->PinFriendlyName = NewDisplayName;
	}
	bBehaviorChangedPins = true;
	return true;
}

bool UK2Node_CustomNode::ChangeWildcardTypeFromBehavior(FName PinName, const FEdGraphPinType& NewType, FText& OutError)
{
	UEdGraphPin* Pin = FCNAPinUtils::FindTopLevelPin(this, PinName);
	if (!Pin)
	{
		OutError = LOCTEXT("BehaviorPinNotFound", "the node has no pin with this name");
		return false;
	}
	if (!IsSignatureWildcardPin(Pin))
	{
		OutError = LOCTEXT("BehaviorNotWildcard", "only Wildcard pins can change their type");
		return false;
	}
	if (NewType.PinCategory.IsNone() || FCNAPinUtils::IsExecType(NewType))
	{
		OutError = LOCTEXT("BehaviorInvalidType", "the new type is not a data type");
		return false;
	}
	if (FCNAPinUtils::IsTypeObjectMissing(NewType))
	{
		OutError = FText::Format(LOCTEXT("BehaviorMissingTypeObject", "the type {0} is incomplete"), CNACustomNodeLocal::DescribeType(NewType));
		return false;
	}

	const FEdGraphPinType Element = FCNAPinUtils::GetElementType(NewType);
	if (FCNAPinUtils::IsWildcardType(Element))
	{
		// Back to Wildcard is only possible without wires.
		for (const UEdGraphPin* Other : Pins)
		{
			if (Other && IsSignatureWildcardPin(Other) && Other->LinkedTo.Num() > 0)
			{
				OutError = LOCTEXT("BehaviorWildcardConnected", "connected Wildcard pins keep their resolved type");
				return false;
			}
		}
	}
	else
	{
		// Wires of Wildcard pins must stay valid with the new type.
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		for (const UEdGraphPin* Other : Pins)
		{
			if (!Other || Other->ParentPin || !IsSignatureWildcardPin(Other))
			{
				continue;
			}
			FEdGraphPinType Candidate = Other->PinType;
			Candidate.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			FCNAPinUtils::ApplyResolvedWildcard(Candidate, Element);
			for (const UEdGraphPin* Linked : Other->LinkedTo)
			{
				const bool bCompatible = Other->Direction == EGPD_Output
					? Schema->ArePinTypesCompatible(Candidate, Linked->PinType)
					: Schema->ArePinTypesCompatible(Linked->PinType, Candidate);
				if (!bCompatible)
				{
					OutError = FText::Format(LOCTEXT("BehaviorTypeBreaksWire", "the wire of {0} does not accept {1}"),
						FText::FromName(Other->PinName), CNACustomNodeLocal::DescribeType(Element));
					return false;
				}
			}
		}
	}

	Modify();
	ResolvedWildcardType = FCNAPinUtils::IsWildcardType(Element) ? FEdGraphPinType() : Element;
	RefreshWildcardTypes();
	bBehaviorChangedPins = true;
	return true;
}

void UK2Node_CustomNode::SetTitleOverrideFromBehavior(const FText& NewTitle)
{
	if (!TitleOverride.EqualTo(NewTitle))
	{
		Modify();
		TitleOverride = NewTitle;
		bBehaviorChangedPins = true;
	}
}

bool UK2Node_CustomNode::SetPinConnectionRuleFromBehavior(FName PinName, ECNAConnectionRule Rule, UClass* ClassFilter, FText& OutError)
{
	UEdGraphPin* Pin = FCNAPinUtils::FindTopLevelPin(this, PinName);
	if (!Pin)
	{
		OutError = LOCTEXT("BehaviorPinNotFound", "the node has no pin with this name");
		return false;
	}
	if (Rule == ECNAConnectionRule::ClassFilter && !ClassFilter)
	{
		OutError = LOCTEXT("BehaviorRuleNeedsClass", "the rule ClassFilter needs a class");
		return false;
	}

	Modify();
	FCNAInstanceOverride& InstanceOverride = FindOrAddInstanceOverride(PinName);
	InstanceOverride.bOverrideRule = true;
	InstanceOverride.Rule = Rule;
	InstanceOverride.ClassFilter = ClassFilter;

	// Existing wires that break the new rule are removed, as if they were connected now.
	TArray<UEdGraphPin*> Linked = Pin->LinkedTo;
	for (UEdGraphPin* Other : Linked)
	{
		FString Reason;
		if (Other && IsConnectionDisallowed(Pin, Other, Reason))
		{
			Pin->BreakLinkTo(Other);
		}
	}
	bBehaviorChangedPins = true;
	return true;
}

void UK2Node_CustomNode::SetBehaviorMessage(EMessageSeverity::Type Severity, const FString& Message)
{
	// The frame and the message of the node stay until the next event (V22).
	bHasCompilerMessage = true;
	ErrorType = Severity == EMessageSeverity::Error ? EMessageSeverity::Error : EMessageSeverity::Warning;
	ErrorMsg = Message;
	bBehaviorMessageActive = true;
	bBehaviorChangedPins = true;
}

void UK2Node_CustomNode::ClearBehaviorMessage()
{
	if (bBehaviorMessageActive)
	{
		bBehaviorMessageActive = false;
		bHasCompilerMessage = false;
		ErrorType = EMessageSeverity::Info;
		ErrorMsg.Empty();
		bBehaviorChangedPins = true;
	}
}

void UK2Node_CustomNode::BeginBehaviorChanges()
{
	if (BehaviorChangeDepth++ == 0)
	{
		bBehaviorChangedPins = false;
	}
}

void UK2Node_CustomNode::EndBehaviorChanges()
{
	BehaviorChangeDepth = FMath::Max(BehaviorChangeDepth - 1, 0);
	if (BehaviorChangeDepth == 0 && bBehaviorChangedPins)
	{
		bBehaviorChangedPins = false;
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
		if (!bIsPreviewNode)
		{
			if (UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(this))
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			}
		}
	}
}

void UK2Node_CustomNode::RefreshSplitPinState(bool bFireEvents)
{
	TArray<FName> Current;
	CNACustomNodeLocal::CollectSplitPins(this, Current);
	if (Current == SplitPinSnapshot)
	{
		return;
	}

	TArray<FName> NewlySplit;
	TArray<FName> Recombined;
	for (const FName& Name : Current)
	{
		if (!SplitPinSnapshot.Contains(Name))
		{
			NewlySplit.Add(Name);
		}
	}
	for (const FName& Name : SplitPinSnapshot)
	{
		if (!Current.Contains(Name))
		{
			Recombined.Add(Name);
		}
	}

	// The snapshot is instance data: the split itself was a transaction of the schema, this is a follow-up change.
	SplitPinSnapshot = Current;

	if (bFireEvents && CNACustomNodeLocal::CanRaiseEvents(this))
	{
		for (const FName& Name : NewlySplit)
		{
			FCNABehaviorHost::Get().NotifyPinSplit(this, Name);
		}
		for (const FName& Name : Recombined)
		{
			FCNABehaviorHost::Get().NotifyPinRecombined(this, Name);
		}
	}
}

int32 UK2Node_CustomNode::CountElementPins() const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if (!Asset)
	{
		return 0;
	}
	for (const FCNAPinOverride& Override : Asset->PinOverrides)
	{
		if (!Override.bDynamicGroup)
		{
			continue;
		}
		int32 Count = 0;
		for (const UEdGraphPin* Pin : Pins)
		{
			if (Pin && Pin->ParentPin == nullptr && FCNAPinUtils::GetDynamicIndex(Pin->PinName) != INDEX_NONE
				&& FCNAPinUtils::GetBaseName(Pin->PinName) == Override.PinName)
			{
				++Count;
			}
		}
		return Count;
	}
	return 0;
}

void UK2Node_CustomNode::GetElementPinNames(int32 Index, TArray<FName>& OutNames) const
{
	if (const UCustomNodeAsset* Asset = GetCustomNodeAsset())
	{
		for (const FCNAPinOverride& Override : Asset->PinOverrides)
		{
			if (Override.bDynamicGroup)
			{
				OutNames.Add(CNATypes::MakeDynamicPinName(Override.PinName, Index));
			}
		}
	}
}

// Reconstruction (spec 9.1)

FName UK2Node_CustomNode::RedirectPinName(FName OldName) const
{
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if (!Asset)
	{
		return OldName;
	}
	FName BaseName;
	int32 Index = INDEX_NONE;
	if (CNATypes::ParseDynamicPinName(OldName, BaseName, Index))
	{
		// "Items__2" of a renamed group "Items" -> "Entries" becomes "Entries__2".
		const FName NewBase = Asset->ResolvePinRedirect(BaseName);
		return NewBase == BaseName ? Asset->ResolvePinRedirect(OldName) : CNATypes::MakeDynamicPinName(NewBase, Index);
	}
	return Asset->ResolvePinRedirect(OldName);
}

void UK2Node_CustomNode::ReconstructNode()
{
	if (bReconstructing)
	{
		return;
	}

	UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if ((!Asset || !FCNAGraphUtils::HasValidExecuteGraph(Asset)) && Pins.Num() > 0)
	{
		// The asset or its macro is gone: the node keeps its pins and wires and reports CNA005 when the
		// Blueprint compiles (spec 2.4, item 6).
		CNA_VERBOSE(TEXT("%s keeps its pins: asset %s is missing or broken."), *GetName(), *GetAssetNameForMessages());
		return;
	}

	// An outdated node keeps its pins while its Blueprint loads: the update service asks the user first and the
	// compiler reports CNA006 until then (spec 9.1, items 1-3).
	const UBlueprint* OwnerBlueprint = FCNAGraphUtils::GetBlueprint(this);
	if (OwnerBlueprint && OwnerBlueprint->bIsRegeneratingOnLoad && IsOutdated() && Pins.Num() > 0)
	{
		CNA_VERBOSE(TEXT("%s is outdated (NodeVersion %d, AssetVersion %d); rebuild deferred to the update service."),
			*GetName(), NodeVersion, Asset ? Asset->AssetVersion : 0);
		return;
	}

	TGuardValue<bool> Guard(bReconstructing, true);

	TArray<FLinkSnapshot> Snapshot;
	TakeLinkSnapshot(Snapshot);
	ReconstructPositionalMatches.Reset();

	Super::ReconstructNode();

	ReconstructPositionalMatches.Reset();
	ReportLostLinks(Snapshot);

	if (Asset)
	{
		NodeVersion = Asset->AssetVersion;
		ApplyDevelopmentOnlyState(Asset->bDevelopmentOnly, true);
	}
}

void UK2Node_CustomNode::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	// AllocateDefaultPins and restoring of split pins.
	Super::ReallocatePinsDuringReconstruction(OldPins);

	// Third priority of spec 9.1, item 5: pins left without a name or redirect match are paired by type and
	// position, in the order of the pins of each direction.
	ReconstructPositionalMatches.Reset();
	TSet<FName> MatchedNewNames;
	TArray<UEdGraphPin*> UnmatchedOld;

	for (UEdGraphPin* OldPin : OldPins)
	{
		if (!OldPin || OldPin->ParentPin != nullptr || OldPin->bOrphanedPin)
		{
			continue;
		}
		const FName Redirected = RedirectPinName(OldPin->PinName);
		UEdGraphPin* NewPin = FCNAPinUtils::FindTopLevelPin(this, OldPin->PinName, OldPin->Direction);
		if (!NewPin && Redirected != OldPin->PinName)
		{
			NewPin = FCNAPinUtils::FindTopLevelPin(this, Redirected, OldPin->Direction);
		}
		if (!NewPin)
		{
			const FName Takeover = GetCurveTakeoverPinName(OldPin->PinName, OldPin->Direction);
			NewPin = Takeover.IsNone() ? nullptr : FCNAPinUtils::FindTopLevelPin(this, Takeover, OldPin->Direction);
		}
		if (NewPin)
		{
			MatchedNewNames.Add(NewPin->PinName);
		}
		else
		{
			UnmatchedOld.Add(OldPin);
		}
	}

	const EEdGraphPinDirection Directions[] = { EGPD_Input, EGPD_Output };
	for (EEdGraphPinDirection Direction : Directions)
	{
		TArray<UEdGraphPin*> UnmatchedNew;
		for (UEdGraphPin* Pin : Pins)
		{
			if (Pin && Pin->ParentPin == nullptr && Pin->Direction == Direction && !MatchedNewNames.Contains(Pin->PinName))
			{
				UnmatchedNew.Add(Pin);
			}
		}
		for (UEdGraphPin* OldPin : UnmatchedOld)
		{
			if (OldPin->Direction != Direction)
			{
				continue;
			}
			for (int32 Index = 0; Index < UnmatchedNew.Num(); ++Index)
			{
				UEdGraphPin* Candidate = UnmatchedNew[Index];
				if (FCNAPinUtils::AreTypesIdentical(OldPin->PinType, Candidate->PinType))
				{
					ReconstructPositionalMatches.Add(OldPin->PinName, Candidate->PinName);
					CNA_VERBOSE(TEXT("%s: pin %s matched to %s by type and position."), *GetName(), *OldPin->PinName.ToString(), *Candidate->PinName.ToString());
					UnmatchedNew.RemoveAt(Index);
					break;
				}
			}
		}
	}
}

UK2Node::ERedirectType UK2Node_CustomNode::DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const
{
	// First priority: the name (and the K2 redirect tables of the engine).
	const ERedirectType EngineMatch = Super::DoPinsMatchForReconstruction(NewPin, NewPinIndex, OldPin, OldPinIndex);
	if (EngineMatch != ERedirectType_None)
	{
		return EngineMatch;
	}
	if (!NewPin || !OldPin || NewPin->Direction != OldPin->Direction)
	{
		return ERedirectType_None;
	}

	// Sub pins of split structures follow their parents: "Old_X" matches "New_X" when Old matches New.
	if (OldPin->ParentPin || NewPin->ParentPin)
	{
		if (!OldPin->ParentPin || !NewPin->ParentPin
			|| DoPinsMatchForReconstruction(NewPin->ParentPin, INDEX_NONE, OldPin->ParentPin, INDEX_NONE) == ERedirectType_None)
		{
			return ERedirectType_None;
		}
		const FString OldSuffix = OldPin->PinName.ToString().RightChop(OldPin->ParentPin->PinName.ToString().Len());
		const FString NewSuffix = NewPin->PinName.ToString().RightChop(NewPin->ParentPin->PinName.ToString().Len());
		return OldSuffix == NewSuffix ? ERedirectType_Name : ERedirectType_None;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	const bool bTypesFit = FCNAPinUtils::IsExecPin(NewPin) == FCNAPinUtils::IsExecPin(OldPin)
		&& (FCNAPinUtils::AreTypesIdentical(OldPin->PinType, NewPin->PinType) || Schema->ArePinTypesCompatible(OldPin->PinType, NewPin->PinType));

	// Second priority: PinRedirects of the asset (V11).
	const FName Redirected = RedirectPinName(OldPin->PinName);
	if (Redirected != OldPin->PinName && Redirected == NewPin->PinName && bTypesFit)
	{
		return ERedirectType_Name;
	}

	// The exec flow of the macro and Play / Update of a Playback curve hand their wires to each other.
	if (bTypesFit && FCNAPinUtils::IsExecPin(NewPin) && !FCNAPinUtils::FindTopLevelPin(this, OldPin->PinName, OldPin->Direction)
		&& GetCurveTakeoverPinName(OldPin->PinName, OldPin->Direction) == NewPin->PinName)
	{
		return ERedirectType_Name;
	}

	// Third priority: type and position, computed in ReallocatePinsDuringReconstruction.
	if (const FName* Positional = ReconstructPositionalMatches.Find(OldPin->PinName))
	{
		if (*Positional == NewPin->PinName)
		{
			return ERedirectType_Name;
		}
	}
	return ERedirectType_None;
}

void UK2Node_CustomNode::GetRedirectPinNames(const UEdGraphPin& Pin, TArray<FString>& RedirectPinNames) const
{
	Super::GetRedirectPinNames(Pin, RedirectPinNames);
	// The engine looks these names up in its own redirect tables; the asset redirects are matched in
	// DoPinsMatchForReconstruction, the names are added for completeness of tooling that lists them.
	const FName Redirected = RedirectPinName(Pin.PinName);
	if (Redirected != Pin.PinName)
	{
		RedirectPinNames.AddUnique(Redirected.ToString());
	}
}

void UK2Node_CustomNode::PostReconstructNode()
{
	Super::PostReconstructNode();

	// Wires restored by the rebuild may resolve or release the Wildcard type.
	if (HasWildcardPins())
	{
		UpdateResolvedWildcardFromLinks();
		RefreshWildcardTypes();
	}

	// Split state is compared after every rebuild (spec 6.2, V15). Splits made without a rebuild are detected by
	// SGraphNode_NodeFactory when the graph panel refreshes.
	RefreshSplitPinState(true);
}

void UK2Node_CustomNode::TakeLinkSnapshot(TArray<FLinkSnapshot>& OutSnapshot) const
{
	for (const UEdGraphPin* Pin : Pins)
	{
		if (!Pin || Pin->LinkedTo.Num() == 0)
		{
			continue;
		}
		FLinkSnapshot& Entry = OutSnapshot[OutSnapshot.AddDefaulted()];
		Entry.PinName = Pin->PinName;
		Entry.Direction = Pin->Direction;
		for (const UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (Linked && Linked->GetOwningNodeUnchecked())
			{
				Entry.Links.Add(TPair<TWeakObjectPtr<UEdGraphNode>, FName>(Linked->GetOwningNodeUnchecked(), Linked->PinName));
			}
		}
	}
}

void UK2Node_CustomNode::ReportLostLinks(const TArray<FLinkSnapshot>& Snapshot)
{
	LastLostWirePins.Reset();
	for (const FLinkSnapshot& Entry : Snapshot)
	{
		for (const TPair<TWeakObjectPtr<UEdGraphNode>, FName>& Link : Entry.Links)
		{
			const UEdGraphNode* OtherNode = Link.Key.Get();
			if (!OtherNode)
			{
				continue;
			}
			bool bKept = false;
			for (const UEdGraphPin* Pin : Pins)
			{
				if (!Pin || Pin->bOrphanedPin || Pin->Direction != Entry.Direction)
				{
					continue;
				}
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked && Linked->GetOwningNodeUnchecked() == OtherNode && Linked->PinName == Link.Value)
					{
						bKept = true;
						break;
					}
				}
				if (bKept)
				{
					break;
				}
			}
			if (!bKept)
			{
				LastLostWirePins.AddUnique(Entry.PinName);
			}
		}
	}

	if (LastLostWirePins.Num() == 0 || bIsPreviewNode || !CNACustomNodeLocal::CanRaiseEvents(this))
	{
		return;
	}
	const UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(this);
	for (const FName& PinName : LastLostWirePins)
	{
		FFormatNamedArguments Arguments = FCNAMessages::AssetArgs(GetCustomNodeAsset());
		Arguments.Add(TEXT("Blueprint"), FText::FromString(Blueprint ? Blueprint->GetName() : FString(TEXT("?"))));
		// Curve pins have internal names; the message shows their label.
		Arguments.Add(TEXT("Pin"), FCNACurveUtils::DescribePinName(GetCustomNodeAsset(), PinName));
		FCNAMessages::ReportToLog(ECNAMessage::CNA016, Arguments, this);
	}
}

void UK2Node_CustomNode::RefreshNode()
{
	const FScopedTransaction Transaction(LOCTEXT("RefreshNodeTransaction", "Refresh Custom Node"));
	Modify();
	ReconstructNode();
	if (UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(this))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

// Compilation (spec 5)

void UK2Node_CustomNode::ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);

	// The compiler may validate a node before or after ExpandNode; the diagnostics are reported once.
	if (bCompileDiagnosticsReported)
	{
		return;
	}
	bCompileDiagnosticsReported = true;

	FCNADiagnosticList Diagnostics;
	FCNAValidator::ValidateNodeForCompilation(this, Diagnostics);
	bCompileHasErrors = Diagnostics.HasErrors();
	Diagnostics.FlushToCompilerLog(MessageLog, this);
}

void UK2Node_CustomNode::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);
	FCNAExpander::Expand(this, CompilerContext, SourceGraph);
}

bool UK2Node_CustomNode::HasExternalDependencies(TArray<UStruct*>* OptionalOutput) const
{
	bool bResult = false;
	for (const UEdGraphPin* Pin : Pins)
	{
		UStruct* Dependency = Pin ? Cast<UStruct>(Pin->PinType.PinSubCategoryObject.Get()) : nullptr;
		UClass* DependencyClass = Cast<UClass>(Dependency);
		// Native types are always available; Blueprint types define the compile order.
		const bool bIsBlueprintType = Dependency && (DependencyClass ? DependencyClass->ClassGeneratedBy != nullptr : !Dependency->IsNative());
		if (bIsBlueprintType)
		{
			bResult = true;
			if (OptionalOutput)
			{
				OptionalOutput->AddUnique(Dependency);
			}
		}
	}
	const bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	return bSuperResult || bResult;
}

// Menus and editing

void UK2Node_CustomNode::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// One spawner per asset, built from the registry tags without loading (spec 9.8, V9).
	FCNANodeMenu::RegisterMenuActions(ActionRegistrar, GetClass());
}

void UK2Node_CustomNode::GetContextMenuActions(const FGraphNodeContextMenuBuilder& Context) const
{
	Super::GetContextMenuActions(Context);
	if (Context.bIsDebugging || !Context.MenuBuilder)
	{
		return;
	}

	UK2Node_CustomNode* MutableThis = const_cast<UK2Node_CustomNode*>(this);
	TWeakObjectPtr<UK2Node_CustomNode> WeakNode(MutableThis);
	UCustomNodeAsset* Asset = GetCustomNodeAsset();

	Context.MenuBuilder->BeginSection(TEXT("CustomNode"), LOCTEXT("CustomNodeMenuSection", "Custom Node"));
	{
		if (Asset)
		{
			TWeakObjectPtr<UCustomNodeAsset> WeakAsset(Asset);
			Context.MenuBuilder->AddMenuEntry(
				LOCTEXT("OpenAssetMenu", "Open Custom Node Asset"),
				LOCTEXT("OpenAssetMenuTooltip", "Opens the asset window of this node (V10)."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("ContentBrowser.AssetActions.Edit")),
				FUIAction(FExecuteAction::CreateLambda([WeakAsset]()
				{
					if (UCustomNodeAsset* Target = WeakAsset.Get())
					{
						FAssetEditorManager::Get().OpenEditorForAsset(Target);
					}
				})));
		}

		if (HasDynamicPins())
		{
			Context.MenuBuilder->AddMenuEntry(
				LOCTEXT("AddElementMenu", "Add Pin"),
				LOCTEXT("AddElementMenuTooltip", "Adds an element pin."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("Plus")),
				FUIAction(
					FExecuteAction::CreateLambda([WeakNode]() { if (UK2Node_CustomNode* Node = WeakNode.Get()) { Node->AddDynamicElement(); } }),
					FCanExecuteAction::CreateLambda([WeakNode]() { const UK2Node_CustomNode* Node = WeakNode.Get(); return Node && Node->CanAddDynamicElement(); })));

			const UEdGraphPin* ContextPin = Context.Pin;
			const UEdGraphPin* TopPin = ContextPin;
			while (TopPin && TopPin->ParentPin)
			{
				TopPin = TopPin->ParentPin;
			}
			if (TopPin && FCNAPinUtils::GetDynamicIndex(TopPin->PinName) != INDEX_NONE)
			{
				const int32 Index = FCNAPinUtils::GetDynamicIndex(TopPin->PinName);
				Context.MenuBuilder->AddMenuEntry(
					LOCTEXT("RemoveElementMenu", "Remove Pin"),
					LOCTEXT("RemoveElementMenuTooltip", "Removes this element pin; its wire is broken."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("Cross")),
					FUIAction(
						FExecuteAction::CreateLambda([WeakNode, Index]() { if (UK2Node_CustomNode* Node = WeakNode.Get()) { Node->RemoveDynamicElement(Index); } }),
						FCanExecuteAction::CreateLambda([WeakNode]() { const UK2Node_CustomNode* Node = WeakNode.Get(); return Node && Node->CanRemoveDynamicElement(); })));
			}
		}

		Context.MenuBuilder->AddMenuEntry(
			LOCTEXT("RefreshNodeMenu", "Refresh Node"),
			LOCTEXT("RefreshNodeMenuTooltip", "Rebuilds the pins from the asset; wires are kept where possible."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("BlueprintEditor.RefreshAllNodes")),
			FUIAction(FExecuteAction::CreateLambda([WeakNode]() { if (UK2Node_CustomNode* Node = WeakNode.Get()) { Node->RefreshNode(); } })));

		if (InstanceOverrides.Num() > 0 || !TitleOverride.IsEmpty())
		{
			Context.MenuBuilder->AddMenuEntry(
				LOCTEXT("ResetBehaviorMenu", "Reset Behavior Changes"),
				LOCTEXT("ResetBehaviorMenuTooltip", "Removes pin visibility, names, rules and the title set by the Behavior of this node."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([WeakNode]() { if (UK2Node_CustomNode* Node = WeakNode.Get()) { Node->ResetInstanceOverrides(); } })));
		}
	}
	Context.MenuBuilder->EndSection();
}

UObject* UK2Node_CustomNode::GetJumpTargetForDoubleClick() const
{
	// Double click opens the asset window at the macro Execute (V10).
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	return Asset ? Asset->FindExecuteGraph() : nullptr;
}

void UK2Node_CustomNode::OnRenameNode(const FString& NewName)
{
	Modify();
	InstanceLabel = FText::FromString(NewName.TrimStartAndEnd());
	if (UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(this))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

TSharedPtr<INameValidatorInterface> UK2Node_CustomNode::MakeNameValidator() const
{
	// Instance labels are free text; several nodes may carry the same label.
	return MakeShareable(new FDummyNameValidator(EValidatorResult::Ok));
}

void UK2Node_CustomNode::DestroyNode()
{
	if (FCNABehaviorHost::IsAvailable())
	{
		FCNABehaviorHost::Get().ForgetNode(this);
	}
	Super::DestroyNode();
}

// UObject

void UK2Node_CustomNode::PostLoad()
{
	Super::PostLoad();
	if (CNAAsset)
	{
		AssetPath = FSoftObjectPath(CNAAsset);
	}
	else if (AssetPath.IsValid())
	{
		CNAAsset = Cast<UCustomNodeAsset>(AssetPath.ResolveObject());
	}
}

void UK2Node_CustomNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UK2Node_CustomNode, DynamicPinCount))
	{
		// The Details panel already opened the transaction (spec 4.4, item 2).
		SetDynamicPinCount(DynamicPinCount, true);
		return;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UK2Node_CustomNode, InstanceLabel))
	{
		if (UEdGraph* Graph = GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}

	// Properties of the node raise OnPinValueChanged with the property name (spec 6.2).
	if (PropertyChangedEvent.Property && CNACustomNodeLocal::CanRaiseEvents(this) && FCNABehaviorHost::IsAvailable())
	{
		FString Value;
		const UProperty* Property = PropertyChangedEvent.Property;
		Property->ExportTextItem(Value, Property->ContainerPtrToValuePtr<void>(this), nullptr, this, PPF_None);
		FCNABehaviorHost::Get().NotifyPinValueChanged(this, PropertyName, Value);
	}
}

void UK2Node_CustomNode::PostEditUndo()
{
	Super::PostEditUndo();
	if (UEdGraph* Graph = GetGraph())
	{
		Graph->NotifyGraphChanged();
	}
}

#undef LOCTEXT_NAMESPACE
