// Copyright (c) NodeFactory. Editor helpers of the Curve body widget: pins of the node, value types, curve data.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "CNATypes.h"

class UClass;
class UCurveBase;
class UCustomNodeAsset;
struct FCNASignaturePin;

/**
 * Shared by UK2Node_CustomNode (curve pins of the instance), UK2Node_CNACurveValue, the validator and the expander.
 * A Curve widget is referenced by its WidgetGuid; its pins have the stable names of CNATypes::MakeCurvePinName.
 */
struct NODEFACTORYEDITOR_API FCNACurveUtils
{
	/** Float, Vector or LinearColor pin type of the value of a curve. */
	static FEdGraphPinType GetValuePinType(ECNACurveValueType ValueType);

	/** UCurveFloat, UCurveVector or UCurveLinearColor. */
	static UClass* GetCurveAssetClass(ECNACurveValueType ValueType);

	/** GetFloatValue, GetVectorValue or GetLinearColorValue of the curve asset class. */
	static FName GetCurveAssetGetterName(ECNACurveValueType ValueType);

	/** The loaded Curve asset when bUseCurveAsset is set and its class matches ValueType; nullptr otherwise. */
	static UCurveBase* LoadCurveAsset(const FCNACurveSettings& Curve);

	static bool IsCurveAssetCompatible(const UCurveBase* CurveAsset, ECNACurveValueType ValueType);

	/** No key in the channels the value type uses. */
	static bool IsOwnCurveEmpty(const FCNACurveSettings& Curve);

	/** Time range of the own curve or of the Curve asset; false without keys. */
	static bool GetTimeRange(const FCNACurveSettings& Curve, float& OutMin, float& OutMax);

	/** Playback length: Length, or the time of the last key when Length is 0; never 0. */
	static float GetPlaybackLength(const FCNACurveSettings& Curve);

	/** NumSamples values of an own channel at equal steps from MinTime to MaxTime (FRichCurve::Eval). */
	static void BakeChannel(const FCNACurveSettings& Curve, int32 Channel, float MinTime, float MaxTime, int32 NumSamples, TArray<float>& OutSamples);

	/** Value of an own channel at one time, limited to the keyed range like the baked samples. */
	static float EvaluateChannel(const FCNACurveSettings& Curve, int32 Channel, float Time);

	// Pins of the node instance

	/** Roles whose pins the instance gets, in pin order. A pure node gets no exec pins. */
	static void GetPinRoles(const FCNACurveSettings& Curve, bool bPureNode, TArray<ECNACurvePinRole>& OutRoles);

	static bool IsExecRole(ECNACurvePinRole Role);
	static EEdGraphPinDirection GetRoleDirection(ECNACurvePinRole Role);

	/** First curve in Playback mode of an impure asset; nullptr for a pure asset or when there is none. */
	static const FCNABodyWidget* FindPlaybackCurve(const UCustomNodeAsset* Asset);

	/** First exec pin of the signature in a direction: the exec flow a Playback curve drives. NAME_None without one. */
	static FName GetPrimaryExecPinName(const TArray<FCNASignaturePin>& Signature, EEdGraphPinDirection Direction);

	// Texts

	/** Name of the curve for titles and messages; "<unnamed curve>" without a name. */
	static FText GetCurveDisplayName(const FCNABodyWidget& Widget);

	/** Text of a pin name for messages: the label of a curve pin, the pin name otherwise. */
	static FText DescribePinName(const UCustomNodeAsset* Asset, FName PinName);

	/** Text of the curve settings that define the pins of the instance; empty without curves (signature hash). */
	static FString GetPinSignatureText(const UCustomNodeAsset* Asset);

	/** Rebuilds the Curve Value nodes of the asset whose curve changed its value type or name. */
	static void SyncCurveValueNodes(UCustomNodeAsset* Asset);
};
