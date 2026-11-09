// Copyright (c) NodeFactory. Wire colors of custom nodes and of the Editor Behavior Graph (spec 4.7, 6.5; V26).

#include "CNAConnectionDrawingPolicy.h"
#include "CNAGraphUtils.h"
#include "CNATypes.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "SGraphPin_NodeFactory.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "CNALocalization.h"

namespace CNAConnectionLocal
{
	/** Color of the pin of a custom node whose asset colors wires (spec 4.7, bColorWires). */
	bool GetPinWireColor(const UEdGraphPin* Pin, FLinearColor& OutColor)
	{
		if (!Pin || Pin->IsPendingKill() || Pin->bOrphanedPin)
		{
			return false;
		}
		const UK2Node_CustomNode* Node = Cast<UK2Node_CustomNode>(Pin->GetOwningNodeUnchecked());
		const UCustomNodeAsset* Asset = Node ? Node->GetCustomNodeAsset() : nullptr;
		return Asset && Asset->bColorWires && Node->GetPinColorOverride(Pin, OutColor);
	}

	/**
	 * Finds Alpha with Lerp(From, To, Alpha) == Value. The channel with the largest distance gives Alpha,
	 * all channels have to agree within a small tolerance.
	 */
	bool SolveLerp(const FLinearColor& From, const FLinearColor& To, const FLinearColor& Value, float& OutAlpha)
	{
		const float FromChannels[4] = { From.R, From.G, From.B, From.A };
		const float ToChannels[4] = { To.R, To.G, To.B, To.A };
		const float ValueChannels[4] = { Value.R, Value.G, Value.B, Value.A };

		int32 BestChannel = INDEX_NONE;
		float BestDistance = 0.0f;
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			const float Distance = FMath::Abs(ToChannels[Channel] - FromChannels[Channel]);
			if (Distance > BestDistance)
			{
				BestDistance = Distance;
				BestChannel = Channel;
			}
		}
		if (BestChannel == INDEX_NONE || BestDistance < KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const float Alpha = (ValueChannels[BestChannel] - FromChannels[BestChannel]) / (ToChannels[BestChannel] - FromChannels[BestChannel]);
		if (Alpha < -0.001f || Alpha > 1.001f)
		{
			return false;
		}
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			const float Expected = FromChannels[Channel] + (ToChannels[Channel] - FromChannels[Channel]) * Alpha;
			if (FMath::Abs(Expected - ValueChannels[Channel]) > 0.003f)
			{
				return false;
			}
		}
		OutAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return true;
	}
}

FCNAConnectionDrawingPolicy::FCNAConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj, bool bInBehaviorGraph)
	: FKismetConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj)
	, bBehaviorGraph(bInBehaviorGraph)
{
}

void FCNAConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FConnectionParams& Params)
{
	FKismetConnectionDrawingPolicy::DetermineWiringStyle(OutputPin, InputPin, Params);

	if (!OutputPin || !GraphObj)
	{
		return;
	}
	if (OutputPin->bOrphanedPin || (InputPin && InputPin->bOrphanedPin))
	{
		// Red wires of orphaned pins stay.
		return;
	}

	FLinearColor CustomColor;
	if (!GetCustomWireColor(OutputPin, InputPin, bBehaviorGraph, CustomColor))
	{
		return;
	}

	const UEdGraphSchema* Schema = GraphObj->GetSchema();
	if (!Schema)
	{
		return;
	}
	const FLinearColor TypeColor = Schema->GetPinTypeColor(OutputPin->PinType);
	Params.WireColor = RecolorWire(Params.WireColor, TypeColor, CustomColor);
}

bool FCNAConnectionDrawingPolicy::GetCustomWireColor(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, bool bInBehaviorGraph, FLinearColor& OutColor)
{
	if (bInBehaviorGraph && OutputPin && OutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		// Exec wires of the Behavior Graph (spec 6.5, A23).
		OutColor = CNATypes::GetAccentColor();
		return true;
	}

	// The output pin first, then the input pin of a custom node (spec 4.7).
	if (CNAConnectionLocal::GetPinWireColor(OutputPin, OutColor))
	{
		return true;
	}
	if (CNAConnectionLocal::GetPinWireColor(InputPin, OutColor))
	{
		return true;
	}

	// Pins drawn as pose pins get the white wire of AnimGraph pose links (exec wires are white already).
	if (FCNAPinStyle::GetPinShape(OutputPin) == ECNAPinShape::Pose || FCNAPinStyle::GetPinShape(InputPin) == ECNAPinShape::Pose)
	{
		OutColor = FLinearColor::White;
		return true;
	}
	return false;
}

FLinearColor FCNAConnectionDrawingPolicy::RecolorWire(const FLinearColor& EngineColor, const FLinearColor& TypeColor, const FLinearColor& CustomColor)
{
	if (EngineColor.Equals(TypeColor, 0.001f))
	{
		return CustomColor;
	}

	// FConnectionDrawingPolicy::ApplyHoverDeemphasis lerps the type color towards translucent black
	// (other wires while a pin is hovered) or towards white (the hovered wire). The same lerp is
	// applied to the custom color (V-check: the targets are the constants of 4.20.3).
	const FLinearColor Targets[2] = { FLinearColor(0.0f, 0.0f, 0.0f, 0.5f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f) };
	for (const FLinearColor& Target : Targets)
	{
		float Alpha = 0.0f;
		if (CNAConnectionLocal::SolveLerp(TypeColor, Target, EngineColor, Alpha))
		{
			return FMath::Lerp(CustomColor, Target, Alpha);
		}
	}

	// Colors of the debugger and anything unknown win.
	return EngineColor;
}

// FCNAPinConnectionFactory

FConnectionDrawingPolicy* FCNAPinConnectionFactory::CreateConnectionPolicy(const UEdGraphSchema* Schema, int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	// Only the plain Blueprint schema: derived schemas (Animation Graph and others) have policies of their own.
	if (!Schema || !InGraphObj || Schema->GetClass() != UEdGraphSchema_K2::StaticClass())
	{
		return nullptr;
	}

	const bool bBehaviorGraph = FCNAGraphUtils::IsBehaviorGraph(InGraphObj);
	if (!bBehaviorGraph && !FCNAGraphUtils::ContainsCustomNodes(InGraphObj))
	{
		return nullptr;
	}

	// SGraphPanel deletes the policy after painting.
	return new FCNAConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj, bBehaviorGraph);
}
