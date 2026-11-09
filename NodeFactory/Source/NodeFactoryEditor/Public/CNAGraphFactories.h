// Copyright (c) NodeFactory. Registration of the node, pin and wire factories of the graph panels (V24-V26).

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"

class SGraphNode;
class SGraphPin;
class UEdGraphNode;
class UEdGraphPin;

/**
 * Node widgets of the plugin:
 * - UK2Node_CustomNode in every graph -> SGraphNode_NodeFactory (V24);
 * - Get Local and Set Local -> SGraphNodeCNALocalVariable (spec 4.8);
 * - events and actions of UCNABehaviorBase in the Behavior Graph -> SGraphNodeCNABehavior (spec 6.5).
 * Any other node gets nullptr and the engine widget.
 */
class NODEFACTORYEDITOR_API FCNAGraphNodeFactory : public FGraphPanelNodeFactory
{
public:
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override;

	/** The node is an event or an action declared by UCNABehaviorBase (or a native subclass) inside a Behavior Graph. */
	static bool IsBehaviorNode(const UEdGraphNode* Node);
};

/** Pin widgets of custom nodes (FCNAPinStyle::CreatePinWidget); pins of other nodes get nullptr. */
class NODEFACTORYEDITOR_API FCNAGraphPinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override;
};

/** Registers and unregisters the three factories; both calls are safe to repeat (spec 9.5, A15). */
struct NODEFACTORYEDITOR_API FCNAGraphFactories
{
	static void Register();
	static void Unregister();
	static bool IsRegistered();
};
