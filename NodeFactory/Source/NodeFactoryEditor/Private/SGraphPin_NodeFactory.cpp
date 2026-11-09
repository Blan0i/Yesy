// Copyright (c) NodeFactory. Pin widgets of custom nodes (spec 4.7, V25).

#include "SGraphPin_NodeFactory.h"
#include "CNAPinUtils.h"
#include "CNATypes.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "NodeFactoryStyle.h"
#include "SGraphNode_NodeFactory.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"

#if CNA_WITH_STYLED_PINS
#include "KismetPins/SGraphPinBool.h"
#include "KismetPins/SGraphPinColor.h"
#include "KismetPins/SGraphPinExec.h"
#include "KismetPins/SGraphPinInteger.h"
#include "KismetPins/SGraphPinNum.h"
#include "KismetPins/SGraphPinObject.h"
#include "KismetPins/SGraphPinString.h"
#include "KismetPins/SGraphPinVector.h"
#include "KismetPins/SGraphPinVector2D.h"
#endif
#include "CNALocalization.h"

namespace CNAPinStyleLocal
{
	const UCustomNodeAsset* GetAsset(const UEdGraphPin* Pin)
	{
		const UK2Node_CustomNode* Node = FCNAPinStyle::GetCustomNode(Pin);
		return Node ? Node->GetCustomNodeAsset() : nullptr;
	}

	/** Override of the pin; element pins of a dynamic group use the override of the group. */
	const FCNAPinOverride* FindOverride(const UCustomNodeAsset& Asset, FName PinName)
	{
		if (const FCNAPinOverride* Override = Asset.FindPinOverride(PinName))
		{
			return Override;
		}
		FName BaseName;
		int32 Index = 0;
		return CNATypes::ParseDynamicPinName(PinName, BaseName, Index) ? Asset.FindPinOverride(BaseName) : nullptr;
	}
}

void SGraphPin_NodeFactory::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InPin);
}

// FCNAPinStyle

const UK2Node_CustomNode* FCNAPinStyle::GetCustomNode(const UEdGraphPin* Pin)
{
	return Pin ? Cast<UK2Node_CustomNode>(Pin->GetOwningNodeUnchecked()) : nullptr;
}

bool FCNAPinStyle::GetPinColor(const UEdGraphPin* Pin, FLinearColor& OutColor)
{
	const UK2Node_CustomNode* Node = GetCustomNode(Pin);
	return Node && Node->GetPinColorOverride(Pin, OutColor);
}

bool FCNAPinStyle::GetLabelColor(const UEdGraphPin* Pin, FLinearColor& OutColor)
{
	const UK2Node_CustomNode* Node = GetCustomNode(Pin);
	if (!Node || !Node->GetCustomNodeAsset())
	{
		return false;
	}
	OutColor = Node->GetPinLabelColor(Pin);
	return true;
}

bool FCNAPinStyle::HasBodyWidget(const UEdGraphPin* Pin)
{
	const UK2Node_CustomNode* Node = GetCustomNode(Pin);
	if (!Node || Pin->ParentPin || Pin->Direction != EGPD_Input)
	{
		return false;
	}
	// Compact nodes have no body (SGraphNodeK2Base::UpdateCompactNode), so their pins keep the own field. The
	// state and transition looks have their own layout with a body in every case.
	if (Node->ShouldDrawCompact() && SGraphNode_NodeFactory::GetLayoutForAsset(Node->GetCustomNodeAsset()) == ECNANodeLayout::Standard)
	{
		return false;
	}
	const FCNABodyWidget* Widget = Node->FindBodyWidget(Pin->PinName);
	// A Curve widget is not bound to a pin, even when a pin has the same name.
	return Widget && !Widget->IsCurve() && FCNAPinUtils::CanBodyWidgetEditType(Widget->Type, Pin->PinType);
}

ECNAPinShape FCNAPinStyle::GetPinShape(const UEdGraphPin* Pin)
{
	const UCustomNodeAsset* Asset = CNAPinStyleLocal::GetAsset(Pin);
	if (!Asset || Pin->bOrphanedPin)
	{
		return ECNAPinShape::Default;
	}
	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		if (Asset->NodeStyle == ECNANodeStyle::Animation && Asset->bExecPinsAsPosePins)
		{
			return ECNAPinShape::Pose;
		}
		if (Asset->NodeStyle == ECNANodeStyle::Niagara && Asset->bExecPinsAsParameterMap)
		{
			return ECNAPinShape::ParameterMap;
		}
	}
	// Sub-pins of a split structure keep their own look.
	if (!Pin->ParentPin)
	{
		const FCNAPinOverride* Override = CNAPinStyleLocal::FindOverride(*Asset, Pin->PinName);
		if (Override && Override->bDrawAsPosePin)
		{
			return ECNAPinShape::Pose;
		}
	}
	return ECNAPinShape::Default;
}

const FSlateBrush* FCNAPinStyle::GetShapedPinIcon(const UEdGraphPin* Pin, bool bConnected, bool bHovered)
{
	switch (GetPinShape(Pin))
	{
	case ECNAPinShape::Pose:
		// The brushes of SGraphPinPose (4.20.3); the hovered variants fall back to the plain ones.
		if (bConnected)
		{
			return bHovered ? FindBrush(TEXT("Graph.PosePin.ConnectedHovered"), TEXT("Graph.PosePin.Connected")) : FindBrush(TEXT("Graph.PosePin.Connected"), TEXT("Graph.Pin.Connected"));
		}
		return bHovered ? FindBrush(TEXT("Graph.PosePin.DisconnectedHovered"), TEXT("Graph.PosePin.Disconnected")) : FindBrush(TEXT("Graph.PosePin.Disconnected"), TEXT("Graph.Pin.Disconnected"));

	case ECNAPinShape::ParameterMap:
		// Niagara draws the Parameter Map with the default round pin of SGraphPin in the execution color.
		return FindBrush(bConnected ? TEXT("Graph.Pin.Connected") : TEXT("Graph.Pin.Disconnected"));

	case ECNAPinShape::Default:
	default:
		return nullptr;
	}
}

bool FCNAPinStyle::UseNamespaceLabel(const UEdGraphPin* Pin)
{
	const UCustomNodeAsset* Asset = CNAPinStyleLocal::GetAsset(Pin);
	return Asset && Asset->NodeStyle == ECNANodeStyle::Niagara && Asset->bNiagaraNamespaceLabels;
}

void FCNAPinStyle::SplitNamespaceLabel(const FText& Label, FText& OutNamespace, FText& OutName)
{
	const FString String = Label.ToString();
	int32 Dot = INDEX_NONE;
	// A trailing dot is part of the name, not a namespace separator.
	if (String.FindLastChar(TEXT('.'), Dot) && Dot > 0 && Dot < String.Len() - 1)
	{
		OutNamespace = FText::FromString(String.Left(Dot + 1));
		OutName = FText::FromString(String.Mid(Dot + 1));
		return;
	}
	OutNamespace = FText::GetEmpty();
	OutName = Label;
}

bool FCNAPinStyle::IgnoresReadOnlyDimming(const UEdGraphPin* Pin)
{
	const UK2Node_CustomNode* Node = GetCustomNode(Pin);
	return Node && Node->IsPreviewNode() && Node->IsNodeEnabled() && !Pin->bOrphanedPin;
}

const FSlateBrush* FCNAPinStyle::FindBrush(FName Name, FName Fallback)
{
	const FSlateBrush* Brush = FNodeFactoryStyle::FindBrush(Name);
	return Brush ? Brush : FNodeFactoryStyle::FindBrush(Fallback);
}

TSharedPtr<SGraphPin> FCNAPinStyle::CreatePinWidget(UEdGraphPin* Pin)
{
	if (!Pin || !GetCustomNode(Pin))
	{
		return nullptr;
	}

#if CNA_WITH_STYLED_PINS
	// The same choice of widgets as FNodeFactory::CreatePinWidget of 4.20.3 (V25).
	const FEdGraphPinType& Type = Pin->PinType;
	if (Type.IsContainer())
	{
		return SNew(SGraphPin_NodeFactory, Pin);
	}

	const FName Category = Type.PinCategory;
	if ((Category == UEdGraphSchema_K2::PC_Byte && Cast<UEnum>(Type.PinSubCategoryObject.Get())) || Category == UEdGraphSchema_K2::PC_Class
		|| Category == UEdGraphSchema_K2::PC_SoftClass)
	{
		// SGraphPinEnum and SGraphPinClass are private in 4.20.3 (V25): the engine widget and the type color,
		// or the plain pin of the plugin when a body widget edits the value.
		return FCNAPinStyle::HasBodyWidget(Pin) ? TSharedPtr<SGraphPin>(SNew(SGraphPin_NodeFactory, Pin)) : nullptr;
	}
	if (Category == UEdGraphSchema_K2::PC_Exec)
	{
		return SNew(TCNAStyledPin<SGraphPinExec>, Pin);
	}
	if (Category == UEdGraphSchema_K2::PC_Boolean)
	{
		return SNew(TCNAStyledPin<SGraphPinBool>, Pin);
	}
	if (Category == UEdGraphSchema_K2::PC_Int || Category == UEdGraphSchema_K2::PC_Byte)
	{
		return SNew(TCNAStyledPin<SGraphPinInteger>, Pin);
	}
	if (Category == UEdGraphSchema_K2::PC_Float)
	{
		return SNew(TCNAStyledPin<SGraphPinNum<float>>, Pin);
	}
	if (Category == UEdGraphSchema_K2::PC_String || Category == UEdGraphSchema_K2::PC_Name)
	{
		return SNew(TCNAStyledPin<SGraphPinString>, Pin);
	}
	if (Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Interface || Category == UEdGraphSchema_K2::PC_SoftObject)
	{
		return SNew(TCNAStyledPin<SGraphPinObject>, Pin);
	}
	if (Category == UEdGraphSchema_K2::PC_Struct)
	{
		const UObject* Struct = Type.PinSubCategoryObject.Get();
		if (Struct == TBaseStructure<FLinearColor>::Get())
		{
			return SNew(TCNAStyledPin<SGraphPinColor>, Pin);
		}
		if (Struct == TBaseStructure<FVector>::Get() || Struct == TBaseStructure<FRotator>::Get())
		{
			return SNew(TCNAStyledPin<SGraphPinVector>, Pin);
		}
		if (Struct == TBaseStructure<FVector2D>::Get())
		{
			return SNew(TCNAStyledPin<SGraphPinVector2D>, Pin);
		}
		// Other structures (Key, Collision Profile...) may have pin factories of their own modules.
		return nullptr;
	}
	if (Category == UEdGraphSchema_K2::PC_Wildcard && Type.PinSubCategory != UEdGraphSchema_K2::PSC_Index)
	{
		return SNew(SGraphPin_NodeFactory, Pin);
	}
#endif

	// Text, index wildcards and everything else: the engine widget.
	return nullptr;
}
