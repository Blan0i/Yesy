// Copyright (c) NodeFactory. Registration of the node, pin and wire factories of the graph panels (V24-V26).

#include "CNAGraphFactories.h"
#include "CNABehaviorBase.h"
#include "CNAConnectionDrawingPolicy.h"
#include "CNAGraphUtils.h"
#include "K2Node_CustomNode.h"
#include "K2Node_GetLocal.h"
#include "SGraphNode_NodeFactory.h"
#include "SGraphPin_NodeFactory.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "SGraphNode.h"
#include "SGraphPin.h"
#include "UObject/Class.h"

namespace CNAGraphFactoriesLocal
{
	TSharedPtr<FCNAGraphNodeFactory> NodeFactory;
	TSharedPtr<FCNAGraphPinFactory> PinFactory;
	TSharedPtr<FCNAPinConnectionFactory> ConnectionFactory;

	/** The function where it was first declared (an override in the asset class points back to the base). */
	const UFunction* GetRootFunction(const UFunction* Function)
	{
		const UFunction* Root = Function;
		while (Root && Root->GetSuperFunction())
		{
			Root = Root->GetSuperFunction();
		}
		return Root;
	}

	/** Declared by UCNABehaviorBase or by a native subclass of it (FCNAEditorHooks::BehaviorBaseClass). */
	bool IsBehaviorFunction(const UFunction* Function)
	{
		const UFunction* Root = GetRootFunction(Function);
		const UClass* Owner = Root ? Root->GetOwnerClass() : nullptr;
		return Owner && Owner->IsChildOf(UCNABehaviorBase::StaticClass()) && Owner->HasAnyClassFlags(CLASS_Native);
	}
}

// FCNAGraphNodeFactory

bool FCNAGraphNodeFactory::IsBehaviorNode(const UEdGraphNode* Node)
{
	if (!Node || !FCNAGraphUtils::IsBehaviorGraph(Node->GetGraph()))
	{
		return false;
	}
	if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		// FindEventSignatureFunction is not const in 4.20 (V-check).
		return CNAGraphFactoriesLocal::IsBehaviorFunction(const_cast<UK2Node_Event*>(EventNode)->FindEventSignatureFunction());
	}
	if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		return CNAGraphFactoriesLocal::IsBehaviorFunction(CallNode->GetTargetFunction());
	}
	return false;
}

TSharedPtr<SGraphNode> FCNAGraphNodeFactory::CreateNode(UEdGraphNode* Node) const
{
	if (UK2Node_CustomNode* CustomNode = Cast<UK2Node_CustomNode>(Node))
	{
		return SNew(SGraphNode_NodeFactory, CustomNode);
	}
	if (UK2Node_CNALocalVariableBase* VariableNode = Cast<UK2Node_CNALocalVariableBase>(Node))
	{
		return SNew(SGraphNodeCNALocalVariable, VariableNode);
	}
	if (IsBehaviorNode(Node))
	{
		return SNew(SGraphNodeCNABehavior, CastChecked<UK2Node>(Node));
	}
	return nullptr;
}

// FCNAGraphPinFactory

TSharedPtr<SGraphPin> FCNAGraphPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	return FCNAPinStyle::CreatePinWidget(Pin);
}

// FCNAGraphFactories

void FCNAGraphFactories::Register()
{
	using namespace CNAGraphFactoriesLocal;
	if (!NodeFactory.IsValid())
	{
		NodeFactory = MakeShareable(new FCNAGraphNodeFactory());
		FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);
	}
	if (!PinFactory.IsValid())
	{
		PinFactory = MakeShareable(new FCNAGraphPinFactory());
		FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);
	}
	if (!ConnectionFactory.IsValid())
	{
		ConnectionFactory = MakeShareable(new FCNAPinConnectionFactory());
		FEdGraphUtilities::RegisterVisualPinConnectionFactory(ConnectionFactory);
	}
}

void FCNAGraphFactories::Unregister()
{
	using namespace CNAGraphFactoriesLocal;
	// Reverse order of Register (spec 9.5).
	if (ConnectionFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinConnectionFactory(ConnectionFactory);
		ConnectionFactory.Reset();
	}
	if (PinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
		PinFactory.Reset();
	}
	if (NodeFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(NodeFactory);
		NodeFactory.Reset();
	}
}

bool FCNAGraphFactories::IsRegistered()
{
	return CNAGraphFactoriesLocal::NodeFactory.IsValid();
}
