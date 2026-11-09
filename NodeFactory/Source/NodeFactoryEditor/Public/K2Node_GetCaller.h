// Copyright (c) NodeFactory. Get Caller: the object that executes the custom node (spec 4.9, D9).

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_GetCaller.generated.h"

class UCustomNodeAsset;
class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;

/**
 * Node without inputs and with one output of the type ContextClass (UObject without it). FCNAExpander replaces it
 * by a UK2Node_Self of the calling Blueprint (V29), so the subgraph reaches the caller and its world: functions
 * of Actor connect to the output, WorldContext pins connect to it as well.
 *
 * Only available in the Execution Graph; allowed in a pure node.
 */
UCLASS(meta = (Keywords = "Self Caller Owner World Context NodeFactory"))
class NODEFACTORYEDITOR_API UK2Node_GetCaller : public UK2Node
{
	GENERATED_BODY()

public:
	UK2Node_GetCaller(const FObjectInitializer& ObjectInitializer);

	static const FName CallerPinName;

	UEdGraphPin* GetCallerPin() const;

	/** The asset whose Execute graph contains the node. */
	UCustomNodeAsset* GetOwningAsset() const;

	/** ContextClass of the asset, UObject without one. */
	UClass* GetCallerClass() const;

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	virtual FText GetKeywords() const override;

	// UK2Node
	virtual bool IsNodePure() const override { return true; }
	virtual bool ShouldDrawCompact() const override { return false; }
	virtual FText GetMenuCategory() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual bool NodeCausesStructuralBlueprintChange() const override { return false; }
};
