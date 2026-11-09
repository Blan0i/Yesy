// Copyright (c) NodeFactory. Data types and enumerations of a Custom Node Asset (spec 3, 4.2, 4.7, 4.8).

#include "CNATypes.h"
#include "Misc/Guid.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace
{
	/** Highest number of element pins a dynamic group can have, independent of the asset settings. */
	const int32 CNAAbsoluteDynamicLimit = 64;

	// Category names of the K2 schema. The runtime module cannot include EdGraphSchema_K2.h (BlueprintGraph is an
	// editor module), so the few names needed here are repeated. They match UEdGraphSchema_K2::PC_* of 4.20.
	const FName CNAPinCategoryExec(TEXT("exec"));
	const FName CNAPinCategoryWildcard(TEXT("wildcard"));
	const FName CNAPinCategoryDelegate(TEXT("delegate"));
	const FName CNAPinCategoryMCDelegate(TEXT("mcdelegate"));
	const FName CNAPinCategoryInt(TEXT("int"));
}

// FCNAPinOverride

void FCNAPinOverride::GetDynamicRange(int32& OutMin, int32& OutMax) const
{
	OutMin = FMath::Clamp(DynamicMin, 0, CNAAbsoluteDynamicLimit);
	OutMax = FMath::Clamp(DynamicMax, 1, CNAAbsoluteDynamicLimit);
	if (OutMax < OutMin)
	{
		Swap(OutMin, OutMax);
	}
	OutMax = FMath::Max(OutMax, 1);
}

int32 FCNAPinOverride::ClampDynamicCount(int32 Count) const
{
	int32 Min = 0;
	int32 Max = 1;
	GetDynamicRange(Min, Max);
	return FMath::Clamp(Count, Min, Max);
}

FText FCNAPinOverride::FormatDynamicName(int32 Index) const
{
	if (!DynamicNameFormat.IsEmpty())
	{
		// {0}: zero based index, {1}: one based index - "Item {1}" gives Item 1, Item 2, ...
		return FText::Format(DynamicNameFormat, FText::AsNumber(Index), FText::AsNumber(Index + 1));
	}
	const FText GroupName = DisplayName.IsEmpty() ? FText::FromName(PinName) : DisplayName;
	return FText::Format(LOCTEXT("DynamicPinDefaultName", "{0} {1}"), GroupName, FText::AsNumber(Index));
}

bool FCNAPinOverride::IsEmpty() const
{
	return DisplayName.IsEmpty()
		&& Tooltip.IsEmpty()
		&& !bAdvanced
		&& !bHiddenByDefault
		&& !bRequired
		&& !bDefaultToSelf
		&& PassThroughOf.IsNone()
		&& !bDynamicGroup
		&& ConnectionRule == ECNAConnectionRule::None
		&& !bOverridePinColor
		&& !bOverrideLabelColor;
}

// FCNALocalVariable

FCNALocalVariable::FCNALocalVariable()
	: Guid(FGuid::NewGuid())
{
	Type.PinCategory = CNAPinCategoryInt;
}

FCNALocalVariable::FCNALocalVariable(FName InName, const FEdGraphPinType& InType)
	: Guid(FGuid::NewGuid())
	, Name(InName)
	, Type(InType)
{
}

bool FCNALocalVariable::IsTypeAllowed(const FEdGraphPinType& InType)
{
	return !InType.PinCategory.IsNone()
		&& InType.PinCategory != CNAPinCategoryExec
		&& InType.PinCategory != CNAPinCategoryWildcard
		&& InType.PinCategory != CNAPinCategoryDelegate
		&& InType.PinCategory != CNAPinCategoryMCDelegate;
}

// CNATypes

namespace CNATypes
{
	const TCHAR* DynamicPinSeparator = TEXT("__");

	FName MakeDynamicPinName(FName BaseName, int32 Index)
	{
		return FName(*FString::Printf(TEXT("%s%s%d"), *BaseName.ToString(), DynamicPinSeparator, Index));
	}

	bool ParseDynamicPinName(FName PinName, FName& OutBaseName, int32& OutIndex)
	{
		const FString Name = PinName.ToString();
		const int32 SeparatorIndex = Name.Find(DynamicPinSeparator, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (SeparatorIndex <= 0)
		{
			return false;
		}
		const FString Base = Name.Left(SeparatorIndex);
		const FString Suffix = Name.Mid(SeparatorIndex + FCString::Strlen(DynamicPinSeparator));
		if (Suffix.IsEmpty() || Suffix.Len() > 3)
		{
			return false;
		}
		for (const TCHAR Character : Suffix)
		{
			if (!FChar::IsDigit(Character))
			{
				return false;
			}
		}
		OutBaseName = FName(*Base);
		OutIndex = FCString::Atoi(*Suffix);
		return true;
	}

	int32 GetGraphKindFlag(ECNAGraphKind Kind)
	{
		return 1 << static_cast<int32>(Kind);
	}

	int32 AllGraphKindsMask()
	{
		return GetGraphKindFlag(ECNAGraphKind::Function)
			| GetGraphKindFlag(ECNAGraphKind::EventGraph)
			| GetGraphKindFlag(ECNAGraphKind::Macro)
			| GetGraphKindFlag(ECNAGraphKind::ConstructionScript);
	}

	bool IsGraphKindAllowed(int32 Mask, ECNAGraphKind Kind)
	{
		return (Mask & GetGraphKindFlag(Kind)) != 0;
	}

	FName GetCornerIconBrushName(ECNACornerIcon Icon)
	{
		// Brush names of the EditorStyle of 4.20 used by K2Node_CallFunction and the event nodes (V20).
		switch (Icon)
		{
		case ECNACornerIcon::Latent:    return FName(TEXT("Graph.Latent.LatentIcon"));
		case ECNACornerIcon::Server:    return FName(TEXT("Graph.Replication.AuthorityOnly"));
		case ECNACornerIcon::Client:    return FName(TEXT("Graph.Replication.ClientEvent"));
		case ECNACornerIcon::Multicast: return FName(TEXT("Graph.Replication.Replicated"));
		case ECNACornerIcon::Message:   return FName(TEXT("Graph.Message.MessageIcon"));
		case ECNACornerIcon::None:
		default:                        return NAME_None;
		}
	}

	FText GetCornerIconDisplayName(ECNACornerIcon Icon)
	{
		switch (Icon)
		{
		case ECNACornerIcon::Latent:    return LOCTEXT("CornerIcon.Latent", "Latent");
		case ECNACornerIcon::Server:    return LOCTEXT("CornerIcon.Server", "Server");
		case ECNACornerIcon::Client:    return LOCTEXT("CornerIcon.Client", "Client");
		case ECNACornerIcon::Multicast: return LOCTEXT("CornerIcon.Multicast", "Multicast");
		case ECNACornerIcon::Message:   return LOCTEXT("CornerIcon.Message", "Message");
		case ECNACornerIcon::None:
		default:                        return LOCTEXT("CornerIcon.None", "None");
		}
	}

	FText GetGraphKindDisplayName(ECNAGraphKind Kind)
	{
		switch (Kind)
		{
		case ECNAGraphKind::Function:           return LOCTEXT("GraphKind.Function", "Function");
		case ECNAGraphKind::EventGraph:         return LOCTEXT("GraphKind.EventGraph", "Event Graph");
		case ECNAGraphKind::Macro:              return LOCTEXT("GraphKind.Macro", "Macro");
		case ECNAGraphKind::ConstructionScript: return LOCTEXT("GraphKind.ConstructionScript", "Construction Script");
		default:                                return FText::GetEmpty();
		}
	}

	FText DescribeGraphKinds(int32 Mask)
	{
		// FText::Join is newer than 4.20: the names are joined as strings.
		TArray<FString> Parts;
		const ECNAGraphKind Kinds[] = { ECNAGraphKind::Function, ECNAGraphKind::EventGraph, ECNAGraphKind::Macro, ECNAGraphKind::ConstructionScript };
		for (ECNAGraphKind Kind : Kinds)
		{
			if (IsGraphKindAllowed(Mask, Kind))
			{
				Parts.Add(GetGraphKindDisplayName(Kind).ToString());
			}
		}
		if (Parts.Num() == 0)
		{
			return LOCTEXT("GraphKind.NoneAllowed", "No graph");
		}
		return FText::FromString(FString::Join(Parts, TEXT(", ")));
	}

	FLinearColor GetAccentColor()
	{
		return FLinearColor(FColor(0, 184, 169));
	}
}

#undef LOCTEXT_NAMESPACE
