// Copyright (c) NodeFactory. Niagara script looks of the custom node widget (NodeStyle == Niagara).
//
// Map Get / Map Set rows follow SNiagaraGraphParameterMapGetNode (pin row backgrounds "Graph.Pin.Background",
// "PlainBorder" while hovered), the "Add Pin" rows follow SNiagaraGraphPinAdd ("Plus" on a HoverHintOnly button) and
// the Custom HLSL text area follows SNiagaraGraphNodeCustomHlsl. The Add Pin rows drive the dynamic pins of the node.

#include "SGraphNode_NodeFactory.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "SGraphPin_NodeFactory.h"

#include "EditorStyleSet.h"
#include "GraphEditorSettings.h"
#include "SGraphPin.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

bool SGraphNode_NodeFactory::UsesNiagaraMapRows(EEdGraphPinDirection Direction) const
{
	const UK2Node_CustomNode* Node = GetCustomNode();
	const UCustomNodeAsset* Asset = Node ? Node->GetCustomNodeAsset() : nullptr;
	if (!Asset || Asset->NodeStyle != ECNANodeStyle::Niagara || CurrentLayout != ECNANodeLayout::Standard || Node->ShouldDrawCompact())
	{
		return false;
	}
	return (Asset->NiagaraStyle == ECNANiagaraNodeStyle::MapGet && Direction == EGPD_Output)
		|| (Asset->NiagaraStyle == ECNANiagaraNodeStyle::MapSet && Direction == EGPD_Input);
}

bool SGraphNode_NodeFactory::UsesNiagaraAddPinRow(EEdGraphPinDirection Direction) const
{
	const UK2Node_CustomNode* Node = GetCustomNode();
	const UCustomNodeAsset* Asset = Node ? Node->GetCustomNodeAsset() : nullptr;
	if (!Asset || Asset->NodeStyle != ECNANodeStyle::Niagara || !Asset->bNiagaraAddPinRows || !Node->HasDynamicPins())
	{
		return false;
	}
	// Map Get reads parameters (outputs), every other kind writes them (inputs), as the Add pins of Niagara.
	const EEdGraphPinDirection Side = Asset->NiagaraStyle == ECNANiagaraNodeStyle::MapGet ? EGPD_Output : EGPD_Input;
	return Direction == Side;
}

void SGraphNode_NodeFactory::AddNiagaraMapRowPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	const bool bInput = PinToAdd->GetDirection() == EGPD_Input;
	TSharedPtr<SVerticalBox> Box = bInput ? LeftNodeBox : RightNodeBox;
	if (!Box.IsValid())
	{
		SGraphNodeK2Base::AddPin(PinToAdd);
		return;
	}

	PinToAdd->SetOwner(SharedThis(this));

	const FSlateBrush* Background = FCNAPinStyle::FindBrush(TEXT("Graph.Pin.Background"), TEXT("NoBorder"));
	const FSlateBrush* HoveredBackground = FCNAPinStyle::FindBrush(TEXT("PlainBorder"), TEXT("Graph.Pin.Background"));

	TSharedRef<SBorder> Row = SNew(SBorder)
		.Padding(FMargin(0.0f, 3.0f))
		.HAlign(bInput ? HAlign_Left : HAlign_Right)
		.VAlign(VAlign_Center)
		[
			PinToAdd
		];
	const TWeakPtr<SBorder> WeakRow = Row;
	Row->SetBorderImage(TAttribute<const FSlateBrush*>::Create(TAttribute<const FSlateBrush*>::FGetter::CreateLambda([WeakRow, Background, HoveredBackground]()
	{
		const TSharedPtr<SBorder> Target = WeakRow.Pin();
		return (Target.IsValid() && Target->IsHovered()) ? HoveredBackground : Background;
	})));

	// The row collapses with an Advanced pin, not only the pin inside it (SGraphNode::AddPin).
	const UEdGraphPin* PinObj = PinToAdd->GetPinObj();
	if (PinObj && PinObj->bAdvancedView)
	{
		Row->SetVisibility(TAttribute<EVisibility>(PinToAdd, &SGraphPin::IsPinVisibleAsAdvanced));
	}

	const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
	Box->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.Padding(bInput ? Settings->GetInputPinPadding() : Settings->GetOutputPinPadding())
		[
			Row
		];

	if (bInput)
	{
		InputPins.Add(PinToAdd);
	}
	else
	{
		OutputPins.Add(PinToAdd);
	}
}

void SGraphNode_NodeFactory::AddNiagaraAddPinRow(TSharedPtr<SVerticalBox> Box, EEdGraphPinDirection Direction)
{
	if (!Box.IsValid())
	{
		return;
	}
	const bool bInput = Direction == EGPD_Input;

	const FSlateBrush* PlusBrush = FCNAPinStyle::FindBrush(TEXT("Plus"));
	TSharedRef<SWidget> PlusWidget = SNew(STextBlock).Text(LOCTEXT("NodeWidget.NiagaraAddPinGlyph", "+"));
	if (PlusBrush)
	{
		PlusWidget = SNew(SImage)
			.Image(PlusBrush)
			.ColorAndOpacity(FSlateColor::UseForeground());
	}
	TSharedRef<SWidget> TextWidget = SNew(STextBlock)
		.TextStyle(FEditorStyle::Get(), "Graph.Node.PinName")
		.Text(LOCTEXT("NodeWidget.NiagaraAddPin", "Add Pin"));

	// Input side: "+" then the text, output side mirrored, like the Add pins of Map Set and Map Get.
	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);
	Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(bInput ? FMargin(0.0f, 0.0f, 4.0f, 0.0f) : FMargin(0.0f))
		[
			bInput ? PlusWidget : TextWidget
		];
	Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(bInput ? FMargin(0.0f) : FMargin(4.0f, 0.0f, 0.0f, 0.0f))
		[
			bInput ? TextWidget : PlusWidget
		];

	const TWeakPtr<SGraphNode_NodeFactory> WeakThis = SharedThis(this);
	const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();
	Box->AddSlot()
		.AutoHeight()
		.HAlign(bInput ? HAlign_Left : HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(bInput ? Settings->GetInputPinPadding() : Settings->GetOutputPinPadding())
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ForegroundColor(FSlateColor::UseForeground())
			.ContentPadding(FMargin(2.0f))
			.Cursor(EMouseCursor::Default)
			.ToolTipText(LOCTEXT("NodeWidget.NiagaraAddPinTooltip", "Add an element pin to the dynamic group of this node."))
			// Hidden when the group is full; shown but disabled in read-only graphs such as the Live Preview.
			.Visibility_Lambda([WeakThis]()
			{
				const TSharedPtr<SGraphNode_NodeFactory> This = WeakThis.Pin();
				const UK2Node_CustomNode* Node = This.IsValid() ? This->GetCustomNode() : nullptr;
				return (Node && Node->CanAddDynamicElement()) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.IsEnabled_Lambda([WeakThis]()
			{
				const TSharedPtr<SGraphNode_NodeFactory> This = WeakThis.Pin();
				return This.IsValid() && This->IsNodeEditable();
			})
			.OnClicked(this, &SGraphNode_NodeFactory::OnAddPin)
			[
				Content
			]
		];
}

void SGraphNode_NodeFactory::AddNiagaraBodyText(TSharedPtr<SVerticalBox> MainBox)
{
	const UCustomNodeAsset* Asset = GetAsset();
	if (!Asset || !MainBox.IsValid() || Asset->NodeStyle != ECNANodeStyle::Niagara || Asset->NiagaraStyle != ECNANiagaraNodeStyle::CustomHlsl)
	{
		return;
	}

	// Read-only on instances: the text is a note of the asset (NiagaraBodyText), not code that runs.
	const TWeakObjectPtr<UCustomNodeAsset> WeakAsset(const_cast<UCustomNodeAsset*>(Asset));
	MainBox->AddSlot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			.Cursor(EMouseCursor::Default)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(10.0f, 5.0f, 10.0f, 10.0f))
			[
				SNew(SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.AutoWrapText(false)
				.Margin(FMargin(5.0f))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
				.HintText(LOCTEXT("NodeWidget.NiagaraBodyTextHint", "Niagara Body Text of the asset"))
				.Text_Lambda([WeakAsset]()
				{
					const UCustomNodeAsset* Target = WeakAsset.Get();
					return Target ? Target->NiagaraBodyText : FText::GetEmpty();
				})
			]
		];
}

#undef LOCTEXT_NAMESPACE
