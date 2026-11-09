// Copyright (c) NodeFactory. Curve Value: reads a Curve body widget inside the macro Execute of a Custom Node Asset.

#include "K2Node_CNACurveValue.h"
#include "CNACurveUtils.h"
#include "CNAGraphUtils.h"
#include "CNANodeMenu.h"
#include "CNAPinUtils.h"
#include "CustomNodeAsset.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditorSettings.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "ScopedTransaction.h"
#include "Textures/SlateIcon.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

const FName UK2Node_CNACurveValue::ValuePinName(TEXT("Value"));
const FName UK2Node_CNACurveValue::TimePinName(TEXT("Time"));

UK2Node_CNACurveValue::UK2Node_CNACurveValue(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CachedValueType(ECNACurveValueType::Float)
{
}

UEdGraphPin* UK2Node_CNACurveValue::GetValuePin() const
{
	return FindPin(ValuePinName, EGPD_Output);
}

UEdGraphPin* UK2Node_CNACurveValue::GetTimePin() const
{
	return FindPin(TimePinName, EGPD_Output);
}

UCustomNodeAsset* UK2Node_CNACurveValue::GetOwningAsset() const
{
	return FCNAGraphUtils::GetOwningAsset(this);
}

const FCNABodyWidget* UK2Node_CNACurveValue::FindCurveIn(const UCustomNodeAsset* Asset) const
{
	return Asset ? Asset->FindCurveWidget(CurveGuid, CurveName) : nullptr;
}

const FCNABodyWidget* UK2Node_CNACurveValue::FindCurve() const
{
	return FindCurveIn(GetOwningAsset());
}

ECNACurveValueType UK2Node_CNACurveValue::GetValueType() const
{
	const FCNABodyWidget* Widget = FindCurve();
	return Widget ? Widget->Curve.ValueType : CachedValueType;
}

FText UK2Node_CNACurveValue::GetCurveDisplayName() const
{
	if (const FCNABodyWidget* Widget = FindCurve())
	{
		return FCNACurveUtils::GetCurveDisplayName(*Widget);
	}
	return CurveName.IsNone() ? LOCTEXT("CurveValue.None", "<none>") : FText::FromName(CurveName);
}

bool UK2Node_CNACurveValue::NeedsRefresh() const
{
	const FCNABodyWidget* Widget = FindCurve();
	if (!Widget)
	{
		return false;
	}
	const UEdGraphPin* ValuePin = GetValuePin();
	return !ValuePin || Widget->PinName != CurveName || Widget->WidgetGuid != CurveGuid
		|| !FCNAPinUtils::AreTypesIdentical(ValuePin->PinType, FCNACurveUtils::GetValuePinType(Widget->Curve.ValueType));
}

void UK2Node_CNACurveValue::SyncCacheFromCurve()
{
	if (const FCNABodyWidget* Widget = FindCurve())
	{
		CurveGuid = Widget->WidgetGuid;
		CurveName = Widget->PinName;
		CachedValueType = Widget->Curve.ValueType;
	}
}

void UK2Node_CNACurveValue::InitCurve(const FCNABodyWidget& Widget)
{
	CurveGuid = Widget.WidgetGuid;
	CurveName = Widget.PinName;
	CachedValueType = Widget.Curve.ValueType;
}

void UK2Node_CNACurveValue::SetCurve(const FCNABodyWidget& Widget)
{
	if (Widget.WidgetGuid == CurveGuid && Widget.PinName == CurveName)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("CurveValue.ChangeTransaction", "Change Curve"));
	Modify();
	InitCurve(Widget);
	ReconstructNode();
	if (UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(this))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

void UK2Node_CNACurveValue::GetSelectableCurves(TArray<const FCNABodyWidget*>& OutCurves) const
{
	OutCurves.Reset();
	if (const UCustomNodeAsset* Asset = GetOwningAsset())
	{
		Asset->GetCurveWidgets(OutCurves);
	}
}

void UK2Node_CNACurveValue::AllocateDefaultPins()
{
	SyncCacheFromCurve();

	UEdGraphPin* ValuePin = CreatePin(EGPD_Output, FCNACurveUtils::GetValuePinType(GetValueType()), ValuePinName);
	ValuePin->PinFriendlyName = LOCTEXT("CurveValue.ValuePin", "Value");
	ValuePin->PinToolTip = LOCTEXT("CurveValue.ValuePinTooltip", "Value of the curve at the current time of the node instance.").ToString();

	FEdGraphPinType TimeType;
	TimeType.PinCategory = UEdGraphSchema_K2::PC_Float;
	UEdGraphPin* TimePin = CreatePin(EGPD_Output, TimeType, TimePinName);
	TimePin->PinFriendlyName = LOCTEXT("CurveValue.TimePin", "Time");
	TimePin->PinToolTip = LOCTEXT("CurveValue.TimePinTooltip", "Time at which the curve is read: the Time pin of the instance or the playback position.").ToString();

	Super::AllocateDefaultPins();
}

void UK2Node_CNACurveValue::ReconstructNode()
{
	// A missing curve keeps the pins and wires; the custom node reports CNA023 when it compiles.
	if (!FindCurve() && Pins.Num() > 0)
	{
		return;
	}
	SyncCacheFromCurve();
	Super::ReconstructNode();
}

FText UK2Node_CNACurveValue::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::FullTitle)
	{
		return FText::Format(LOCTEXT("CurveValue.FullTitle", "Curve Value\n{0}"), GetCurveDisplayName());
	}
	if (TitleType == ENodeTitleType::MenuTitle && CurveGuid.IsValid())
	{
		return FText::Format(LOCTEXT("CurveValue.MenuTitle", "Curve Value: {0}"), GetCurveDisplayName());
	}
	return LOCTEXT("CurveValue.Title", "Curve Value");
}

FText UK2Node_CNACurveValue::GetTooltipText() const
{
	const FCNABodyWidget* Widget = FindCurve();
	if (!Widget)
	{
		return FText::Format(LOCTEXT("CurveValue.MissingTooltip", "The curve {0} does not exist in this asset (CNA023)."), GetCurveDisplayName());
	}
	const FText Mode = Widget->Curve.Mode == ECNACurveMode::Playback
		? LOCTEXT("CurveValue.ModePlayback", "the playback position of the node")
		: LOCTEXT("CurveValue.ModeTime", "the Time pin of the node");
	return FText::Format(LOCTEXT("CurveValue.Tooltip", "Value of the curve {0} ({1}) at {2}.\nIn a compiled Blueprint the curve is evaluated by engine nodes only."),
		FCNACurveUtils::GetCurveDisplayName(*Widget), FCNAPinUtils::GetTypeText(FCNACurveUtils::GetValuePinType(Widget->Curve.ValueType)), Mode);
}

FLinearColor UK2Node_CNACurveValue::GetNodeTitleColor() const
{
	return GetDefault<UGraphEditorSettings>()->PureFunctionCallNodeTitleColor;
}

FSlateIcon UK2Node_CNACurveValue::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	// V-check: the Timeline brush of EditorStyle in 4.20.3; the function icon when it is missing.
	static const FName TimelineIcon(TEXT("GraphEditor.Timeline_16x"));
	if (FEditorStyle::Get().GetOptionalBrush(TimelineIcon, nullptr, nullptr) != nullptr)
	{
		return FSlateIcon(FEditorStyle::GetStyleSetName(), TimelineIcon);
	}
	return FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("Kismet.AllClasses.FunctionIcon"));
}

bool UK2Node_CNACurveValue::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	if (!Super::IsCompatibleWithGraph(TargetGraph))
	{
		return false;
	}
	// Only inside the Execution Graph of a Custom Node Asset that has a curve.
	const UCustomNodeAsset* GraphOwner = FCNAGraphUtils::GetExecuteGraphOwner(TargetGraph);
	if (!GraphOwner)
	{
		return false;
	}
	// A menu entry bound to a curve belongs to its own asset.
	if (CurveGuid.IsValid())
	{
		return GraphOwner->FindCurveWidget(CurveGuid) != nullptr;
	}
	TArray<const FCNABodyWidget*> Curves;
	GraphOwner->GetCurveWidgets(Curves);
	return Curves.Num() > 0;
}

void UK2Node_CNACurveValue::GetContextMenuActions(const FGraphNodeContextMenuBuilder& Context) const
{
	Super::GetContextMenuActions(Context);
	if (Context.bIsDebugging || !Context.MenuBuilder)
	{
		return;
	}

	TArray<const FCNABodyWidget*> Curves;
	GetSelectableCurves(Curves);
	if (Curves.Num() == 0)
	{
		return;
	}

	TWeakObjectPtr<UK2Node_CNACurveValue> WeakNode(const_cast<UK2Node_CNACurveValue*>(this));
	Context.MenuBuilder->BeginSection(TEXT("CNACurveValue"), LOCTEXT("CurveValue.MenuSection", "Curve"));
	for (const FCNABodyWidget* Widget : Curves)
	{
		const FGuid Guid = Widget->WidgetGuid;
		const FText Name = FCNACurveUtils::GetCurveDisplayName(*Widget);
		Context.MenuBuilder->AddMenuEntry(
			FText::Format(LOCTEXT("CurveValue.UseCurveMenu", "Use {0}"), Name),
			FText::Format(LOCTEXT("CurveValue.UseCurveMenuTooltip", "Read the curve {0} ({1})."), Name,
				FCNAPinUtils::GetTypeText(FCNACurveUtils::GetValuePinType(Widget->Curve.ValueType))),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakNode, Guid]()
				{
					UK2Node_CNACurveValue* Node = WeakNode.Get();
					const UCustomNodeAsset* Asset = Node ? Node->GetOwningAsset() : nullptr;
					if (const FCNABodyWidget* Target = Asset ? Asset->FindCurveWidget(Guid) : nullptr)
					{
						Node->SetCurve(*Target);
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([WeakNode, Guid]()
				{
					const UK2Node_CNACurveValue* Node = WeakNode.Get();
					return Node && Node->CurveGuid == Guid;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	Context.MenuBuilder->EndSection();
}

void UK2Node_CNACurveValue::PostPasteNode()
{
	Super::PostPasteNode();
	// Pasted into another asset: the Guid is unknown there, the name may still match a curve.
	const UCustomNodeAsset* Asset = GetOwningAsset();
	if (Asset && !Asset->FindCurveWidget(CurveGuid))
	{
		if (const FCNABodyWidget* ByName = Asset->FindCurveWidget(FGuid(), CurveName))
		{
			InitCurve(*ByName);
		}
	}
}

FText UK2Node_CNACurveValue::GetKeywords() const
{
	return LOCTEXT("CurveValue.Keywords", "curve value timeline track time");
}

FText UK2Node_CNACurveValue::GetMenuCategory() const
{
	return LOCTEXT("CurveValue.MenuCategory", "Curves");
}

void UK2Node_CNACurveValue::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	FCNANodeMenu::RegisterCurveValueActions(ActionRegistrar, GetClass());
}

void UK2Node_CNACurveValue::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);
	// FCNAExpander replaces the node and leaves it without wires. A wired node here is outside the Execution Graph
	// of a Custom Node Asset; an unwired one has no effect and is pruned by the compiler.
	const bool bHasWires = Pins.ContainsByPredicate([](const UEdGraphPin* Pin) { return Pin && Pin->LinkedTo.Num() > 0; });
	if (bHasWires)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("CurveValue.OutsideAsset", "@@ can only be used inside the Execution Graph of a Custom Node Asset.").ToString(), this);
		BreakAllNodeLinks();
	}
}

#undef LOCTEXT_NAMESPACE
