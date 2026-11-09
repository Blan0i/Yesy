// Copyright (c) NodeFactory. Animation Blueprint looks of the custom node widget (NodeStyle == Animation).
//
// SGraphNodeAnimState, SGraphNodeAnimStateEntry, SGraphNodeAnimTransition, SGraphNodeAnimationResult and
// SAnimationGraphNode are private to GraphEditor in 4.20.3, so their looks are rebuilt here around Blueprint pins:
// every pin keeps its links, default values, body widgets, badges, corner icon and the "+" button.

#include "SGraphNode_NodeFactory.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "SGraphPin_NodeFactory.h"

#include "EdGraph/EdGraphNode.h"
#include "EditorStyleSet.h"
#include "GraphEditorSettings.h"
#include "SCommentBubble.h"
#include "SGraphNode.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNAAnimStylesLocal
{
	// Colors of the 4.20.3 widgets: SGraphNodeAnimState::GetBorderBackgroundColor (inactive state, conduit),
	// SGraphNodeAnimTransition::StaticGetTransitionColor (base color) and the shadow behind the state title.
	const FLinearColor StateColor(0.08f, 0.08f, 0.08f, 1.0f);
	const FLinearColor ConduitColor(0.38f, 0.45f, 0.21f, 1.0f);
	const FLinearColor TransitionColor(0.9f, 0.9f, 0.9f, 1.0f);
	const FLinearColor TitleShadowColor(0.6f, 0.6f, 0.6f, 1.0f);

	/** Size of the transition circle when the brushes of the editor style are missing. */
	const float TransitionFallbackSize = 25.0f;

	/** Tooltip of the own layouts; the transition has no visible title, so its tooltip starts with the title. */
	FText GetNodeTooltipText(const TWeakObjectPtr<UEdGraphNode>& WeakNode, bool bWithTitle)
	{
		const UEdGraphNode* Node = WeakNode.Get();
		if (!Node)
		{
			return FText::GetEmpty();
		}
		const FText Tooltip = Node->GetTooltipText();
		if (!bWithTitle)
		{
			return Tooltip;
		}
		const FText Title = Node->GetNodeTitle(ENodeTitleType::FullTitle);
		return Tooltip.IsEmpty() ? Title : FText::Format(LOCTEXT("NodeWidget.TransitionTooltip", "{0}\n{1}"), Title, Tooltip);
	}
}

// Shared parts

FSlateColor SGraphNode_NodeFactory::GetAnimBodyColor() const
{
	using namespace CNAAnimStylesLocal;

	const UCustomNodeAsset* Asset = GetAsset();
	FLinearColor Color = StateColor;
	if (Asset)
	{
		if (Asset->bUseHeaderColor)
		{
			Color = Asset->HeaderColor;
		}
		else if (Asset->AnimStyle == ECNAAnimNodeStyle::Conduit)
		{
			Color = ConduitColor;
		}
		else if (Asset->AnimStyle == ECNAAnimNodeStyle::Transition)
		{
			// The title color of transitions (red) is only used by the details; the icon is drawn light grey.
			Color = TransitionColor;
		}
		else
		{
			FLinearColor StyleColor;
			if (Asset->GetStyleHeaderColor(StyleColor))
			{
				Color = StyleColor;
			}
		}
	}
	if (GraphNode && !GraphNode->IsNodeEnabled())
	{
		Color.A *= 0.5f;
	}
	return FSlateColor(Color);
}

const FSlateBrush* SGraphNode_NodeFactory::GetAnimTitleIcon() const
{
	const UCustomNodeAsset* Asset = GetAsset();
	if (!Asset || Asset->NodeStyle != ECNANodeStyle::Animation || Asset->AnimStyle != ECNAAnimNodeStyle::StateMachine)
	{
		return nullptr;
	}
	// An icon or icon texture of the asset wins.
	if (!Asset->Icon.IsNone() || IconTexture.IsValid())
	{
		return nullptr;
	}
	// V-check: "GraphEditor.StateMachine_16x" is the state machine icon of My Blueprint in 4.20.3.
	return FCNAPinStyle::FindBrush(TEXT("GraphEditor.StateMachine_16x"), TEXT("Graph.StateNode.Icon"));
}

void SGraphNode_NodeFactory::BuildFastPathIndicator()
{
	FastPathWidget.Reset();

	const UCustomNodeAsset* Asset = GetAsset();
	if (!Asset || Asset->NodeStyle != ECNANodeStyle::Animation || !Asset->bShowFastPathIcon)
	{
		return;
	}

	const FText Tooltip = LOCTEXT("NodeWidget.FastPathTooltip", "Fast path enabled: This node is not using any Blueprint calls to update its data.");
	// V-check: brush of SAnimationGraphNode in 4.20.3. Overlay widgets are only painted when exactly Visible.
	if (const FSlateBrush* Brush = FCNAPinStyle::FindBrush(TEXT("Graph.AnimationFastPathIndicator")))
	{
		FastPathWidget = SNew(SImage)
			.Image(Brush)
			.ToolTipText(Tooltip)
			.Visibility(EVisibility::Visible);
		return;
	}
	FastPathWidget = SNew(SBorder)
		.Visibility(EVisibility::Visible)
		.BorderImage(FEditorStyle::GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.95f, 0.75f, 0.1f, 1.0f))
		.Padding(FMargin(3.0f, 0.0f))
		.ToolTipText(Tooltip)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NodeWidget.FastPathFallback", "FAST"))
			.ColorAndOpacity(FLinearColor::Black)
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 7))
		];
}

void SGraphNode_NodeFactory::FinishCustomLayout(TSharedRef<SVerticalBox> InnerBox)
{
	// Comment bubble as SGraphNode::UpdateGraphNode and SGraphNodeK2Base::UpdateCompactNode create it in 4.20.3.
	TSharedPtr<SCommentBubble> CommentBubble;
	SAssignNew(CommentBubble, SCommentBubble)
		.GraphNode(GraphNode)
		.Text(this, &SGraphNode_NodeFactory::GetNodeComment)
		.OnTextCommitted(this, &SGraphNode_NodeFactory::OnCommentTextCommitted)
		.ColorAndOpacity(GetDefault<UGraphEditorSettings>()->DefaultCommentNodeTitleColor)
		.AllowPinning(true)
		.EnableTitleBarBubble(true)
		.EnableBubbleCtrls(true)
		.GraphLOD(this, &SGraphNode_NodeFactory::GetCurrentLOD)
		.IsGraphNodeHovered(this, &SGraphNode_NodeFactory::IsHovered);

	GetOrAddSlot(ENodeZone::TopCenter)
		.SlotOffset(TAttribute<FVector2D>(CommentBubble.Get(), &SCommentBubble::GetOffset))
		.SlotSize(TAttribute<FVector2D>(CommentBubble.Get(), &SCommentBubble::GetSize))
		.AllowScaling(TAttribute<bool>(CommentBubble.Get(), &SCommentBubble::IsScalingAllowed))
		.VAlign(VAlign_Top)
		[
			CommentBubble.ToSharedRef()
		];

	// Same order as SGraphNode::UpdateGraphNode, so body widgets, the "+" button and the Advanced arrow behave alike.
	CreatePinWidgets();
	CreateInputSideAddButton(LeftNodeBox);
	CreateOutputSideAddButton(RightNodeBox);
	CreateBelowPinControls(InnerBox);
	CreateAdvancedViewArrow(InnerBox);
}

// Final Animation Pose (SGraphNodeAnimationResult)

TSharedRef<SWidget> SGraphNode_NodeFactory::CreateAnimResultContentArea()
{
	// V-check: "Graph.AnimationResultNode.Body" is the figure of the result node in 4.20.3.
	const FSlateBrush* Figure = FCNAPinStyle::FindBrush(TEXT("Graph.AnimationResultNode.Body"));
	TSharedRef<SWidget> FigureWidget = SNullWidget::NullWidget;
	if (Figure)
	{
		FigureWidget = SNew(SImage).Image(Figure);
	}

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush(TEXT("NoBorder")))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(0.0f, 3.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				SAssignNew(LeftNodeBox, SVerticalBox)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(5.0f, 0.0f))
			[
				FigureWidget
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			[
				SAssignNew(RightNodeBox, SVerticalBox)
			]
		];
}

// State, Conduit, Entry (SGraphNodeAnimState, SGraphNodeAnimStateEntry)

TSharedRef<SWidget> SGraphNode_NodeFactory::CreateAnimStateTitle(TSharedRef<SNodeTitle> NodeTitle, const FSlateBrush* TypeIcon)
{
	const TAttribute<FLinearColor> TextColor = TAttribute<FLinearColor>::Create(
		TAttribute<FLinearColor>::FGetter::CreateSP(this, &SGraphNode_NodeFactory::GetHeaderTextColor));
	NodeTitle->SetColorAndOpacity(TextColor);

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	if (TypeIcon)
	{
		Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(TypeIcon)
			];
	}
	Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SImage)
			.Image(&IconTextureBrush)
			.ColorAndOpacity(this, &SGraphNode_NodeFactory::GetIconTextureTint)
			.Visibility(this, &SGraphNode_NodeFactory::GetIconTextureVisibility)
		];
	Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(4.0f, 0.0f, 4.0f, 0.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SAssignNew(InlineEditableText, SInlineEditableTextBlock)
				.Style(FEditorStyle::Get(), "Graph.StateNode.NodeTitleInlineEditableText")
				.Text(&NodeTitle.Get(), &SNodeTitle::GetHeadTitle)
				.OnVerifyTextChanged(this, &SGraphNode_NodeFactory::OnVerifyNameTextChanged)
				.OnTextCommitted(this, &SGraphNode_NodeFactory::OnNameTextCommited)
				.IsReadOnly(this, &SGraphNode_NodeFactory::IsNameReadOnly)
				.IsSelected(this, &SGraphNode_NodeFactory::IsSelectedExclusively)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				NodeTitle
			]
		];
	InlineEditableText->SetColorAndOpacity(TextColor);

	return SNew(SBorder)
		.BorderImage(FCNAPinStyle::FindBrush(TEXT("Graph.StateNode.ColorSpill"), TEXT("WhiteBrush")))
		.BorderBackgroundColor(CNAAnimStylesLocal::TitleShadowColor)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			Row
		];
}

void SGraphNode_NodeFactory::UpdateAnimStateNode()
{
	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	// V-check: SGraphNode::SetupErrorReporting (protected) creates ErrorReporting in 4.20.3.
	SetupErrorReporting();

	const UCustomNodeAsset* Asset = GetAsset();
	const ECNAAnimNodeStyle Kind = Asset ? Asset->AnimStyle : ECNAAnimNodeStyle::State;
	const FSlateBrush* TypeIcon = nullptr;
	if (Kind == ECNAAnimNodeStyle::Conduit)
	{
		TypeIcon = FCNAPinStyle::FindBrush(TEXT("Graph.ConduitNode.Icon"), TEXT("Graph.StateNode.Icon"));
	}
	else if (Kind == ECNAAnimNodeStyle::State)
	{
		TypeIcon = FCNAPinStyle::FindBrush(TEXT("Graph.StateNode.Icon"));
	}

	const TWeakObjectPtr<UEdGraphNode> WeakNode(GraphNode);
	SetToolTipText(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([WeakNode]()
	{
		return CNAAnimStylesLocal::GetNodeTooltipText(WeakNode, false);
	})));

	this->ContentScale.Bind(this, &SGraphNode_NodeFactory::GetContentScale);

	TSharedRef<SNodeTitle> NodeTitle = SNew(SNodeTitle, GraphNode);
	TSharedRef<SVerticalBox> InnerBox = SNew(SVerticalBox);
	InnerBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(10.0f, 10.0f, 10.0f, 6.0f)
		[
			CreateAnimStateTitle(NodeTitle, TypeIcon)
		];
	// The pins sit along the body below the title, so they stay reachable for wires.
	InnerBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SAssignNew(LeftNodeBox, SVerticalBox)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SAssignNew(RightNodeBox, SVerticalBox)
			]
		];
	InnerBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(5.0f, 1.0f))
		[
			ErrorReporting->AsWidget()
		];

	// V-check: "Graph.StateNode.Body" is a rounded box brush of 4.20.3; "Graph.Node.Body" is the fallback.
	GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FCNAPinStyle::FindBrush(TEXT("Graph.StateNode.Body"), TEXT("Graph.Node.Body")))
			.Padding(0.0f)
			.BorderBackgroundColor(this, &SGraphNode_NodeFactory::GetAnimBodyColor)
			[
				InnerBox
			]
		];

	FinishCustomLayout(InnerBox);
}

// Transition (SGraphNodeAnimTransition)

void SGraphNode_NodeFactory::UpdateAnimTransitionNode()
{
	InputPins.Empty();
	OutputPins.Empty();
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	SetupErrorReporting();

	const TWeakObjectPtr<UEdGraphNode> WeakNode(GraphNode);
	SetToolTipText(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([WeakNode]()
	{
		return CNAAnimStylesLocal::GetNodeTooltipText(WeakNode, true);
	})));

	this->ContentScale.Bind(this, &SGraphNode_NodeFactory::GetContentScale);

	// V-check: transition brushes of 4.20.3; the color spill is tinted, the icon drawn on top.
	const FSlateBrush* Spill = FCNAPinStyle::FindBrush(TEXT("Graph.TransitionNode.ColorSpill"), TEXT("Graph.TransitionNode.Body"));
	const FSlateBrush* Icon = FCNAPinStyle::FindBrush(TEXT("Graph.TransitionNode.Icon"));
	TSharedRef<SOverlay> Circle = SNew(SOverlay);
	if (Spill)
	{
		Circle->AddSlot()
			[
				SNew(SImage)
				.Image(Spill)
				.ColorAndOpacity(this, &SGraphNode_NodeFactory::GetAnimBodyColor)
			];
	}
	else
	{
		Circle->AddSlot()
			[
				SNew(SBox)
				.WidthOverride(CNAAnimStylesLocal::TransitionFallbackSize)
				.HeightOverride(CNAAnimStylesLocal::TransitionFallbackSize)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush(TEXT("WhiteBrush")))
					.ColorAndOpacity(this, &SGraphNode_NodeFactory::GetAnimBodyColor)
				]
			];
	}
	if (Icon)
	{
		Circle->AddSlot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(Icon)
			];
	}

	TSharedRef<SVerticalBox> InnerBox = SNew(SVerticalBox);
	InnerBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(LeftNodeBox, SVerticalBox)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			[
				Circle
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SAssignNew(RightNodeBox, SVerticalBox)
			]
		];
	InnerBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(5.0f, 1.0f))
		[
			ErrorReporting->AsWidget()
		];

	GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			InnerBox
		];

	FinishCustomLayout(InnerBox);
}

#undef LOCTEXT_NAMESPACE
