// Copyright (c) NodeFactory. Curve Value: reads a Curve body widget inside the macro Execute of a Custom Node Asset.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "CNATypes.h"
#include "K2Node_CNACurveValue.generated.h"

class UCustomNodeAsset;
class UEdGraph;
class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;

/**
 * Pure node with the outputs Value (Float, Vector or LinearColor, by the ValueType of the curve) and Time.
 * It references a Curve widget of the asset by WidgetGuid; the name is kept for messages and for pasting into
 * another asset. In the asset window the node is only a placeholder: FCNAExpander replaces it by engine nodes
 * that evaluate the curve at the time of the instance (Time pin or playback position).
 */
UCLASS(meta = (Keywords = "Curve Value Timeline Track NodeFactory"))
class NODEFACTORYEDITOR_API UK2Node_CNACurveValue : public UK2Node
{
	GENERATED_BODY()

public:
	UK2Node_CNACurveValue(const FObjectInitializer& ObjectInitializer);

	/** WidgetGuid of the Curve body widget. */
	UPROPERTY()
	FGuid CurveGuid;

	/** Name of the curve when it was chosen; used when the Guid is unknown (pasted from another asset). */
	UPROPERTY()
	FName CurveName;

	/** Value type when the pins were built; keeps the wires while the curve is missing (CNA023). */
	UPROPERTY()
	ECNACurveValueType CachedValueType;

	static const FName ValuePinName;
	static const FName TimePinName;

	UEdGraphPin* GetValuePin() const;
	UEdGraphPin* GetTimePin() const;

	/** The asset whose Execute graph contains the node. */
	UCustomNodeAsset* GetOwningAsset() const;

	/** The referenced curve in the owning asset, or nullptr (CNA023). */
	const FCNABodyWidget* FindCurve() const;

	/** The referenced curve in another asset: the expander resolves the clone against the asset of the instance. */
	const FCNABodyWidget* FindCurveIn(const UCustomNodeAsset* Asset) const;

	/** Value type of the curve or the cached one. */
	ECNACurveValueType GetValueType() const;

	/** Name for titles and messages. */
	FText GetCurveDisplayName() const;

	/** The Value pin no longer has the type of the curve, or the curve was renamed. */
	bool NeedsRefresh() const;

	/** Chooses another curve inside a transaction and rebuilds the node (context menu, drop-down of the widget). */
	void SetCurve(const FCNABodyWidget& Widget);

	/** Binds a node to a curve without a transaction (menu spawner). */
	void InitCurve(const FCNABodyWidget& Widget);

	/** Curves the node can reference: every Curve widget of the owning asset. */
	void GetSelectableCurves(TArray<const FCNABodyWidget*>& OutCurves) const;

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual void ReconstructNode() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	virtual void GetContextMenuActions(const FGraphNodeContextMenuBuilder& Context) const override;
	virtual void PostPasteNode() override;
	virtual FText GetKeywords() const override;

	// UK2Node
	virtual bool IsNodePure() const override { return true; }
	virtual bool ShouldDrawCompact() const override { return false; }
	virtual bool ShouldShowNodeProperties() const override { return false; }
	virtual bool NodeCausesStructuralBlueprintChange() const override { return false; }
	virtual FText GetMenuCategory() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

private:
	/** Updates the cached name and type from the curve. */
	void SyncCacheFromCurve();
};
