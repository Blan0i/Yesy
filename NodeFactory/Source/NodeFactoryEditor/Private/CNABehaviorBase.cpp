// Copyright (c) NodeFactory. Parent class of every Custom Node Asset (spec 6, D4).

#include "CNABehaviorBase.h"
#include "CNABehaviorHost.h"
#include "CNAPinUtils.h"
#include "K2Node_CustomNode.h"
#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

const FName UCNABehaviorBase::OnNodePlacedName(TEXT("OnNodePlaced"));
const FName UCNABehaviorBase::OnWireChangedName(TEXT("OnWireChanged"));
const FName UCNABehaviorBase::OnPinValueChangedName(TEXT("OnPinValueChanged"));
const FName UCNABehaviorBase::OnAddPinName(TEXT("OnAddPin"));
const FName UCNABehaviorBase::OnRemovePinName(TEXT("OnRemovePin"));
const FName UCNABehaviorBase::OnPinSplitName(TEXT("OnPinSplit"));
const FName UCNABehaviorBase::OnPinRecombinedName(TEXT("OnPinRecombined"));
const FName UCNABehaviorBase::OnAdvancedViewToggledName(TEXT("OnAdvancedViewToggled"));

// FCNAPinTypeDesc

FEdGraphPinType FCNAPinTypeDesc::ToPinType() const
{
	FEdGraphPinType Type;
	Type.PinCategory = Category;
	Type.PinSubCategory = SubCategory;
	Type.PinSubCategoryObject = SubCategoryObject;
	switch (Container)
	{
	case ECNAPinContainer::Array: Type.ContainerType = EPinContainerType::Array; break;
	case ECNAPinContainer::Set:   Type.ContainerType = EPinContainerType::Set; break;
	case ECNAPinContainer::Map:   Type.ContainerType = EPinContainerType::Map; break;
	default:                      Type.ContainerType = EPinContainerType::None; break;
	}
	return Type;
}

FCNAPinTypeDesc FCNAPinTypeDesc::FromPinType(const FEdGraphPinType& Type)
{
	FCNAPinTypeDesc Desc;
	Desc.Category = Type.PinCategory;
	Desc.SubCategory = Type.PinSubCategory;
	Desc.SubCategoryObject = Type.PinSubCategoryObject.Get();
	switch (Type.ContainerType)
	{
	case EPinContainerType::Array: Desc.Container = ECNAPinContainer::Array; break;
	case EPinContainerType::Set:   Desc.Container = ECNAPinContainer::Set; break;
	case EPinContainerType::Map:   Desc.Container = ECNAPinContainer::Map; break;
	default:                       Desc.Container = ECNAPinContainer::Single; break;
	}
	return Desc;
}

bool FCNAPinTypeDesc::IsWildcard() const
{
	return Category == UEdGraphSchema_K2::PC_Wildcard;
}

// Actions

void UCNABehaviorBase::ReportRefusedAction(const FText& Details) const
{
	if (UK2Node_CustomNode* Node = BoundNode.Get())
	{
		FCNABehaviorHost::Get().ReportProblem(Node, Details);
	}
}

bool UCNABehaviorBase::SetPinVisibility(FName PinName, bool bVisible)
{
	UK2Node_CustomNode* Node = BoundNode.Get();
	if (!Node)
	{
		return false;
	}
	FText Error;
	if (!Node->SetPinVisibilityFromBehavior(PinName, bVisible, Error))
	{
		ReportRefusedAction(FText::Format(LOCTEXT("Behavior.SetPinVisibilityFailed", "SetPinVisibility({0}): {1}"), FText::FromName(PinName), Error));
		return false;
	}
	return true;
}

bool UCNABehaviorBase::RenamePin(FName PinName, const FText& NewDisplayName)
{
	UK2Node_CustomNode* Node = BoundNode.Get();
	if (!Node)
	{
		return false;
	}
	FText Error;
	if (!Node->RenamePinFromBehavior(PinName, NewDisplayName, Error))
	{
		ReportRefusedAction(FText::Format(LOCTEXT("Behavior.RenamePinFailed", "RenamePin({0}): {1}"), FText::FromName(PinName), Error));
		return false;
	}
	return true;
}

bool UCNABehaviorBase::ChangePinDataType(FName PinName, const FCNAPinTypeDesc& NewType)
{
	UK2Node_CustomNode* Node = BoundNode.Get();
	if (!Node)
	{
		return false;
	}
	FText Error;
	if (!Node->ChangeWildcardTypeFromBehavior(PinName, NewType.ToPinType(), Error))
	{
		ReportRefusedAction(FText::Format(LOCTEXT("Behavior.ChangePinDataTypeFailed", "ChangePinDataType({0}): {1}"), FText::FromName(PinName), Error));
		return false;
	}
	return true;
}

void UCNABehaviorBase::SetNodeTitle(const FText& NewTitle)
{
	if (UK2Node_CustomNode* Node = BoundNode.Get())
	{
		Node->SetTitleOverrideFromBehavior(NewTitle);
	}
}

void UCNABehaviorBase::ThrowNodeError(const FString& Message)
{
	if (UK2Node_CustomNode* Node = BoundNode.Get())
	{
		Node->SetBehaviorMessage(EMessageSeverity::Error, Message);
	}
}

void UCNABehaviorBase::ThrowNodeWarning(const FString& Message)
{
	if (UK2Node_CustomNode* Node = BoundNode.Get())
	{
		Node->SetBehaviorMessage(EMessageSeverity::Warning, Message);
	}
}

bool UCNABehaviorBase::SetPinConnectionRule(FName PinName, ECNAConnectionRule Rule, TSubclassOf<UObject> ClassFilter)
{
	UK2Node_CustomNode* Node = BoundNode.Get();
	if (!Node)
	{
		return false;
	}
	FText Error;
	if (!Node->SetPinConnectionRuleFromBehavior(PinName, Rule, ClassFilter.Get(), Error))
	{
		ReportRefusedAction(FText::Format(LOCTEXT("Behavior.SetRuleFailed", "SetPinConnectionRule({0}): {1}"), FText::FromName(PinName), Error));
		return false;
	}
	return true;
}

// Queries

FString UCNABehaviorBase::GetPinValue(FName PinName) const
{
	const UK2Node_CustomNode* Node = BoundNode.Get();
	const UEdGraphPin* Pin = Node ? Node->FindPin(PinName) : nullptr;
	return FCNAPinUtils::GetValueAsString(Pin);
}
bool UCNABehaviorBase::IsPinConnected(FName PinName) const
{
	const UK2Node_CustomNode* Node = BoundNode.Get();
	return Node && FCNAPinUtils::IsPinOrSubPinLinked(Node->FindPin(PinName));
}
bool UCNABehaviorBase::IsPinVisible(FName PinName) const
{
	const UK2Node_CustomNode* Node = BoundNode.Get();
	const UEdGraphPin* Pin = Node ? Node->FindPin(PinName) : nullptr;
	return Pin && !Pin->bHidden;
}
bool UCNABehaviorBase::HasPin(FName PinName) const
{
	const UK2Node_CustomNode* Node = BoundNode.Get();
	return Node && Node->FindPin(PinName) != nullptr;
}
FCNAPinTypeDesc UCNABehaviorBase::GetPinType(FName PinName) const
{
	const UK2Node_CustomNode* Node = BoundNode.Get();
	const UEdGraphPin* Pin = Node ? Node->FindPin(PinName) : nullptr;
	return Pin ? FCNAPinTypeDesc::FromPinType(Pin->PinType) : FCNAPinTypeDesc();
}
int32 UCNABehaviorBase::GetDynamicPinCount() const
{
	const UK2Node_CustomNode* Node = BoundNode.Get();
	return Node ? Node->GetDynamicPinCount() : 0;
}
FName UCNABehaviorBase::GetDynamicPinName(int32 Index) const
{
	const UK2Node_CustomNode* Node = BoundNode.Get();
	return Node ? Node->GetDynamicPinName(Index) : NAME_None;
}
TArray<FName> UCNABehaviorBase::GetPinNames(bool bInputs, bool bOutputs) const
{
	TArray<FName> Names;
	if (const UK2Node_CustomNode* Node = BoundNode.Get())
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->ParentPin == nullptr
				&& ((bInputs && Pin->Direction == EGPD_Input) || (bOutputs && Pin->Direction == EGPD_Output)))
			{
				Names.Add(Pin->PinName);
			}
		}
	}
	return Names;
}
bool UCNABehaviorBase::IsPreview() const
{
	const UK2Node_CustomNode* Node = BoundNode.Get();
	return Node && Node->IsPreviewNode();
}
// Pin type helpers

namespace CNABehaviorLocal
{
	FCNAPinTypeDesc Make(FName Category, UObject* Object = nullptr)
	{
		FCNAPinTypeDesc Desc;
		Desc.Category = Category;
		Desc.SubCategoryObject = Object;
		return Desc;
	}
}
FCNAPinTypeDesc UCNABehaviorBase::MakeBooleanType()   { return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Boolean); }
FCNAPinTypeDesc UCNABehaviorBase::MakeIntegerType()   { return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Int); }
FCNAPinTypeDesc UCNABehaviorBase::MakeFloatType()     { return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Float); }
FCNAPinTypeDesc UCNABehaviorBase::MakeStringType()    { return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_String); }
FCNAPinTypeDesc UCNABehaviorBase::MakeNameType()      { return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Name); }
FCNAPinTypeDesc UCNABehaviorBase::MakeTextType()      { return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Text); }
FCNAPinTypeDesc UCNABehaviorBase::MakeVectorType()    { return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Struct, TBaseStructure<FVector>::Get()); }
FCNAPinTypeDesc UCNABehaviorBase::MakeRotatorType()   { return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Struct, TBaseStructure<FRotator>::Get()); }
FCNAPinTypeDesc UCNABehaviorBase::MakeTransformType() { return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Struct, TBaseStructure<FTransform>::Get()); }

FCNAPinTypeDesc UCNABehaviorBase::MakeObjectType(UClass* Class)
{
	return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Object, Class ? Class : UObject::StaticClass());
}
FCNAPinTypeDesc UCNABehaviorBase::MakeClassType(UClass* MetaClass)
{
	return CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Class, MetaClass ? MetaClass : UObject::StaticClass());
}
FCNAPinTypeDesc UCNABehaviorBase::MakeStructType(UScriptStruct* Struct)
{
	return Struct ? CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Struct, Struct) : FCNAPinTypeDesc();
}
FCNAPinTypeDesc UCNABehaviorBase::MakeEnumType(UEnum* Enum)
{
	return Enum ? CNABehaviorLocal::Make(UEdGraphSchema_K2::PC_Byte, Enum) : FCNAPinTypeDesc();
}
FCNAPinTypeDesc UCNABehaviorBase::MakeArrayOf(const FCNAPinTypeDesc& ElementType)
{
	FCNAPinTypeDesc Result = ElementType;
	Result.Container = ECNAPinContainer::Array;
	return Result;
}
bool UCNABehaviorBase::IsSamePinType(const FCNAPinTypeDesc& A, const FCNAPinTypeDesc& B)
{
	return FCNAPinUtils::AreTypesIdentical(A.ToPinType(), B.ToPinType());
}
#undef LOCTEXT_NAMESPACE
