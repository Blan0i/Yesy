// Copyright (c) NodeFactory. Wire colors of custom nodes and of the Editor Behavior Graph (spec 4.7, 6.5; V26).

#pragma once

#include "CoreMinimal.h"
#include "BlueprintConnectionDrawingPolicy.h"
#include "EdGraphUtilities.h"

class UEdGraph;
class UEdGraphPin;
class UEdGraphSchema;

/**
 * Kismet wire drawing with two additions (V26):
 * - a wire takes the PinColor of the output pin when that pin belongs to a custom node with bColorWires;
 *   otherwise the PinColor of the input pin of a custom node; otherwise the color of the type (spec 4.7);
 * - exec wires of the Editor Behavior Graph are turquoise (spec 6.5).
 * Colors of the debugger (execution roadmap) and of orphaned pins keep priority; hover emphasis is applied
 * to the custom color the same way the engine applies it to the type color.
 */
class NODEFACTORYEDITOR_API FCNAConnectionDrawingPolicy : public FKismetConnectionDrawingPolicy
{
public:
	FCNAConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj, bool bInBehaviorGraph);

	// FConnectionDrawingPolicy
	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;

	/** Custom color of a wire, false for the engine color. Public for the tests. */
	static bool GetCustomWireColor(const UEdGraphPin* OutputPin, const UEdGraphPin* InputPin, bool bBehaviorGraph, FLinearColor& OutColor);

	/**
	 * Replaces TypeColor inside the engine wire color by CustomColor. Plain and hover-lerped colors are recognized;
	 * any other color (debugging) is returned unchanged.
	 */
	static FLinearColor RecolorWire(const FLinearColor& EngineColor, const FLinearColor& TypeColor, const FLinearColor& CustomColor);

private:
	/** The graph is the Event Graph of a Custom Node Asset. */
	bool bBehaviorGraph;
};

/**
 * Gives FCNAConnectionDrawingPolicy to graphs of the plain K2 schema that contain custom nodes and to the
 * Behavior Graph. All other graphs (and other schemas: animation, materials) get nullptr, so the engine keeps
 * its own policy for them (spec A23).
 */
class NODEFACTORYEDITOR_API FCNAPinConnectionFactory : public FGraphPanelPinConnectionFactory
{
public:
	virtual FConnectionDrawingPolicy* CreateConnectionPolicy(const UEdGraphSchema* Schema, int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const override;
};
