// Copyright (c) NodeFactory. Data types and enumerations of a Custom Node Asset (spec 3, 4.2, 4.7, 4.8).

#include "CNATypes.h"
#include "Misc/Guid.h"
#include "CNALocalization.h"

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

	static const TCHAR* GetCurvePinRoleToken(ECNACurvePinRole Role)
	{
		switch (Role)
		{
		case ECNACurvePinRole::Time:          return TEXT("Time");
		case ECNACurvePinRole::Play:          return TEXT("Play");
		case ECNACurvePinRole::PlayFromStart: return TEXT("PlayFromStart");
		case ECNACurvePinRole::Stop:          return TEXT("Stop");
		case ECNACurvePinRole::Reverse:       return TEXT("Reverse");
		case ECNACurvePinRole::Update:        return TEXT("Update");
		case ECNACurvePinRole::Finished:      return TEXT("Finished");
		default:                              return TEXT("Unknown");
		}
	}

	FName MakeCurvePinName(const FGuid& WidgetGuid, ECNACurvePinRole Role)
	{
		return FName(*FString::Printf(TEXT("CNACurve_%s_%s"), *WidgetGuid.ToString(EGuidFormats::Digits), GetCurvePinRoleToken(Role)));
	}

	bool ParseCurvePinName(FName PinName, FGuid& OutWidgetGuid, ECNACurvePinRole& OutRole)
	{
		const FString Name = PinName.ToString();
		const FString Prefix(TEXT("CNACurve_"));
		if (!Name.StartsWith(Prefix, ESearchCase::CaseSensitive))
		{
			return false;
		}
		const FString Rest = Name.Mid(Prefix.Len());
		FString GuidPart;
		FString RolePart;
		if (!Rest.Split(TEXT("_"), &GuidPart, &RolePart, ESearchCase::CaseSensitive, ESearchDir::FromStart))
		{
			return false;
		}
		if (!FGuid::ParseExact(GuidPart, EGuidFormats::Digits, OutWidgetGuid))
		{
			return false;
		}
		for (uint8 Index = 0; Index <= static_cast<uint8>(ECNACurvePinRole::Finished); ++Index)
		{
			const ECNACurvePinRole Role = static_cast<ECNACurvePinRole>(Index);
			if (RolePart == GetCurvePinRoleToken(Role))
			{
				OutRole = Role;
				return true;
			}
		}
		return false;
	}

	FText GetCurvePinRoleDefaultName(ECNACurvePinRole Role)
	{
		switch (Role)
		{
		case ECNACurvePinRole::Time:          return LOCTEXT("CurvePin.Time", "Time");
		case ECNACurvePinRole::Play:          return LOCTEXT("CurvePin.Play", "Play");
		case ECNACurvePinRole::PlayFromStart: return LOCTEXT("CurvePin.PlayFromStart", "Play from Start");
		case ECNACurvePinRole::Stop:          return LOCTEXT("CurvePin.Stop", "Stop");
		case ECNACurvePinRole::Reverse:       return LOCTEXT("CurvePin.Reverse", "Reverse");
		case ECNACurvePinRole::Update:        return LOCTEXT("CurvePin.Update", "Update");
		case ECNACurvePinRole::Finished:      return LOCTEXT("CurvePin.Finished", "Finished");
		default:                              return FText::GetEmpty();
		}
	}

	FLinearColor GetAnimStyleTitleColor(ECNAAnimNodeStyle Style)
	{
		// Values of GetNodeTitleColor in the AnimGraph module of 4.20.3; result-like nodes use the editor result color.
		switch (Style)
		{
		case ECNAAnimNodeStyle::AssetPlayer:     return FLinearColor(0.10f, 0.60f, 0.12f);
		case ECNAAnimNodeStyle::Blend:           return FLinearColor(0.2f, 0.8f, 0.2f);
		case ECNAAnimNodeStyle::BlendSimple:     return FLinearColor(0.75f, 0.75f, 0.75f);
		case ECNAAnimNodeStyle::SkeletalControl: return FLinearColor(0.75f, 0.75f, 0.10f);
		case ECNAAnimNodeStyle::SpaceConversion: return FLinearColor(0.7f, 0.7f, 0.7f);
		case ECNAAnimNodeStyle::RefPose:         return FLinearColor(FColor(200, 100, 100));
		case ECNAAnimNodeStyle::StateMachine:    return FLinearColor(0.8f, 0.8f, 0.8f);
		case ECNAAnimNodeStyle::Result:          return FLinearColor(1.0f, 0.65f, 0.4f);
		case ECNAAnimNodeStyle::Transition:      return FLinearColor(FColor::Red);
		case ECNAAnimNodeStyle::State:
		case ECNAAnimNodeStyle::Conduit:
		case ECNAAnimNodeStyle::Entry:           return FLinearColor(0.08f, 0.08f, 0.08f);
		case ECNAAnimNodeStyle::AnimNode:
		default:                                 return FLinearColor::Black;
		}
	}
}

// FCNACurveSettings

FText FCNACurveSettings::GetPinLabel(ECNACurvePinRole Role) const
{
	const FText* Label = nullptr;
	switch (Role)
	{
	case ECNACurvePinRole::Time:          Label = &TimeLabel; break;
	case ECNACurvePinRole::Play:          Label = &PlayLabel; break;
	case ECNACurvePinRole::PlayFromStart: Label = &PlayFromStartLabel; break;
	case ECNACurvePinRole::Stop:          Label = &StopLabel; break;
	case ECNACurvePinRole::Reverse:       Label = &ReverseLabel; break;
	case ECNACurvePinRole::Update:        Label = &UpdateLabel; break;
	case ECNACurvePinRole::Finished:      Label = &FinishedLabel; break;
	default: break;
	}
	return (Label && !Label->IsEmptyOrWhitespace()) ? *Label : CNATypes::GetCurvePinRoleDefaultName(Role);
}

bool FCNACurveSettings::HasPinRole(ECNACurvePinRole Role) const
{
	if (Mode == ECNACurveMode::EvaluateAtTime)
	{
		return Role == ECNACurvePinRole::Time;
	}
	switch (Role)
	{
	case ECNACurvePinRole::Time:          return false;
	case ECNACurvePinRole::PlayFromStart: return bShowPlayFromStart;
	case ECNACurvePinRole::Reverse:       return bShowReverse;
	default:                              return true;
	}
}

int32 FCNACurveSettings::GetChannelCount() const
{
	switch (ValueType)
	{
	case ECNACurveValueType::Vector:      return 3;
	case ECNACurveValueType::LinearColor: return 4;
	case ECNACurveValueType::Float:
	default:                              return 1;
	}
}

const FRuntimeFloatCurve* FCNACurveSettings::GetChannel(int32 Index) const
{
	switch (Index)
	{
	case 0: return &CurveX;
	case 1: return &CurveY;
	case 2: return &CurveZ;
	case 3: return &CurveW;
	default: return nullptr;
	}
}

bool FCNACurveSettings::GetOwnTimeRange(float& OutMin, float& OutMax) const
{
	bool bAny = false;
	OutMin = 0.0f;
	OutMax = 0.0f;
	for (int32 Index = 0; Index < GetChannelCount(); ++Index)
	{
		const FRichCurve& Rich = GetChannel(Index)->EditorCurveData;
		if (Rich.GetNumKeys() == 0)
		{
			continue;
		}
		float Min = 0.0f;
		float Max = 0.0f;
		Rich.GetTimeRange(Min, Max);
		OutMin = bAny ? FMath::Min(OutMin, Min) : Min;
		OutMax = bAny ? FMath::Max(OutMax, Max) : Max;
		bAny = true;
	}
	return bAny;
}

#undef LOCTEXT_NAMESPACE
