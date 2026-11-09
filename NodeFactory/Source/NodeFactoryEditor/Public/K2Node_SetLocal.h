// Copyright (c) NodeFactory. Set Local: writes a local variable of the subgraph (spec 4.8, D9).

#pragma once

#include "CoreMinimal.h"
#include "K2Node_GetLocal.h"
#include "K2Node_SetLocal.generated.h"

/**
 * Writes a local variable. Impure: exec in and out, the new value in, and the value after the assignment out,
 * like the Set node of member variables. FCNAExpander turns it into a UK2Node_AssignmentStatement (V28).
 */
UCLASS(meta = (Keywords = "Local Variable Set Assign NodeFactory"))
class NODEFACTORYEDITOR_API UK2Node_SetLocal : public UK2Node_CNALocalVariableBase
{
	GENERATED_BODY()

public:
	UK2Node_SetLocal(const FObjectInitializer& ObjectInitializer);

	/** Name of the output that returns the value after the assignment. */
	static const FName OutputPinName;

	UEdGraphPin* GetExecPin() const;
	UEdGraphPin* GetThenPin() const;
	UEdGraphPin* GetValueInputPin() const;
	UEdGraphPin* GetValueOutputPin() const;

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

	// UK2Node
	virtual bool IsNodePure() const override { return false; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};
