// Copyright (c) NodeFactory. Node widgets of the plugin (spec 3.2, 4.4, 4.8, 6.2, 6.5; V4, V15, V24).

#include "SGraphNode_NodeFactory.h"
#include "CNAPinUtils.h"
#include "CNATypes.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "K2Node_GetLocal.h"
#include "NodeFactoryStyle.h"
#include "SCNACurveGraph.h"
#include "SGraphPin_NodeFactory.h"

#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Containers/Ticker.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "EditorStyleSet.h"
#include "Engine/Texture2D.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditorSettings.h"
#include "K2Node_Event.h"
#include "SGraphPin.h"
#include "ScopedTransaction.h"
#include "Styling/CoreStyle.h"
#include "UObject/Class.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNANodeWidgetLocal
{
	/** Size of IconTexture in the title (the engine palette icons are 16 units as well). */
	const FVector2D IconTextureSize(16.0f, 16.0f);

	/** Distance of the first badge from the side of the node and the gap between badges. */
	const float BadgeMargin = 8.0f;
	const float BadgeGap = 3.0f;

	/** Background of the automatic badge of outdated nodes (CNA006). */
	const FLinearColor OutdatedBadgeColor(0.78f, 0.38f, 0.04f, 1.0f);

	/** Writes a default value the way SGraphPinBool and the other engine pins do: undoable, through the schema. */
	void SetPinValue(UEdGraphPin* Pin, const FString& NewValue)
	{
		if (!Pin || Pin->IsPendingKill() || Pin->GetDefaultAsString() == NewValue)
		{
			return;
		}
		const UEdGraphSchema* Schema = Pin->GetSchema();
		if (!Schema)
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("NodeWidget.BodyWidgetValueTransaction", "Change Pin Value"));
		Pin->Modify();
		// The schema calls PinDefaultValueChanged, which raises OnPinValueChanged (spec 3.2, A22).
		Schema->TrySetDefaultValue(*Pin, NewValue);
	}

	void SetPinText(UEdGraphPin* Pin, const FText& NewText)
	{
		if (!Pin || Pin->IsPendingKill() || Pin->DefaultTextValue.EqualTo(NewText))
		{
			return;
		}
		const UEdGraphSchema* Schema = Pin->GetSchema();
		if (!Schema)
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("NodeWidget.BodyWidgetTextTransaction", "Change Pin Value"));
		Pin->Modify();
		Schema->TrySetDefaultText(*Pin, NewText);
	}

	/** Text shown by a text box: the text of Text pins, the plain value otherwise. */
	FText GetPinValueText(const UEdGraphPin* Pin)
	{
		if (!Pin || Pin->IsPendingKill())
		{
			return FText::GetEmpty();
		}
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
		{
			return Pin->DefaultTextValue;
		}
		return FText::FromString(Pin->GetDefaultAsString());
	}

	/** Normalizes typed input of a text box for numeric pins; false rejects the input. */
	bool NormalizeTypedValue(const FEdGraphPinType& Type, const FString& Input, FString& OutValue)
	{
		const FString Trimmed = Input.TrimStartAndEnd();
		if (Type.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			if (!Trimmed.IsNumeric())
			{
				return false;
			}
			OutValue = FString::FromInt(FCString::Atoi(*Trimmed));
			return true;
		}
		if (Type.PinCategory == UEdGraphSchema_K2::PC_Float)
		{
			if (!Trimmed.IsNumeric())
			{
				return false;
			}
			OutValue = FString::SanitizeFloat(FCString::Atof(*Trimmed));
			return true;
		}
		OutValue = Input;
		return true;
	}

	bool IsPinEditable(const UEdGraphPin* Pin)
	{
		return Pin && !Pin->IsPendingKill() && !Pin->bOrphanedPin && !Pin->bDefaultValueIsReadOnly && Pin->LinkedTo.Num() == 0;
	}

	bool IsRightCorner(ECNABadgeCorner Corner)
	{
		return Corner == ECNABadgeCorner::TopRight || Corner == ECNABadgeCorner::BottomRight;
	}

	bool IsBottomCorner(ECNABadgeCorner Corner)
	{
		return Corner == ECNABadgeCorner::BottomLeft || Corner == ECNABadgeCorner::BottomRight;
	}

	TSharedRef<SWidget> MakeBadge(const FText& Text, const FLinearColor& Color, const FLinearColor& TextColor, const FText& ToolTip)
	{
		const FSlateBrush* Brush = FNodeFactoryStyle::FindBrush(FNodeFactoryStyle::BadgeBrushName);
		if (!Brush)
		{
			Brush = FEditorStyle::GetBrush(TEXT("WhiteBrush"));
		}
		// Overlay widgets are only painted when their visibility is exactly Visible (SGraphPanel::OnPaint).
		return SNew(SBorder)
			.Visibility(EVisibility::Visible)
			.BorderImage(Brush)
			.BorderBackgroundColor(Color)
			.Padding(FMargin(4.0f, 1.0f))
			.ToolTipText(ToolTip)
			[
				SNew(STextBlock)
				.Text(Text)
				.ColorAndOpacity(TextColor)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 7))
			];
	}
}

// SGraphNode_NodeFactory

void SGraphNode_NodeFactory::Construct(const FArguments& InArgs, UK2Node_CustomNode* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

UK2Node_CustomNode* SGraphNode_NodeFactory::GetCustomNode() const
{
	return Cast<UK2Node_CustomNode>(GraphNode);
}

const UCustomNodeAsset* SGraphNode_NodeFactory::GetAsset() const
{
	const UK2Node_CustomNode* Node = GetCustomNode();
	return Node ? Node->GetCustomNodeAsset() : nullptr;
}

ECNANodeLayout SGraphNode_NodeFactory::GetLayoutForAsset(const UCustomNodeAsset* Asset)
{
	if (!Asset || Asset->NodeStyle != ECNANodeStyle::Animation)
	{
		return ECNANodeLayout::Standard;
	}
	switch (Asset->AnimStyle)
	{
	case ECNAAnimNodeStyle::Result:
		return ECNANodeLayout::AnimResult;
	case ECNAAnimNodeStyle::State:
	case ECNAAnimNodeStyle::Conduit:
	case ECNAAnimNodeStyle::Entry:
		return ECNANodeLayout::AnimState;
	case ECNAAnimNodeStyle::Transition:
		return ECNANodeLayout::AnimTransition;
	default:
		return ECNANodeLayout::Standard;
	}
}

void SGraphNode_NodeFactory::UpdateGraphNode()
{
	ComboData.Reset();

	// The texture is loaded before the base layout asks GetIconAndTint, which then leaves the brush icon out.
	RefreshIconBrush();

	const UK2Node_CustomNode* Node = GetCustomNode();
	const UCustomNodeAsset* Asset = Node ? Node->GetCustomNodeAsset() : nullptr;

	// CreateNodeContentArea and AddPin read the layout while the base builds the widget.
	CurrentLayout = GetLayoutForAsset(Asset);
	switch (CurrentLayout)
	{
	case ECNANodeLayout::AnimState:
		UpdateAnimStateNode();
		break;
	case ECNANodeLayout::AnimTransition:
		UpdateAnimTransitionNode();
		break;
	case ECNANodeLayout::Standard:
	case ECNANodeLayout::AnimResult:
	default:
		SGraphNodeK2Base::UpdateGraphNode();
		break;
	}

	TSharedPtr<SVerticalBox> BodyBox = ArrowBox;
	ReplaceAdvancedViewArrow();
	// Add space inside the body overlay, not outside its background.
	if (Asset && Asset->MinHeight > 0.0f && BodyBox.IsValid())
	{
		FNodeSlot* Slot = GetSlot(ENodeZone::Center);
		if (Slot)
		{
			Slot->GetWidget()->SlatePrepass(1.0f);
			const float Extra = FMath::Max(0.0f, Asset->MinHeight - Slot->GetWidget()->GetDesiredSize().Y);
			if (Extra > 0.0f) BodyBox->AddSlot().AutoHeight()[SNew(SSpacer).Size(FVector2D(0.0f, Extra))];
		}
	}
	BuildBadges();
	ScheduleSplitRefresh();
}

void SGraphNode_NodeFactory::RefreshIconBrush()
{
	IconTexture.Reset();
	IconTextureBrush = FSlateNoResource();
	CornerTexture.Reset();
	CornerTextureWidget.Reset();
	CornerTextureBrush = FSlateNoResource();

	const UK2Node_CustomNode* Node = GetCustomNode();
	const UCustomNodeAsset* Asset = Node ? Node->GetCustomNodeAsset() : nullptr;
	if (!Asset) return;
	if (!Asset->CornerIconTexture.IsNull())
	{
		UTexture2D* Texture = Asset->CornerIconTexture.LoadSynchronous();
		if (Texture)
		{
			const float Size = FMath::Clamp(Asset->CornerIconTextureSize, 8.0f, 128.0f);
			CornerTexture.Reset(Texture);
			CornerTextureBrush = FSlateImageBrush(Texture, FVector2D(Size, Size));
			CornerTextureWidget = SNew(SImage).Image(&CornerTextureBrush).ColorAndOpacity(Asset->CornerIconTextureTint).Visibility(EVisibility::Visible);
		}
	}
	if (Asset->IconTexture.IsNull()) return;

	// A missing texture is CNA019 of the validator; the node then falls back to Icon (spec 3.2).
	UTexture2D* Texture = Asset->IconTexture.Get();
	if (!Texture)
	{
		Texture = Asset->IconTexture.LoadSynchronous();
	}
	if (!Texture)
	{
		return;
	}

	IconTexture.Reset(Texture);
	IconTextureBrush = FSlateImageBrush(Texture, CNANodeWidgetLocal::IconTextureSize);
}

FSlateColor SGraphNode_NodeFactory::GetIconTextureTint() const
{
	const UK2Node_CustomNode* Node = GetCustomNode();
	const UCustomNodeAsset* Asset = Node ? Node->GetCustomNodeAsset() : nullptr;
	FLinearColor Tint = Asset ? Asset->IconTextureTint : FLinearColor::White;
	if (Node && !Node->IsNodeEnabled())
	{
		Tint.A *= 0.5f;
	}
	return FSlateColor(Tint);
}

EVisibility SGraphNode_NodeFactory::GetIconTextureVisibility() const
{
	return IconTexture.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

FLinearColor SGraphNode_NodeFactory::GetHeaderTextColor() const
{
	const UK2Node_CustomNode* Node = GetCustomNode();
	const UCustomNodeAsset* Asset = Node ? Node->GetCustomNodeAsset() : nullptr;
	FLinearColor Color = Asset ? Asset->HeaderTextColor : FLinearColor::White;
	if (Node && !Node->IsNodeEnabled())
	{
		// Disabled nodes are drawn dimmed by the engine; the title follows.
		Color.A *= 0.5f;
	}
	return Color;
}

TSharedRef<SWidget> SGraphNode_NodeFactory::CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle)
{
	TSharedRef<SWidget> TitleWidget = SGraphNodeK2Base::CreateTitleWidget(NodeTitle);

	// HeaderTextColor replaces the engine color attribute of the title; the subtitle lines of SNodeTitle are
	// tinted with the same color (spec 3.2, V24 V-check: the subtitle widget multiplies its children).
	const TAttribute<FLinearColor> TextColor = TAttribute<FLinearColor>::Create(
		TAttribute<FLinearColor>::FGetter::CreateSP(this, &SGraphNode_NodeFactory::GetHeaderTextColor));
	if (InlineEditableText.IsValid())
	{
		InlineEditableText->SetColorAndOpacity(TextColor);
	}
	if (NodeTitle.IsValid())
	{
		NodeTitle->SetColorAndOpacity(TextColor);
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(SImage)
			.Image(&IconTextureBrush)
			.ColorAndOpacity(this, &SGraphNode_NodeFactory::GetIconTextureTint)
			.Visibility(this, &SGraphNode_NodeFactory::GetIconTextureVisibility)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			TitleWidget
		];
}

// Badges (spec 3.2)

void SGraphNode_NodeFactory::BuildBadges()
{
	BadgeWidgets.Reset();

	const UK2Node_CustomNode* Node = GetCustomNode();
	if (!Node)
	{
		return;
	}

	auto AddBadge = [this](TSharedRef<SWidget> Widget, ECNABadgeCorner Corner)
	{
		FBadgeWidget& Badge = BadgeWidgets[BadgeWidgets.AddDefaulted()];
		Badge.Widget = Widget;
		Badge.Corner = Corner;
	};

	if (Node->IsOutdated())
	{
		AddBadge(CNANodeWidgetLocal::MakeBadge(
			LOCTEXT("NodeWidget.OutdatedBadge", "OUTDATED"),
			CNANodeWidgetLocal::OutdatedBadgeColor,
			FLinearColor::White,
			LOCTEXT("NodeWidget.OutdatedBadgeTooltip", "The asset changed after this node was built (CNA006). Use Refresh Node or recompile the Blueprint.")),
			ECNABadgeCorner::TopLeft);
	}

	if (const UCustomNodeAsset* Asset = Node->GetCustomNodeAsset())
	{
		for (const FCNABadge& Badge : Asset->Badges)
		{
			if (Badge.Text.IsEmpty())
			{
				continue;
			}
			AddBadge(CNANodeWidgetLocal::MakeBadge(Badge.Text, Badge.Color, Badge.TextColor, FText::GetEmpty()), Badge.Corner);
		}
	}
}

TArray<FOverlayWidgetInfo> SGraphNode_NodeFactory::GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const
{
	TArray<FOverlayWidgetInfo> Widgets = SGraphNodeK2Base::GetOverlayWidgets(bSelected, WidgetSize);
	if (CornerTextureWidget.IsValid())
	{
		FOverlayWidgetInfo Info(CornerTextureWidget);
		Info.OverlayOffset = FVector2D(WidgetSize.X - CornerTextureBrush.ImageSize.X * 0.5f, -CornerTextureBrush.ImageSize.Y * 0.5f);
		Widgets.Add(Info);
	}
	if (BadgeWidgets.Num() == 0) return Widgets;

	// Horizontal position reached in every corner; badges of one corner line up next to each other.
	float Advance[4] = { CNANodeWidgetLocal::BadgeMargin, CNANodeWidgetLocal::BadgeMargin, CNANodeWidgetLocal::BadgeMargin, CNANodeWidgetLocal::BadgeMargin };

	if (CornerTextureWidget.IsValid())
		Advance[static_cast<int32>(ECNABadgeCorner::TopRight)] += CornerTextureBrush.ImageSize.X * 0.5f + CNANodeWidgetLocal::BadgeGap;
	// The corner icon of SGraphNodeK2Base (latent clock and the others) sits on the top right corner.
	if (const UK2Node_CustomNode* Node = GetCustomNode())
	{
		const FName CornerIcon = Node->GetCornerIcon();
		if (!CornerIcon.IsNone())
		{
			const FSlateBrush* CornerBrush = FNodeFactoryStyle::FindBrush(CornerIcon);
			Advance[static_cast<int32>(ECNABadgeCorner::TopRight)] += CornerBrush ? CornerBrush->ImageSize.X * 0.5f + CNANodeWidgetLocal::BadgeGap : 12.0f;
		}
	}

	for (const FBadgeWidget& Badge : BadgeWidgets)
	{
		if (!Badge.Widget.IsValid())
		{
			continue;
		}

		// Overlay widgets are outside the child hierarchy; their first desired size needs an explicit prepass.
		FVector2D Size = Badge.Widget->GetDesiredSize();
		if (Size.IsNearlyZero())
		{
			Badge.Widget->SlatePrepass(1.0f);
			Size = Badge.Widget->GetDesiredSize();
		}

		const int32 CornerIndex = FMath::Clamp(static_cast<int32>(Badge.Corner), 0, 3);
		FOverlayWidgetInfo Info(Badge.Widget);
		Info.OverlayOffset.X = CNANodeWidgetLocal::IsRightCorner(Badge.Corner)
			? WidgetSize.X - Advance[CornerIndex] - Size.X
			: Advance[CornerIndex];
		Info.OverlayOffset.Y = CNANodeWidgetLocal::IsBottomCorner(Badge.Corner)
			? WidgetSize.Y - Size.Y * 0.5f
			: -Size.Y * 0.5f;
		Advance[CornerIndex] += Size.X + CNANodeWidgetLocal::BadgeGap;
		Widgets.Add(Info);
	}
	return Widgets;
}

// Body widgets and minimum width (spec 3.2)

void SGraphNode_NodeFactory::CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox)
{
	SGraphNodeK2Base::CreateBelowPinControls(MainBox);
	ArrowBox = MainBox;

	UK2Node_CustomNode* Node = GetCustomNode();
	const UCustomNodeAsset* Asset = Node ? Node->GetCustomNodeAsset() : nullptr;
	if (!Asset || !MainBox.IsValid())
	{
		return;
	}

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	int32 NumRows = 0;
	for (const FCNABodyWidget& Widget : Asset->BodyWidgets)
	{
		UEdGraphPin* Pin = FCNAPinUtils::FindTopLevelPin(Node, Widget.PinName, EGPD_Input);
		// Pins that cannot take the widget are reported by the validator (CNA014) and keep their own field.
		if (!Pin || Pin->bHidden || Pin->bOrphanedPin || !FCNAPinUtils::CanBodyWidgetEditType(Widget.Type, Pin->PinType))
		{
			continue;
		}

		const FText Label = Widget.Label.IsEmpty() ? Node->GetPinDisplayName(Pin) : Widget.Label;
		const FEdGraphPinReference PinRef(Pin);
		Rows->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				.Cursor(EMouseCursor::Default)
				.ToolTipText(FText::FromString(Pin->PinToolTip))
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(Label)
					.ColorAndOpacity(FSlateColor(Node->GetPinLabelColor(Pin)))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(Widget.Width > 0.0f ? FOptionalSize(Widget.Width) : FOptionalSize())
					.IsEnabled_Lambda([PinRef]() { return CNANodeWidgetLocal::IsPinEditable(PinRef.Get()); })
					[
						CreateBodyWidget(Widget, Pin)
					]
				]
			];
		++NumRows;
	}

	if (NumRows > 0)
	{
		MainBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(FMargin(12.0f, 2.0f, 12.0f, 4.0f))
			[
				Rows
			];
	}

	// MinWidth: an invisible spacer keeps the body at least this wide; the pin columns spread apart.
	if (Asset->MinWidth > 0.0f)
	{
		MainBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			[
				SNew(SSpacer)
				.Size(FVector2D(Asset->MinWidth, 0.0f))
			];
	}
}

TSharedRef<SWidget> SGraphNode_NodeFactory::CreateBodyWidget(const FCNABodyWidget& Widget, UEdGraphPin* Pin)
{
	const FEdGraphPinReference PinRef(Pin);
	TWeakPtr<SGraphNode_NodeFactory> WeakThis = SharedThis(this);
	auto IsEditable = [WeakThis, PinRef]() -> bool
	{
		TSharedPtr<SGraphNode_NodeFactory> This = WeakThis.Pin();
		return This.IsValid() && This->IsNodeEditable() && CNANodeWidgetLocal::IsPinEditable(PinRef.Get());
	};

	switch (Widget.Type)
	{
	case ECNABodyWidgetType::CheckBox:
	{
		return SNew(SCheckBox)
			.IsEnabled_Lambda(IsEditable)
			.IsChecked_Lambda([PinRef]()
			{
				const UEdGraphPin* Target = PinRef.Get();
				return (Target && !Target->IsPendingKill() && Target->GetDefaultAsString().ToBool()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([PinRef](ECheckBoxState NewState)
			{
				CNANodeWidgetLocal::SetPinValue(PinRef.Get(), NewState == ECheckBoxState::Checked ? TEXT("true") : TEXT("false"));
			});
	}

	case ECNABodyWidgetType::ComboBox:
	case ECNABodyWidgetType::ScrollList:
	{
		TSharedPtr<FComboData> Data = MakeShareable(new FComboData());
		if (const UEnum* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
		{
			// The same entries as SGraphPinEnum: hidden values and the generated _MAX are left out.
			for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
			{
				if (Enum->HasMetaData(TEXT("Hidden"), Index))
				{
					continue;
				}
				const FString Value = Enum->GetNameStringByIndex(Index);
				if (Widget.Options.Num() > 0 && !Widget.Options.Contains(Value))
				{
					// Options of an enumeration narrow the list to the named values.
					continue;
				}
				Data->Labels.Add(MakeShareable(new FString(Enum->GetDisplayNameTextByIndex(Index).ToString())));
				Data->Values.Add(Value);
			}
		}
		else
		{
			for (const FString& Option : Widget.Options)
			{
				Data->Labels.Add(MakeShareable(new FString(Option)));
				Data->Values.Add(Option);
			}
		}

		// The current value stays selectable even when the options no longer contain it.
		const FString CurrentValue = FCNAPinUtils::GetValueAsString(Pin);
		int32 SelectedIndex = Data->Values.IndexOfByKey(CurrentValue);
		if (SelectedIndex == INDEX_NONE && !CurrentValue.IsEmpty())
		{
			Data->Labels.Add(MakeShareable(new FString(CurrentValue)));
			SelectedIndex = Data->Values.Add(CurrentValue);
		}
		ComboData.Add(Data);

		const bool bTextPin = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text;
		if (Widget.Type == ECNABodyWidgetType::ScrollList)
		{
			TSharedRef<SScrollBox> List = SNew(SScrollBox);
			for (int32 Index = 0; Index < Data->Values.Num(); ++Index)
			{
				const FString Value = Data->Values[Index];
				const FString Caption = Data->Labels[Index].IsValid() ? *Data->Labels[Index] : Value;
				List->AddSlot()[SNew(SButton).IsEnabled_Lambda(IsEditable)
					.Text_Lambda([PinRef, Value, Caption]() { return FText::FromString((FCNAPinUtils::GetValueAsString(PinRef.Get()) == Value ? TEXT("[x] ") : TEXT("[ ] ")) + Caption); })
					.OnClicked_Lambda([PinRef, Value, bTextPin]() { if (bTextPin) CNANodeWidgetLocal::SetPinText(PinRef.Get(), FText::FromString(Value)); else CNANodeWidgetLocal::SetPinValue(PinRef.Get(), Value); return FReply::Handled(); })];
			}
			return SNew(SBox).MaxDesiredHeight(Widget.Height > 0.0f ? Widget.Height : 120.0f)[List];
		}
		// The lambdas hold Data, so the options live exactly as long as the combo box that points at them.
		return SNew(SComboBox<TSharedPtr<FString>>)
			.IsEnabled_Lambda(IsEditable)
			.OptionsSource(&Data->Labels)
			.InitiallySelectedItem(Data->Labels.IsValidIndex(SelectedIndex) ? Data->Labels[SelectedIndex] : TSharedPtr<FString>())
			.ContentPadding(FMargin(4.0f, 1.0f))
			.OnGenerateWidget_Lambda([Data](TSharedPtr<FString> Item) -> TSharedRef<SWidget>
			{
				return SNew(STextBlock)
					.Text(FText::FromString(Item.IsValid() ? *Item : FString()))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));
			})
			.OnSelectionChanged_Lambda([Data, PinRef, bTextPin](TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
			{
				if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct)
				{
					return;
				}
				const int32 Index = Data->Labels.IndexOfByKey(Item);
				if (!Data->Values.IsValidIndex(Index))
				{
					return;
				}
				if (bTextPin)
				{
					CNANodeWidgetLocal::SetPinText(PinRef.Get(), FText::FromString(Data->Values[Index]));
				}
				else
				{
					CNANodeWidgetLocal::SetPinValue(PinRef.Get(), Data->Values[Index]);
				}
			})
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
				.Text_Lambda([Data, PinRef]()
				{
					// Follows undo and the changes of Behavior actions without a rebuild of the widget.
					const FString Value = FCNAPinUtils::GetValueAsString(PinRef.Get());
					const int32 Index = Data->Values.IndexOfByKey(Value);
					return FText::FromString(Data->Labels.IsValidIndex(Index) && Data->Labels[Index].IsValid() ? *Data->Labels[Index] : Value);
				})
			];
	}

	case ECNABodyWidgetType::Slider:
	{
		const float Min = FMath::Clamp(Widget.SliderMin, -1000000.0f, 1000000.0f);
		const float Max = FMath::Clamp(Widget.SliderMax, -1000000.0f, 1000000.0f);
		const bool bInteger = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int;
		TSharedRef<float> Pending = MakeShared<float>(0.0f);
		TSharedRef<bool> Dragging = MakeShared<bool>(false);
		auto Current = [PinRef, Pending, Dragging, Min, Max]() -> float
		{
			if (*Dragging) return *Pending;
			UEdGraphPin* Target = PinRef.Get();
			const float Value = Target && !Target->IsPendingKill() ? FCString::Atof(*Target->GetDefaultAsString()) : Min;
			return Max > Min ? FMath::Clamp((Value - Min) / (Max - Min), 0.0f, 1.0f) : 0.0f;
		};
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SBox).MinDesiredWidth(100.0f)
				[
					SNew(SSlider).IsFocusable(false).IsEnabled_Lambda([IsEditable, Min, Max]() { return Max > Min && IsEditable(); })
					.Value_Lambda(Current)
					.OnMouseCaptureBegin_Lambda([Pending, Dragging, Current]() { *Pending = Current(); *Dragging = true; })
					.OnValueChanged_Lambda([Pending](float Value) { *Pending = Value; })
					.OnMouseCaptureEnd_Lambda([PinRef, Pending, Dragging, Min, Max, bInteger, IsEditable]()
					{
						*Dragging = false;
						if (!IsEditable() || Max <= Min) return;
						const float Value = FMath::Lerp(Min, Max, FMath::Clamp(*Pending, 0.0f, 1.0f));
						CNANodeWidgetLocal::SetPinValue(PinRef.Get(), bInteger ? FString::FromInt(FMath::RoundToInt(Value)) : FString::SanitizeFloat(Value));
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f)
			[
				SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
				.Text_Lambda([Current, Dragging, PinRef, Min, Max, bInteger]() { UEdGraphPin* Target = PinRef.Get(); if (!*Dragging && Target && !Target->IsPendingKill()) return FText::FromString(Target->GetDefaultAsString()); const float Value = FMath::Lerp(Min, Max, Current()); return FText::FromString(bInteger ? FString::FromInt(FMath::RoundToInt(Value)) : FString::SanitizeFloat(Value)); })
			];
	}
	case ECNABodyWidgetType::TextBox:
	default:
	{
		const FEdGraphPinType PinType = Pin->PinType;
		return SNew(SBox)
			.MinDesiredWidth(60.0f)
			[
				SNew(SEditableTextBox)
				.IsEnabled_Lambda(IsEditable)
				.SelectAllTextWhenFocused(true)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
				.Text_Lambda([PinRef]() { return CNANodeWidgetLocal::GetPinValueText(PinRef.Get()); })
				.OnTextCommitted_Lambda([PinRef, PinType](const FText& NewText, ETextCommit::Type CommitType)
				{
					if (CommitType == ETextCommit::OnCleared)
					{
						return;
					}
					UEdGraphPin* Target = PinRef.Get();
					if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
					{
						CNANodeWidgetLocal::SetPinText(Target, NewText);
						return;
					}
					FString Value;
					if (CNANodeWidgetLocal::NormalizeTypedValue(PinType, NewText.ToString(), Value))
					{
						CNANodeWidgetLocal::SetPinValue(Target, Value);
					}
				})
			];
	}
	}
}

// Dynamic pins (spec 4.4, V4)

void SGraphNode_NodeFactory::CreateInputSideAddButton(TSharedPtr<SVerticalBox> InputBox)
{
	const UK2Node_CustomNode* Node = GetCustomNode();
	if (!Node || !Node->HasDynamicPins() || !InputBox.IsValid())
	{
		return;
	}

	TSharedRef<SWidget> AddPinButton = AddPinButtonContent(
		LOCTEXT("NodeWidget.AddDynamicPinButton", "Add pin"),
		LOCTEXT("NodeWidget.AddDynamicPinButtonTooltip", "Add an element pin to the dynamic group of this node."),
		false);

	// Same spacing as the add button of the engine Sequence node (V-check: UGraphEditorSettings::GetInputPinPadding).
	FMargin AddPinPadding = GetDefault<UGraphEditorSettings>()->GetInputPinPadding();
	AddPinPadding.Top += 6.0f;

	InputBox->AddSlot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		.Padding(AddPinPadding)
		[
			AddPinButton
		];
}

FReply SGraphNode_NodeFactory::OnAddPin()
{
	UK2Node_CustomNode* Node = GetCustomNode();
	if (Node && Node->CanAddDynamicElement())
	{
		// AddDynamicElement opens its own transaction and raises OnAddPin (spec 6.2).
		Node->AddDynamicElement();
		UpdateGraphNode();
		if (UEdGraph* Graph = Node->GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}
	return FReply::Handled();
}

EVisibility SGraphNode_NodeFactory::IsAddPinButtonVisible() const
{
	const UK2Node_CustomNode* Node = GetCustomNode();
	if (!Node || !Node->CanAddDynamicElement() || !IsNodeEditable())
	{
		return EVisibility::Collapsed;
	}
	return SGraphNodeK2Base::IsAddPinButtonVisible();
}

// Advanced arrow (spec 6.2, V16, V24)

void SGraphNode_NodeFactory::ReplaceAdvancedViewArrow()
{
	// SGraphNode::UpdateGraphNode adds its arrow (an SCheckBox) right after CreateBelowPinControls. The same widget
	// takes its place with another handler, so that OnAdvancedViewToggled is raised (no hook for the toggle, V16).
	const TSharedPtr<SVerticalBox> MainBox = ArrowBox;
	ArrowBox.Reset();
	FChildren* Children = MainBox.IsValid() ? MainBox->GetChildren() : nullptr;
	if (!GraphNode || !Children || Children->Num() == 0 || Children->GetChildAt(Children->Num() - 1)->GetType() != FName(TEXT("SCheckBox")))
	{
		return;
	}
	MainBox->RemoveSlot(Children->GetChildAt(Children->Num() - 1));

	MainBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Top)
		.Padding(3.0f, 0.0f, 3.0f, 3.0f)
		[
			SNew(SCheckBox)
			.Visibility(this, &SGraphNode_NodeFactory::GetAdvancedArrowVisibility)
			.OnCheckStateChanged(this, &SGraphNode_NodeFactory::OnAdvancedArrowChanged)
			.IsChecked(this, &SGraphNode_NodeFactory::GetAdvancedArrowState)
			.Cursor(EMouseCursor::Default)
			.Style(FEditorStyle::Get(), "Graph.Node.AdvancedView")
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(this, &SGraphNode_NodeFactory::GetAdvancedArrowImage)
				]
			]
		];
}

EVisibility SGraphNode_NodeFactory::GetAdvancedArrowVisibility() const
{
	return (GraphNode && GraphNode->AdvancedPinDisplay != ENodeAdvancedPins::NoPins) ? EVisibility::Visible : EVisibility::Collapsed;
}

ECheckBoxState SGraphNode_NodeFactory::GetAdvancedArrowState() const
{
	return (GraphNode && GraphNode->AdvancedPinDisplay == ENodeAdvancedPins::Hidden) ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
}

const FSlateBrush* SGraphNode_NodeFactory::GetAdvancedArrowImage() const
{
	const bool bHidden = GraphNode && GraphNode->AdvancedPinDisplay == ENodeAdvancedPins::Hidden;
	return FEditorStyle::GetBrush(bHidden ? TEXT("Kismet.TitleBarEditor.ArrowDown") : TEXT("Kismet.TitleBarEditor.ArrowUp"));
}

void SGraphNode_NodeFactory::OnAdvancedArrowChanged(ECheckBoxState NewState)
{
	if (UK2Node_CustomNode* Node = GetCustomNode())
	{
		// Changes AdvancedPinDisplay inside a transaction and raises OnAdvancedViewToggled.
		Node->SetAdvancedViewExpanded(NewState == ECheckBoxState::Checked);
	}
}

// Split pins (V15)

void SGraphNode_NodeFactory::ScheduleSplitRefresh()
{
	UK2Node_CustomNode* Node = GetCustomNode();
	if (!Node || Node->IsPreviewNode())
	{
		return;
	}

	// Splitting and recombining rebuild the widget through NotifyGraphChanged; the comparison with the snapshot
	// runs on the next tick, outside of the schema transaction (V15: the engine has no split notification).
	const TWeakObjectPtr<UK2Node_CustomNode> WeakNode(Node);
	FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakNode](float DeltaTime) -> bool
	{
		UK2Node_CustomNode* Target = WeakNode.Get();
		if (Target && !Target->IsPendingKill() && Target->GetGraph())
		{
			Target->RefreshSplitPinState(true);
		}
		return false;
	}));
}

// SGraphNodeCNABehavior (spec 6.5)

void SGraphNodeCNABehavior::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	// SGraphNodeK2Event draws the delegate pin of events inside the title; this widget does not, so an
	// unconnected delegate pin is left out instead of standing as an extra output.
	const UEdGraphPin* Pin = PinToAdd->GetPinObj();
	if (Pin && Cast<UK2Node_Event>(GraphNode) && Pin->PinName == UK2Node_Event::DelegateOutputName && Pin->LinkedTo.Num() == 0)
	{
		return;
	}
	SGraphNodeK2Default::AddPin(PinToAdd);
}

TSharedRef<SWidget> SGraphNodeCNABehavior::CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle)
{
	TSharedRef<SWidget> TitleWidget = SGraphNodeK2Default::CreateTitleWidget(NodeTitle);

	const FSlateBrush* Brush = FNodeFactoryStyle::FindBrush(FNodeFactoryStyle::BadgeBrushName);
	if (!Brush)
	{
		Brush = FEditorStyle::GetBrush(TEXT("WhiteBrush"));
	}
	FLinearColor Accent = CNATypes::GetAccentColor();
	Accent.A = 0.85f;

	return SNew(SBorder)
		.BorderImage(Brush)
		.BorderBackgroundColor(Accent)
		.Padding(FMargin(4.0f, 1.0f))
		[
			TitleWidget
		];
}

// SGraphNodeCNALocalVariable (spec 4.8)

TSharedRef<SWidget> SGraphNodeCNALocalVariable::CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle)
{
	TSharedRef<SWidget> TitleWidget = SGraphNodeK2Default::CreateTitleWidget(NodeTitle);

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 5.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(10.0f)
			.HeightOverride(10.0f)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush(TEXT("WhiteBrush")))
				.ColorAndOpacity(this, &SGraphNodeCNALocalVariable::GetVariableColor)
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			TitleWidget
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SComboButton)
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ForegroundColor(FSlateColor::UseForeground())
			.ContentPadding(FMargin(1.0f, 0.0f))
			.HasDownArrow(true)
			.IsEnabled_Lambda([this]() { return IsNodeEditable(); })
			.ToolTipText(LOCTEXT("NodeWidget.ChooseLocalVariableTooltip", "Choose the local variable of the asset this node reads or writes."))
			.OnGetMenuContent(this, &SGraphNodeCNALocalVariable::MakeVariableMenu)
			.ButtonContent()
			[
				SNullWidget::NullWidget
			]
		];
}

FSlateColor SGraphNodeCNALocalVariable::GetVariableColor() const
{
	const UK2Node_CNALocalVariableBase* Node = Cast<UK2Node_CNALocalVariableBase>(GraphNode);
	if (!Node)
	{
		return FSlateColor(FLinearColor::White);
	}
	FLinearColor Color = FCNAPinUtils::GetTypeColor(Node->GetVariableType());
	if (!Node->IsVariableValid())
	{
		// Missing variable (CNA017): the marker turns grey.
		Color = FLinearColor(0.35f, 0.35f, 0.35f, 1.0f);
	}
	return FSlateColor(Color);
}

TSharedRef<SWidget> SGraphNodeCNALocalVariable::MakeVariableMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	UK2Node_CNALocalVariableBase* Node = Cast<UK2Node_CNALocalVariableBase>(GraphNode);
	TArray<const FCNALocalVariable*> Variables;
	if (Node)
	{
		Node->GetSelectableVariables(Variables);
	}

	MenuBuilder.BeginSection(TEXT("CNALocalVariables"), LOCTEXT("NodeWidget.LocalVariablesSection", "Local Variables"));
	if (Variables.Num() == 0)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("NodeWidget.NoLocalVariables", "No local variables"),
			LOCTEXT("NodeWidget.NoLocalVariablesTooltip", "Add variables in the Local Variables panel of the asset."),
			FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}

	const TWeakObjectPtr<UK2Node_CNALocalVariableBase> WeakNode(Node);
	for (const FCNALocalVariable* Variable : Variables)
	{
		const FGuid Guid = Variable->Guid;
		MenuBuilder.AddMenuEntry(
			FText::FromName(Variable->Name),
			FText::Format(LOCTEXT("NodeWidget.LocalVariableEntryTooltip", "{0}\n{1}"), FCNAPinUtils::GetTypeText(Variable->Type), Variable->Tooltip),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakNode, Guid]()
				{
					UK2Node_CNALocalVariableBase* Target = WeakNode.Get();
					const UCustomNodeAsset* Asset = Target ? Target->GetOwningAsset() : nullptr;
					const FCNALocalVariable* Chosen = Asset ? Asset->FindLocalVariable(Guid) : nullptr;
					if (Chosen)
					{
						Target->SetVariable(*Chosen);
						if (UEdGraph* Graph = Target->GetGraph())
						{
							Graph->NotifyGraphChanged();
						}
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([WeakNode, Guid]()
				{
					const UK2Node_CNALocalVariableBase* Target = WeakNode.Get();
					return Target && Target->VariableGuid == Guid;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
