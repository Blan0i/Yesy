// Copyright (c) NodeFactory. Pin widgets of custom nodes: own pin and label colors (spec 4.7, V25).

#pragma once

#include "CoreMinimal.h"
#include "SGraphPin.h"
#include "Widgets/SNullWidget.h"

class UEdGraphPin;
class UK2Node_CustomNode;

/**
 * The typed pin widgets of GraphEditor (KismetPins/SGraphPinBool.h and the others) are expected to be exported
 * in 4.20.3 with the constructor (FArguments, UEdGraphPin*) (V25, V-check). Set to 0 if they are not: pins of
 * custom nodes then use the engine widgets and the colors of their types (the fallback of V25).
 */
#ifndef CNA_WITH_STYLED_PINS
#define CNA_WITH_STYLED_PINS 1
#endif

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
		return BaseType::GetPinColor();
	}

	virtual FSlateColor GetPinTextColor() const override
	{
		// Disabled and orphaned pins keep the dimmed or red text of the engine.
		const FSlateColor BaseColor = BaseType::GetPinTextColor();
		if (!BaseColor.IsColorSpecified() || BaseColor.GetSpecifiedColor() != FLinearColor::White)
		{
			return BaseColor;
		}
		const UEdGraphPin* Pin = this->GetPinObj();
		FLinearColor Color;
		if (Pin && FCNAPinStyle::GetLabelColor(Pin, Color))
		{
			return FSlateColor(Color);
		}
		return BaseColor;
	}

protected:
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
