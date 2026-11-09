// Copyright (c) NodeFactory. Pin widgets of custom nodes: own pin and label colors (spec 4.7, V25).

#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"
#include "EditorStyleSet.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

class UEdGraphPin;
class UK2Node_CustomNode;
struct FSlateBrush;

/**
 * The typed pin widgets of GraphEditor (KismetPins/SGraphPinBool.h and the others) are expected to be exported
 * in 4.20.3 with the constructor (FArguments, UEdGraphPin*) (V25, V-check). Set to 0 if they are not: pins of
 * custom nodes then use the engine widgets and the colors of their types (the fallback of V25).
 */
#ifndef CNA_WITH_STYLED_PINS
#define CNA_WITH_STYLED_PINS 1
#endif

/**
 * SGraphPin::GetLabelWidget(const FName&) is expected to be virtual in 4.20.3 (V-check: Niagara's
 * TNiagaraGraphPinEditableName overrides it). Set to 0 if it is not: namespace labels of the Niagara style are then
 * drawn as plain labels.
 */
#ifndef CNA_WITH_PIN_LABEL_WIDGET
#define CNA_WITH_PIN_LABEL_WIDGET 1
#endif

/** How the icon of a pin is drawn. */
enum class ECNAPinShape : uint8
{
	/** The icon of the pin type. */
	Default,
	/** Pose pin of the Animation Blueprint (SGraphPinPose). */
	Pose,
	/** Parameter Map pin of Niagara: the round pin in the execution color. */
	ParameterMap
};

/** Colors and value fields of the pins of custom nodes; every pin widget of the plugin asks here. */
struct NODEFACTORYEDITOR_API FCNAPinStyle
{
	/** The custom node that owns the pin, nullptr for pins of other nodes. */
	static const UK2Node_CustomNode* GetCustomNode(const UEdGraphPin* Pin);

	/** PinColor of the override (spec 4.7). False keeps the color of the type. */
	static bool GetPinColor(const UEdGraphPin* Pin, FLinearColor& OutColor);

	/** LabelColor of the override or PinLabelColor of the asset. */
	static bool GetLabelColor(const UEdGraphPin* Pin, FLinearColor& OutColor);

	/** A body widget of the node edits the value of this pin, so the pin hides its own field (spec 3.2). */
	static bool HasBodyWidget(const UEdGraphPin* Pin);

	/** Pose look (bExecPinsAsPosePins, bDrawAsPosePin) or Parameter Map look (bExecPinsAsParameterMap) of the pin. */
	static ECNAPinShape GetPinShape(const UEdGraphPin* Pin);

	/** Icon of a pin with a shape other than Default; nullptr keeps the icon of the pin widget. */
	static const FSlateBrush* GetShapedPinIcon(const UEdGraphPin* Pin, bool bConnected, bool bHovered);

	/** Niagara style with bNiagaraNamespaceLabels: "Module.Speed" draws "Module." dimmed. */
	static bool UseNamespaceLabel(const UEdGraphPin* Pin);

	/** "Module.Speed" -> "Module." and "Speed"; without a namespace OutNamespace is empty and OutName the label. */
	static void SplitNamespaceLabel(const FText& Label, FText& OutNamespace, FText& OutName);

	/**
	 * The label keeps its color although the graph is read-only. SGraphPin::GetPinTextColor dims every label of a
	 * graph that cannot be edited (IsEditingEnabled); the Live Preview is read-only but shows the node as in a Blueprint.
	 */
	static bool IgnoresReadOnlyDimming(const UEdGraphPin* Pin);

	/** Brush of the plugin style or of EditorStyle; Fallback when Name does not exist; nullptr when neither does. */
	static const FSlateBrush* FindBrush(FName Name, FName Fallback = NAME_None);

	/**
	 * Widget of a pin of a custom node. Text, key, enumeration and class pins and structures with their own pin
	 * factories keep the engine widget (nullptr): their editing stays exactly as usual, only the type color applies.
	 */
	static TSharedPtr<SGraphPin> CreatePinWidget(UEdGraphPin* Pin);
};

/**
 * Behaviour shared by all pin widgets of custom nodes. BaseType is SGraphPin or one of the typed pins of
 * GraphEditor, so the value fields of the engine stay (check box, numbers, vectors, object pickers).
 */
template<typename BaseType>
class TCNAStyledPin : public BaseType
{
public:
	// SGraphPin
	virtual FSlateColor GetPinColor() const override
	{
		const UEdGraphPin* Pin = this->GetPinObj();
		FLinearColor Color;
		if (Pin && !Pin->IsPendingKill() && !Pin->bOrphanedPin && FCNAPinStyle::GetPinColor(Pin, Color))
		{
			return FSlateColor(Color);
		}
		const FSlateColor BaseColor = BaseType::GetPinColor();
		if (Pin && !Pin->IsPendingKill() && BaseColor.IsColorSpecified() && FCNAPinStyle::GetPinShape(Pin) == ECNAPinShape::Pose)
		{
			// Pose links of the AnimGraph are white; the alpha keeps the dimming of disabled nodes.
			return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, BaseColor.GetSpecifiedColor().A));
		}
		return BaseColor;
	}

	virtual FSlateColor GetPinTextColor() const override
	{
		const FSlateColor BaseColor = BaseType::GetPinTextColor();
		const UEdGraphPin* Pin = this->GetPinObj();
		FLinearColor Color;
		if (!Pin || Pin->IsPendingKill() || !FCNAPinStyle::GetLabelColor(Pin, Color))
		{
			return BaseColor;
		}
		if (BaseColor.IsColorSpecified() && BaseColor.GetSpecifiedColor() == FLinearColor::White)
		{
			return FSlateColor(Color);
		}
		// Disabled and orphaned pins keep the dimmed or red text of the engine; only the dimming of the read-only
		// Live Preview graph is ignored, so the preview shows the label color of a Blueprint.
		if (FCNAPinStyle::IgnoresReadOnlyDimming(Pin))
		{
			return FSlateColor(Color);
		}
		return BaseColor;
	}

protected:
	// V-check: SGraphPin::GetPinIcon is virtual in 4.20.3 (SGraphPinExec and SGraphPinPose override it).
	virtual const FSlateBrush* GetPinIcon() const override
	{
		const UEdGraphPin* Pin = this->GetPinObj();
		const FSlateBrush* Brush = (Pin && !Pin->IsPendingKill()) ? FCNAPinStyle::GetShapedPinIcon(Pin, this->IsConnected(), this->IsHovered()) : nullptr;
		return Brush ? Brush : BaseType::GetPinIcon();
	}

#if CNA_WITH_PIN_LABEL_WIDGET
	virtual TSharedRef<SWidget> GetLabelWidget(const FName& InPinLabelStyle) override
	{
		if (!FCNAPinStyle::UseNamespaceLabel(this->GetPinObj()))
		{
			return BaseType::GetLabelWidget(InPinLabelStyle);
		}
		// Niagara parameter names: the namespace is drawn dimmed in front of the name.
		return SNew(SHorizontalBox)
			.Visibility_Lambda([this]() { return this->GetPinLabel().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), InPinLabelStyle)
				.Text_Lambda([this]() { FText Namespace, Name; FCNAPinStyle::SplitNamespaceLabel(this->GetPinLabel(), Namespace, Name); return Namespace; })
				.ColorAndOpacity_Lambda([this]()
				{
					const FSlateColor TextColor = this->GetPinTextColor();
					FLinearColor Color = TextColor.IsColorSpecified() ? TextColor.GetSpecifiedColor() : FLinearColor::White;
					Color.A *= 0.45f;
					return FSlateColor(Color);
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), InPinLabelStyle)
				.Text_Lambda([this]() { FText Namespace, Name; FCNAPinStyle::SplitNamespaceLabel(this->GetPinLabel(), Namespace, Name); return Name; })
				.ColorAndOpacity_Lambda([this]() { return this->GetPinTextColor(); })
			];
	}
#endif

	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		if (FCNAPinStyle::HasBodyWidget(this->GetPinObj()))
		{
			return SNullWidget::NullWidget;
		}
		return BaseType::GetDefaultValueWidget();
	}
};

/** Pin widget of a custom node without a typed value field: Wildcard, containers, other structures. */
class NODEFACTORYEDITOR_API SGraphPin_NodeFactory : public TCNAStyledPin<SGraphPin>
{
public:
	SLATE_BEGIN_ARGS(SGraphPin_NodeFactory) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin);
};
