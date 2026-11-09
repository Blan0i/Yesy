// Copyright (c) NodeFactory. Editor helpers of the Curve body widget: pins of the node, value types, curve data.

#include "CNACurveUtils.h"
#include "CNAGraphUtils.h"
#include "CNAPinUtils.h"
#include "CustomNodeAsset.h"
#include "K2Node_CNACurveValue.h"

#include "Curves/CurveBase.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Internationalization/Text.h"
#include "UObject/Class.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNACurveUtilsLocal
{
	FString GetSourceString(const FText& Text)
	{
		const FString* Source = FTextInspector::GetSourceString(Text);
		return Source ? *Source : Text.ToString();
	}

	const ECNACurvePinRole AllRoles[] =
	{
		ECNACurvePinRole::Play,
		ECNACurvePinRole::PlayFromStart,
		ECNACurvePinRole::Stop,
		ECNACurvePinRole::Reverse,
		ECNACurvePinRole::Time,
		ECNACurvePinRole::Update,
		ECNACurvePinRole::Finished
	};
}

FEdGraphPinType FCNACurveUtils::GetValuePinType(ECNACurveValueType ValueType)
{
	FEdGraphPinType Type;
	switch (ValueType)
	{
	case ECNACurveValueType::Vector:
		Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
		Type.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		break;
	case ECNACurveValueType::LinearColor:
		Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
		Type.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
		break;
	case ECNACurveValueType::Float:
	default:
		Type.PinCategory = UEdGraphSchema_K2::PC_Float;
		break;
	}
	return Type;
}

UClass* FCNACurveUtils::GetCurveAssetClass(ECNACurveValueType ValueType)
{
	switch (ValueType)
	{
	case ECNACurveValueType::Vector:      return UCurveVector::StaticClass();
	case ECNACurveValueType::LinearColor: return UCurveLinearColor::StaticClass();
	case ECNACurveValueType::Float:
	default:                              return UCurveFloat::StaticClass();
	}
}

FName FCNACurveUtils::GetCurveAssetGetterName(ECNACurveValueType ValueType)
{
	// V-check: BlueprintCallable const members of 4.20.3 (pure in Blueprints): UCurveFloat::GetFloatValue,
	// UCurveVector::GetVectorValue, UCurveLinearColor::GetLinearColorValue, parameter InTime.
	switch (ValueType)
	{
	case ECNACurveValueType::Vector:      return FName(TEXT("GetVectorValue"));
	case ECNACurveValueType::LinearColor: return FName(TEXT("GetLinearColorValue"));
	case ECNACurveValueType::Float:
	default:                              return FName(TEXT("GetFloatValue"));
	}
}

bool FCNACurveUtils::IsCurveAssetCompatible(const UCurveBase* CurveAsset, ECNACurveValueType ValueType)
{
	return CurveAsset && CurveAsset->IsA(GetCurveAssetClass(ValueType));
}

UCurveBase* FCNACurveUtils::LoadCurveAsset(const FCNACurveSettings& Curve)
{
	if (!Curve.bUseCurveAsset || Curve.CurveAsset.IsNull())
	{
		return nullptr;
	}
	UCurveBase* Loaded = Curve.CurveAsset.LoadSynchronous();
	return IsCurveAssetCompatible(Loaded, Curve.ValueType) ? Loaded : nullptr;
}

bool FCNACurveUtils::IsOwnCurveEmpty(const FCNACurveSettings& Curve)
{
	float Min = 0.0f;
	float Max = 0.0f;
	return !Curve.GetOwnTimeRange(Min, Max);
}

bool FCNACurveUtils::GetTimeRange(const FCNACurveSettings& Curve, float& OutMin, float& OutMax)
{
	OutMin = 0.0f;
	OutMax = 0.0f;
	if (!Curve.bUseCurveAsset)
	{
		return Curve.GetOwnTimeRange(OutMin, OutMax);
	}
	if (const UCurveBase* CurveAsset = LoadCurveAsset(Curve))
	{
		// V-check: UCurveBase::GetTimeRange(float&, float&) const of 4.20.3 (range over all its curves).
		CurveAsset->GetTimeRange(OutMin, OutMax);
		return true;
	}
	return false;
}

float FCNACurveUtils::GetPlaybackLength(const FCNACurveSettings& Curve)
{
	// A length of 0 would make the loop divide by zero; the shortest playback ends on its first tick.
	const float MinimumLength = 0.0001f;
	if (Curve.Length > 0.0f)
	{
		return FMath::Max(Curve.Length, MinimumLength);
	}
	float Min = 0.0f;
	float Max = 0.0f;
	GetTimeRange(Curve, Min, Max);
	return FMath::Max(Max, MinimumLength);
}

float FCNACurveUtils::EvaluateChannel(const FCNACurveSettings& Curve, int32 Channel, float Time)
{
	const FRuntimeFloatCurve* RuntimeCurve = Curve.GetChannel(Channel);
	if (!RuntimeCurve)
	{
		return 0.0f;
	}
	float Min = 0.0f;
	float Max = 0.0f;
	if (Curve.GetOwnTimeRange(Min, Max))
	{
		Time = FMath::Clamp(Time, Min, Max);
	}
	// The own curve is the EditorCurveData of the struct, as drawn by the widget and read by GetOwnTimeRange.
	return RuntimeCurve->EditorCurveData.Eval(Time, 0.0f);
}

void FCNACurveUtils::BakeChannel(const FCNACurveSettings& Curve, int32 Channel, float MinTime, float MaxTime, int32 NumSamples, TArray<float>& OutSamples)
{
	OutSamples.Reset();
	NumSamples = FMath::Max(NumSamples, 2);
	OutSamples.Reserve(NumSamples);
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(NumSamples - 1);
		OutSamples.Add(EvaluateChannel(Curve, Channel, FMath::Lerp(MinTime, MaxTime, Alpha)));
	}
}

void FCNACurveUtils::GetPinRoles(const FCNACurveSettings& Curve, bool bPureNode, TArray<ECNACurvePinRole>& OutRoles)
{
	OutRoles.Reset();
	for (ECNACurvePinRole Role : CNACurveUtilsLocal::AllRoles)
	{
		if (Curve.HasPinRole(Role) && !(bPureNode && IsExecRole(Role)))
		{
			OutRoles.Add(Role);
		}
	}
}

bool FCNACurveUtils::IsExecRole(ECNACurvePinRole Role)
{
	return Role != ECNACurvePinRole::Time;
}

EEdGraphPinDirection FCNACurveUtils::GetRoleDirection(ECNACurvePinRole Role)
{
	return (Role == ECNACurvePinRole::Update || Role == ECNACurvePinRole::Finished) ? EGPD_Output : EGPD_Input;
}

const FCNABodyWidget* FCNACurveUtils::FindPlaybackCurve(const UCustomNodeAsset* Asset)
{
	if (!Asset || Asset->bPure)
	{
		return nullptr;
	}
	for (const FCNABodyWidget& Widget : Asset->BodyWidgets)
	{
		if (Widget.IsCurve() && Widget.WidgetGuid.IsValid() && Widget.Curve.Mode == ECNACurveMode::Playback)
		{
			return &Widget;
		}
	}
	return nullptr;
}

FName FCNACurveUtils::GetPrimaryExecPinName(const TArray<FCNASignaturePin>& Signature, EEdGraphPinDirection Direction)
{
	for (const FCNASignaturePin& Pin : Signature)
	{
		if (Pin.Direction == Direction && Pin.IsExec())
		{
			return Pin.Name;
		}
	}
	return NAME_None;
}

FText FCNACurveUtils::GetCurveDisplayName(const FCNABodyWidget& Widget)
{
	return Widget.PinName.IsNone() ? LOCTEXT("Curve.Unnamed", "<unnamed curve>") : FText::FromName(Widget.PinName);
}

FText FCNACurveUtils::DescribePinName(const UCustomNodeAsset* Asset, FName PinName)
{
	FGuid WidgetGuid;
	ECNACurvePinRole Role = ECNACurvePinRole::Time;
	if (!CNATypes::ParseCurvePinName(PinName, WidgetGuid, Role))
	{
		return FText::FromName(PinName);
	}
	const FCNABodyWidget* Widget = Asset ? Asset->FindCurveWidget(WidgetGuid) : nullptr;
	if (!Widget)
	{
		return CNATypes::GetCurvePinRoleDefaultName(Role);
	}
	return FText::Format(LOCTEXT("Curve.PinForMessages", "{0} ({1})"), Widget->Curve.GetPinLabel(Role), GetCurveDisplayName(*Widget));
}

FString FCNACurveUtils::GetPinSignatureText(const UCustomNodeAsset* Asset)
{
	FString Text;
	if (!Asset)
	{
		return Text;
	}
	for (const FCNABodyWidget& Widget : Asset->BodyWidgets)
	{
		if (!Widget.IsCurve())
		{
			continue;
		}
		// Only what the instance stores on its pins: names (Guid + role), kinds and labels.
		const FCNACurveSettings& Curve = Widget.Curve;
		Text += FString::Printf(TEXT("Curve:%s:%d:%d%d;"), *Widget.WidgetGuid.ToString(EGuidFormats::Digits),
			static_cast<int32>(Curve.Mode), Curve.bShowPlayFromStart ? 1 : 0, Curve.bShowReverse ? 1 : 0);
		const FText* Labels[] = { &Curve.TimeLabel, &Curve.PlayLabel, &Curve.PlayFromStartLabel, &Curve.StopLabel, &Curve.ReverseLabel, &Curve.UpdateLabel, &Curve.FinishedLabel };
		for (const FText* Label : Labels)
		{
			Text += CNACurveUtilsLocal::GetSourceString(*Label) + TEXT("|");
		}
		Text += TEXT(";");
	}
	return Text;
}

void FCNACurveUtils::SyncCurveValueNodes(UCustomNodeAsset* Asset)
{
	if (!Asset)
	{
		return;
	}
	TArray<UK2Node_CNACurveValue*> Nodes;
	FCNAGraphUtils::GetExecuteNodesOfClass<UK2Node_CNACurveValue>(Asset, Nodes);
	for (UK2Node_CNACurveValue* Node : Nodes)
	{
		if (Node && Node->NeedsRefresh())
		{
			Node->Modify();
			Node->ReconstructNode();
			if (UEdGraph* Graph = Node->GetGraph())
			{
				Graph->NotifyGraphChanged();
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
