// Copyright (c) NodeFactory. Pin widgets of custom nodes (spec 4.7, V25).

#include "SGraphPin_NodeFactory.h"
#include "CNAPinUtils.h"
#include "CNATypes.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"

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
	// Compact nodes have no body (SGraphNodeK2Base::UpdateCompactNode), so their pins keep the own field.
	if (!Node || Pin->ParentPin || Pin->Direction != EGPD_Input || Node->ShouldDrawCompact())
	{
		return false;
	}
	const FCNABodyWidget* Widget = Node->FindBodyWidget(Pin->PinName);
	return Widget && FCNAPinUtils::CanBodyWidgetEditType(Widget->Type, Pin->PinType);
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
