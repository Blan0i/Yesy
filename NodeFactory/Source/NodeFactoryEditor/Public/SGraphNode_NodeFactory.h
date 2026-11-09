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
class UCustomNodeAsset;
class UEdGraphPin;
class UK2Node;
class UK2Node_CustomNode;
class UTexture2D;
struct FSlateBrush;

/** Layout of the node widget chosen by NodeStyle, AnimStyle and NiagaraStyle of the asset. */
enum class ECNANodeLayout : uint8
{
	/** SGraphNodeK2Base: Blueprint, the regular anim nodes and the Niagara nodes. */
	Standard,
	/** Standard layout with the "Final Animation Pose" figure between the pin columns. */
	AnimResult,
	/** Rounded state body with the title in the middle: State, Conduit, Entry. */
	AnimState,
	/** Round transition icon with the pins left and right of it. */
	AnimTransition
};

/**
 * Widget of a custom node. The engine layout of SGraphNodeK2Base stays; the widget adds what UK2Node cannot
 * express (spec 3.2): the color of the title text, the icon texture, badges, the minimum width, body widgets,
 * the "+" button of dynamic pins (V4) and the Advanced arrow that raises OnAdvancedViewToggled (spec 6.2).
 * The Animation and Niagara styles of the asset change the look only; every pin stays a Blueprint pin.
 * The style parts are implemented in SGraphNode_CNAAnimStyles.cpp and SGraphNode_CNANiagara.cpp.
 */
class NODEFACTORYEDITOR_API SGraphNode_NodeFactory : public SGraphNodeK2Base
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_NodeFactory) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UK2Node_CustomNode* InNode);

	/** Layout used for nodes of the asset; Standard without an asset. */
	static ECNANodeLayout GetLayoutForAsset(const UCustomNodeAsset* Asset);

	// SGraphNode
	virtual void UpdateGraphNode() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

	// SNodePanel::SNode
	virtual TArray<FOverlayWidgetInfo> GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const override;

protected:
	// SGraphNode
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override;
	virtual TSharedRef<SWidget> CreateNodeContentArea() override;
	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override;
	virtual void CreateInputSideAddButton(TSharedPtr<SVerticalBox> InputBox) override;
	virtual void CreateOutputSideAddButton(TSharedPtr<SVerticalBox> OutputBox) override;
	virtual FReply OnAddPin() override;
	virtual EVisibility IsAddPinButtonVisible() const override;

private:
	UK2Node_CustomNode* GetCustomNode() const;
	const UCustomNodeAsset* GetAsset() const;

	// Animation styles (SGraphNode_CNAAnimStyles.cpp)

	/** State, Conduit and Entry: SGraphNodeAnimState / SGraphNodeAnimStateEntry of 4.20.3 with Blueprint pins. */
	void UpdateAnimStateNode();
	/** Transition: SGraphNodeAnimTransition of 4.20.3 with the pins left and right of the icon. */
	void UpdateAnimTransitionNode();
	/** Pin columns with the result figure between them (SGraphNodeAnimationResult). */
	TSharedRef<SWidget> CreateAnimResultContentArea();
	/** Comment bubble, below-pin controls and Advanced arrow shared by the own layouts. */
	void FinishCustomLayout(TSharedRef<SVerticalBox> InnerBox);
	/** Title of the state look: icon, inline editable name and the subtitle lines. */
	TSharedRef<SWidget> CreateAnimStateTitle(TSharedRef<SNodeTitle> NodeTitle, const FSlateBrush* TypeIcon);
	/** Icon in front of the title of the regular anim kinds (State Machine); nullptr for none. */
	const FSlateBrush* GetAnimTitleIcon() const;
	FSlateColor GetAnimBodyColor() const;
	/** Lightning mark of anim nodes on the fast path (SAnimationGraphNode). */
	void BuildFastPathIndicator();

	// Niagara styles (SGraphNode_CNANiagara.cpp)

	/** Map Get / Map Set: every pin of the parameter column sits on a row background like in Niagara. */
	bool UsesNiagaraMapRows(EEdGraphPinDirection Direction) const;
	/** "Add Pin" rows of Map Get / Map Set; false when the side has none. */
	bool UsesNiagaraAddPinRow(EEdGraphPinDirection Direction) const;
	void AddNiagaraMapRowPin(const TSharedRef<SGraphPin>& PinToAdd);
	void AddNiagaraAddPinRow(TSharedPtr<SVerticalBox> Box, EEdGraphPinDirection Direction);
	/** Read-only text area of Custom HLSL with NiagaraBodyText. */
	void AddNiagaraBodyText(TSharedPtr<SVerticalBox> MainBox);

	/** Curve body widget: a graph on the body, not bound to a pin (spec Curve). */
	TSharedRef<SWidget> CreateCurveBodyWidget(const FCNABodyWidget& Widget);

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

	/** Fast path mark of the Animation style, nullptr when it is off. */
	TSharedPtr<SWidget> FastPathWidget;

	/** Layout of the last UpdateGraphNode; CreateNodeContentArea and AddPin follow it. */
	ECNANodeLayout CurrentLayout = ECNANodeLayout::Standard;

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
