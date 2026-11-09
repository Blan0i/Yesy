// Copyright (c) NodeFactory. Pin type helpers (spec 4).

#include "CNAPinUtils.h"
#include "CNATypes.h"
#include "CustomNodeAsset.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

// FCNASignaturePin

bool FCNASignaturePin::IsExec() const
{
	return FCNAPinUtils::IsExecType(Type);
}
bool FCNASignaturePin::IsWildcard() const
{
	return FCNAPinUtils::IsWildcardType(Type);
}
// Types

bool FCNAPinUtils::IsExecType(const FEdGraphPinType& Type)
{
	return Type.PinCategory == UEdGraphSchema_K2::PC_Exec;
}
bool FCNAPinUtils::IsWildcardType(const FEdGraphPinType& Type)
{
	return Type.PinCategory == UEdGraphSchema_K2::PC_Wildcard;
}
bool FCNAPinUtils::IsExecPin(const UEdGraphPin* Pin)
{
	return Pin && IsExecType(Pin->PinType);
}
bool FCNAPinUtils::IsReferenceCategory(FName Category)
{
	return Category == UEdGraphSchema_K2::PC_Object
		|| Category == UEdGraphSchema_K2::PC_Class
		|| Category == UEdGraphSchema_K2::PC_Interface
		|| Category == UEdGraphSchema_K2::PC_SoftObject
		|| Category == UEdGraphSchema_K2::PC_SoftClass;
}
FEdGraphPinType FCNAPinUtils::GetElementType(const FEdGraphPinType& Type)
{
	FEdGraphPinType Element = Type;
	Element.ContainerType = EPinContainerType::None;
	Element.PinValueType = FEdGraphTerminalType();
	Element.bIsReference = false;
	return Element;
}
FEdGraphPinType FCNAPinUtils::MakeContainerType(const FEdGraphPinType& ElementType, EPinContainerType Container)
{
	FEdGraphPinType Result = GetElementType(ElementType);
	Result.ContainerType = Container;
	return Result;
}
bool FCNAPinUtils::AreTypesIdentical(const FEdGraphPinType& A, const FEdGraphPinType& B)
{
	if (A.PinCategory != B.PinCategory
		|| A.PinSubCategory != B.PinSubCategory
		|| A.PinSubCategoryObject.Get() != B.PinSubCategoryObject.Get()
		|| A.ContainerType != B.ContainerType)
	{
		return false;
	}
	if (A.IsMap())
	{
		return A.PinValueType.TerminalCategory == B.PinValueType.TerminalCategory
			&& A.PinValueType.TerminalSubCategory == B.PinValueType.TerminalSubCategory
			&& A.PinValueType.TerminalSubCategoryObject.Get() == B.PinValueType.TerminalSubCategoryObject.Get();
	}
	return true;
}
bool FCNAPinUtils::AreTypesCompatible(const FEdGraphPinType& Output, const FEdGraphPinType& Input)
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	return Schema->ArePinTypesCompatible(Output, Input);
}
void FCNAPinUtils::ApplyResolvedWildcard(FEdGraphPinType& InOutType, const FEdGraphPinType& Resolved)
{
	// Same rule as UK2Node_MacroInstance: only the category part is copied, the container of the
	// macro pin stays (spec 4.5, item 1; V14).
	if (!IsWildcardType(InOutType) || Resolved.PinCategory.IsNone() || IsWildcardType(Resolved))
	{
		return;
	}
	InOutType.PinCategory = Resolved.PinCategory;
	InOutType.PinSubCategory = Resolved.PinSubCategory;
	InOutType.PinSubCategoryObject = Resolved.PinSubCategoryObject;
	InOutType.PinSubCategoryMemberReference = Resolved.PinSubCategoryMemberReference;
}
FText FCNAPinUtils::GetTypeText(const FEdGraphPinType& Type)
{
	return UEdGraphSchema_K2::TypeToText(Type);
}
FString FCNAPinUtils::GetTypeSignatureString(const FEdGraphPinType& Type)
{
	const UObject* SubObject = Type.PinSubCategoryObject.Get();
	const UObject* ValueObject = Type.PinValueType.TerminalSubCategoryObject.Get();
	return FString::Printf(TEXT("%s|%s|%s|%d|%s|%s|%s|%d|%d"),
		*Type.PinCategory.ToString(),
		*Type.PinSubCategory.ToString(),
		SubObject ? *SubObject->GetPathName() : TEXT(""),
		static_cast<int32>(Type.ContainerType),
		*Type.PinValueType.TerminalCategory.ToString(),
		*Type.PinValueType.TerminalSubCategory.ToString(),
		ValueObject ? *ValueObject->GetPathName() : TEXT(""),
		Type.bIsReference ? 1 : 0,
		Type.bIsConst ? 1 : 0);
}
FLinearColor FCNAPinUtils::GetTypeColor(const FEdGraphPinType& Type)
{
	return GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(Type);
}
bool FCNAPinUtils::IsTypeObjectMissing(const FEdGraphPinType& Type)
{
	auto NeedsObject = [](FName Category, FName SubCategory) -> bool
	{
		if (Category == UEdGraphSchema_K2::PC_Struct)
		{
			return true;
		}
		if (IsReferenceCategory(Category))
		{
			// "self" references have no class object by design.
			return SubCategory != UEdGraphSchema_K2::PSC_Self;
		}
		return false;
	};

	if (NeedsObject(Type.PinCategory, Type.PinSubCategory) && !Type.PinSubCategoryObject.IsValid())
	{
		return true;
	}
	if (Type.IsMap() && NeedsObject(Type.PinValueType.TerminalCategory, Type.PinValueType.TerminalSubCategory)
		&& !Type.PinValueType.TerminalSubCategoryObject.IsValid())
	{
		return true;
	}
	return false;
}
bool FCNAPinUtils::CanBeLocalVariableType(const FEdGraphPinType& Type)
{
	return !Type.PinCategory.IsNone() && !IsExecType(Type) && !IsWildcardType(Type)
		&& Type.PinCategory != UEdGraphSchema_K2::PC_Delegate
		&& Type.PinCategory != UEdGraphSchema_K2::PC_MCDelegate;
}
// Values

bool FCNAPinUtils::IsValueEmpty(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return true;
	}
	const FEdGraphPinType& Type = Pin->PinType;
	if (Type.IsContainer())
	{
		return true;
	}
	const FName Category = Type.PinCategory;
	if (Category == UEdGraphSchema_K2::PC_Exec || Category == UEdGraphSchema_K2::PC_Wildcard)
	{
		return false;
	}
	if (IsReferenceCategory(Category))
	{
		if (Type.PinSubCategory == UEdGraphSchema_K2::PSC_Self)
		{
			return false;
		}
		return Pin->DefaultObject == nullptr && Pin->DefaultValue.IsEmpty();
	}
	if (Category == UEdGraphSchema_K2::PC_Text)
	{
		return Pin->DefaultTextValue.IsEmpty();
	}
	if (Category == UEdGraphSchema_K2::PC_Name)
	{
		return Pin->DefaultValue.IsEmpty() || Pin->DefaultValue == TEXT("None");
	}
	return Pin->DefaultValue.IsEmpty();
}
void FCNAPinUtils::CopyDefaultValue(const UEdGraphPin* From, UEdGraphPin* To)
{
	if (From && To)
	{
		To->DefaultValue = From->DefaultValue;
		To->DefaultObject = From->DefaultObject;
		To->DefaultTextValue = From->DefaultTextValue;
	}
}
void FCNAPinUtils::ApplySignatureDefault(const FCNASignaturePin& From, UEdGraphPin* To)
{
	if (!To)
	{
		return;
	}
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	Schema->SetPinAutogeneratedDefaultValueBasedOnType(To);

	if (From.DefaultObject.IsValid())
	{
		To->DefaultObject = From.DefaultObject.Get();
	}
	else if (!From.DefaultText.IsEmpty())
	{
		To->DefaultTextValue = From.DefaultText;
	}
	else if (!From.DefaultValue.IsEmpty() && !To->PinType.IsContainer())
	{
		To->DefaultValue = From.DefaultValue;
	}
}
FString FCNAPinUtils::GetValueAsString(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return FString();
	}
	if (Pin->DefaultObject)
	{
		return Pin->DefaultObject->GetPathName();
	}
	if (!Pin->DefaultTextValue.IsEmpty())
	{
		return Pin->DefaultTextValue.ToString();
	}
	return Pin->DefaultValue;
}
// Pins

FName FCNAPinUtils::GetBaseName(FName PinName)
{
	FName BaseName;
	int32 Index = INDEX_NONE;
	if (CNATypes::ParseDynamicPinName(PinName, BaseName, Index))
	{
		return BaseName;
	}
	return PinName;
}
int32 FCNAPinUtils::GetDynamicIndex(FName PinName)
{
	FName BaseName;
	int32 Index = INDEX_NONE;
	if (CNATypes::ParseDynamicPinName(PinName, BaseName, Index))
	{
		return Index;
	}
	return INDEX_NONE;
}
UEdGraphPin* FCNAPinUtils::FindTopLevelPin(const UEdGraphNode* Node, FName PinName, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->ParentPin == nullptr && Pin->PinName == PinName
			&& (Direction == EGPD_MAX || Pin->Direction == Direction))
		{
			return Pin;
		}
	}
	return nullptr;
}
bool FCNAPinUtils::IsPinOrSubPinLinked(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return false;
	}
	if (Pin->LinkedTo.Num() > 0)
	{
		return true;
	}
	for (const UEdGraphPin* SubPin : Pin->SubPins)
	{
		if (IsPinOrSubPinLinked(SubPin))
		{
			return true;
		}
	}
	return false;
}
FText FCNAPinUtils::GetPinDisplayText(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return FText::GetEmpty();
	}
	if (!Pin->PinFriendlyName.IsEmpty())
	{
		return Pin->PinFriendlyName;
	}
	return FText::FromName(Pin->PinName);
}
const FCNAPinOverride* FCNAPinUtils::FindOverrideForPin(const UCustomNodeAsset* Asset, FName PinName)
{
	if (!Asset)
	{
		return nullptr;
	}
	if (const FCNAPinOverride* Direct = Asset->FindPinOverride(PinName))
	{
		return Direct;
	}
	const FName BaseName = GetBaseName(PinName);
	if (BaseName != PinName)
	{
		return Asset->FindPinOverride(BaseName);
	}
	return nullptr;
}
void FCNAPinUtils::BreakAllLinksRecursive(UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return;
	}
	for (UEdGraphPin* SubPin : Pin->SubPins)
	{
		BreakAllLinksRecursive(SubPin);
	}
	Pin->BreakAllPinLinks(true);
}
bool FCNAPinUtils::IsEnumType(const FEdGraphPinType& Type)
{
	return Type.PinCategory == UEdGraphSchema_K2::PC_Byte && Cast<UEnum>(Type.PinSubCategoryObject.Get()) != nullptr;
}
bool FCNAPinUtils::CanBodyWidgetEditType(ECNABodyWidgetType WidgetType, const FEdGraphPinType& Type)
{
	if (Type.IsContainer())
	{
		return false;
	}
	const FName Category = Type.PinCategory;
	const bool bTextual = Category == UEdGraphSchema_K2::PC_String
		|| Category == UEdGraphSchema_K2::PC_Name
		|| Category == UEdGraphSchema_K2::PC_Text;
	switch (WidgetType)
	{
	case ECNABodyWidgetType::CheckBox:
		return Category == UEdGraphSchema_K2::PC_Boolean;
	case ECNABodyWidgetType::ComboBox:
	case ECNABodyWidgetType::ScrollList:
		return bTextual || IsEnumType(Type);
	case ECNABodyWidgetType::Slider:
		return Category == UEdGraphSchema_K2::PC_Int || Category == UEdGraphSchema_K2::PC_Float;
	case ECNABodyWidgetType::TextBox:
		return bTextual || Category == UEdGraphSchema_K2::PC_Int || Category == UEdGraphSchema_K2::PC_Float;
	default:
		return false;
	}
}
#undef LOCTEXT_NAMESPACE
