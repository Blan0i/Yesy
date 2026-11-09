// Copyright (c) NodeFactory. Local variables of the subgraph: the shared base and Get Local (spec 4.8, D9).

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "CNATypes.h"
#include "K2Node_GetLocal.generated.h"

class UCustomNodeAsset;
class UEdGraph;
class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;

/**
 * Base of Get Local and Set Local. The node references a local variable of the asset whose macro Execute
 * contains it, by Guid (the name is kept for messages and for pasting into another asset).
 *
 * The nodes only exist inside the Execution Graph of a Custom Node Asset. FCNAExpander replaces them by accesses
 * to a UK2Node_TemporaryVariable of the calling graph, one per variable and instance (V28).
 */
UCLASS(Abstract)
class NODEFACTORYEDITOR_API UK2Node_CNALocalVariableBase : public UK2Node
{
	GENERATED_BODY()

public:
	UK2Node_CNALocalVariableBase(const FObjectInitializer& ObjectInitializer);

	/** Identity of the variable in FCNALocalVariable::Guid. */
	UPROPERTY()
	FGuid VariableGuid;

	/** Name at the time the variable was chosen; used when the Guid is unknown (pasted from another asset). */
	UPROPERTY()
	FName VariableName;

	/** Type of the variable when the pins were built; keeps the wires while the variable is missing (CNA017). */
	UPROPERTY()
	FEdGraphPinType CachedVariableType;

	/** Name of the value pin. */
	static const FName ValuePinName;

	/** The asset whose Execute graph contains the node. */
	UCustomNodeAsset* GetOwningAsset() const;

	/** The referenced variable, or nullptr (CNA017). */
	const FCNALocalVariable* FindVariable() const;

	/** Type of the variable or the cached type. */
	FEdGraphPinType GetVariableType() const;

	/** Name for titles and messages. */
	FText GetVariableDisplayName() const;

	/** Chooses another variable inside a transaction and rebuilds the node. */
	void SetVariable(const FCNALocalVariable& Variable);

	/** Binds a template node of the menu to a variable without a transaction. */
	void InitVariable(const FCNALocalVariable& Variable);

	/** Variables offered by the drop-down of SGraphNodeCNALocalVariable. */
	void GetSelectableVariables(TArray<const FCNALocalVariable*>& OutVariables) const;

	// UEdGraphNode
	virtual void ReconstructNode() override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	virtual void GetContextMenuActions(const FGraphNodeContextMenuBuilder& Context) const override;
	virtual void PostPasteNode() override;
	virtual FText GetKeywords() const override;

	// UK2Node
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual bool ShouldShowNodeProperties() const override { return false; }
	virtual bool NodeCausesStructuralBlueprintChange() const override { return false; }

	/** Writes CNA017 when the variable is missing; used by the validator of the custom node. */
	bool IsVariableValid() const { return FindVariable() != nullptr; }

protected:
	/** Creates the value pin with the variable type. */
	UEdGraphPin* CreateValuePin(EEdGraphPinDirection Direction, FName PinName);

	/** Updates the cached name and type from the variable. */
	void SyncCacheFromVariable();
};

/** Reads a local variable (spec 4.8). Pure: one output with the variable type. */
UCLASS(meta = (Keywords = "Local Variable Get NodeFactory"))
class NODEFACTORYEDITOR_API UK2Node_GetLocal : public UK2Node_CNALocalVariableBase
{
	GENERATED_BODY()

public:
	UK2Node_GetLocal(const FObjectInitializer& ObjectInitializer);

	UEdGraphPin* GetValuePin() const;

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

	// UK2Node
	virtual bool IsNodePure() const override { return true; }
	virtual bool ShouldDrawCompact() const override { return false; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};
