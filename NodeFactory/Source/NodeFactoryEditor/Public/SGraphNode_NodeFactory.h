// Copyright (c) NodeFactory. Node widgets of the plugin (spec 3.2, 6.2, 6.5; V24).

#pragma once

#include "CoreMinimal.h"
#include "KismetNodes/SGraphNodeK2Base.h"
#include "KismetNodes/SGraphNodeK2Default.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtr.h"
#include "CNATypes.h"

class SVerticalBox;
class UEdGraphPin;
class UK2Node;
class UK2Node_CustomNode;
class UTexture2D;

/**
 * Widget of a custom node. The engine layout of SGraphNodeK2Base stays; the widget adds what UK2Node cannot
 * express (spec 3.2): the color of the title text, the icon texture, badges, the minimum width, body widgets,
 * the "+" button of dynamic pins (V4) and the Advanced arrow that raises OnAdvancedViewToggled (spec 6.2).
 */
class NODEFACTORYEDITOR_API SGraphNode_NodeFactory : public SGraphNodeK2Base
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_NodeFactory) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UK2Node_CustomNode* InNode);

	// SGraphNode
	virtual void UpdateGraphNode() override;

	// SNodePanel::SNode
	virtual TArray<FOverlayWidgetInfo> GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const override;

protected:
	// SGraphNode
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override;
	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override;
	virtual void CreateInputSideAddButton(TSharedPtr<SVerticalBox> InputBox) override;
	virtual FReply OnAddPin() override;
	virtual EVisibility IsAddPinButtonVisible() const override;

private:
	UK2Node_CustomNode* GetCustomNode() const;

	/** Loads IconTexture and points the brush at it (spec 3.2). */
	void RefreshIconBrush();

	/** Badges of the asset plus the automatic OUTDATED badge (CNA006). */
	void BuildBadges();

	/** One body widget for the value of Pin (spec 3.2, BodyWidgets). */
	TSharedRef<SWidget> CreateBodyWidget(const FCNABodyWidget& Widget, UEdGraphPin* Pin);

	/** Split and recombined pins are noticed after the widget update (V15). */
	void ScheduleSplitRefresh();

	FLinearColor GetHeaderTextColor() const;
	FSlateColor GetIconTextureTint() const;
	EVisibility GetIconTextureVisibility() const;

	/** SGraphNode::CreateAdvancedViewArrow is not virtual in 4.20.3: the engine arrow is swapped for one that raises OnAdvancedViewToggled. */
	void ReplaceAdvancedViewArrow();
	EVisibility GetAdvancedArrowVisibility() const;
	ECheckBoxState GetAdvancedArrowState() const;
	const FSlateBrush* GetAdvancedArrowImage() const;
	void OnAdvancedArrowChanged(ECheckBoxState NewState);

	/** The box CreateBelowPinControls got; the engine arrow follows the widgets of the plugin in it. */
	TSharedPtr<SVerticalBox> ArrowBox;

	/** Brush of IconTexture; the texture is held strongly while the widget lives (spec 9.6). */
	FSlateBrush IconTextureBrush;
	TStrongObjectPtr<UTexture2D> IconTexture;
	FSlateBrush CornerTextureBrush;
	TStrongObjectPtr<UTexture2D> CornerTexture;
	TSharedPtr<SWidget> CornerTextureWidget;

	struct FBadgeWidget
	{
		TSharedPtr<SWidget> Widget;
		ECNABadgeCorner Corner = ECNABadgeCorner::TopRight;
	};
	TArray<FBadgeWidget> BadgeWidgets;

	/** Entries of the combo boxes on the body: labels shown and values written to the pin. */
	struct FComboData
	{
		TArray<TSharedPtr<FString>> Labels;
		TArray<FString> Values;
	};
	TArray<TSharedPtr<FComboData>> ComboData;
};

/**
 * Events and actions of UCNABehaviorBase in the Behavior Graph get a turquoise title (spec 6.5, V24).
 * The look only applies inside the asset window; the logic does not change.
 */
class NODEFACTORYEDITOR_API SGraphNodeCNABehavior : public SGraphNodeK2Default
{
public:
	// SGraphNode
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

protected:
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override;
};

/** Get Local and Set Local: a marker in the color of the variable type in front of the title (spec 4.8). */
class NODEFACTORYEDITOR_API SGraphNodeCNALocalVariable : public SGraphNodeK2Default
{
protected:
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override;

private:
	FSlateColor GetVariableColor() const;

	/** Drop-down of the variables of the asset (spec 4.8, item 2). */
	TSharedRef<SWidget> MakeVariableMenu();
};
