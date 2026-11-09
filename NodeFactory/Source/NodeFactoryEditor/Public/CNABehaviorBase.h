// Copyright (c) NodeFactory. Parent class of every Custom Node Asset: the Editor Behavior Graph (spec 6, D4).
//
// Events are BlueprintImplementableEvent and appear in the Event Graph of the asset through the usual
// right click menu ("Event On Node Placed" ...). Actions are BlueprintCallable functions of this class.
// For every node instance FCNABehaviorHost creates one transient object of the compiled class of the
// asset and calls its events. Behavior keeps no state: everything that must survive is written into the
// instance data of the node (InstanceOverrides, ResolvedWildcardType, TitleOverride).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "EdGraph/EdGraphPin.h"
#include "CNATypes.h"
#include "CNABehaviorBase.generated.h"

class UK2Node_CustomNode;

/** Container of a pin type used by Behavior (spec 4.3). */
UENUM(BlueprintType)
enum class ECNAPinContainer : uint8
{
	Single,
	Array,
	Set,
	Map
};

/** Blueprint friendly description of a pin type: parameter of OnWireChanged and ChangePinDataType. */
USTRUCT(BlueprintType)
struct NODEFACTORYEDITOR_API FCNAPinTypeDesc
{
	GENERATED_BODY()

	/** K2 category: bool, int, float, string, name, text, struct, object, class, byte, wildcard, exec ... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin Type")
	FName Category;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin Type")
	FName SubCategory;

	/** Struct, enum or class of the type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin Type")
	UObject* SubCategoryObject = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin Type")
	ECNAPinContainer Container = ECNAPinContainer::Single;

	FEdGraphPinType ToPinType() const;
	static FCNAPinTypeDesc FromPinType(const FEdGraphPinType& Type);
	bool IsWildcard() const;
	bool IsValid() const { return !Category.IsNone(); }
};

UCLASS(Abstract, Blueprintable, meta = (DisplayName = "CNA Behavior Base"))
class NODEFACTORYEDITOR_API UCNABehaviorBase : public UObject
{
	GENERATED_BODY()

public:
	// Events (spec 6.2)

	/** The node was placed into a graph (PostPlacedNewNode). Node is the UK2Node_CustomNode instance. */
	UFUNCTION(BlueprintImplementableEvent, Category = "NodeFactory|Events", meta = (DisplayName = "On Node Placed"))
	void OnNodePlaced(UObject* Node);

	/** A wire of the pin was connected or disconnected (PinConnectionListChanged). */
	UFUNCTION(BlueprintImplementableEvent, Category = "NodeFactory|Events", meta = (DisplayName = "On Wire Changed"))
	void OnWireChanged(FName PinName, bool bConnected, const FCNAPinTypeDesc& OtherPinType);

	/** Default value of an input changed: pin field, BodyWidgets or a node property (PinDefaultValueChanged). */
	UFUNCTION(BlueprintImplementableEvent, Category = "NodeFactory|Events", meta = (DisplayName = "On Pin Value Changed"))
	void OnPinValueChanged(FName PinName, const FString& NewValue);

	/** A dynamic pin was added with the "+" button or by DynamicPinCount (V4). */
	UFUNCTION(BlueprintImplementableEvent, Category = "NodeFactory|Events", meta = (DisplayName = "On Add Pin"))
	void OnAddPin(int32 TotalDynamicPins);

	/** A dynamic pin was removed from the context menu or by DynamicPinCount (V4). */
	UFUNCTION(BlueprintImplementableEvent, Category = "NodeFactory|Events", meta = (DisplayName = "On Remove Pin"))
	void OnRemovePin(int32 TotalDynamicPins, FName RemovedPinName);

	/** A struct pin was split (V15). */
	UFUNCTION(BlueprintImplementableEvent, Category = "NodeFactory|Events", meta = (DisplayName = "On Pin Split"))
	void OnPinSplit(FName PinName);

	/** A split struct pin was recombined (V15). */
	UFUNCTION(BlueprintImplementableEvent, Category = "NodeFactory|Events", meta = (DisplayName = "On Pin Recombined"))
	void OnPinRecombined(FName PinName);

	/** The Advanced arrow at the bottom of the node was toggled (SGraphNode_NodeFactory, V24). */
	UFUNCTION(BlueprintImplementableEvent, Category = "NodeFactory|Events", meta = (DisplayName = "On Advanced View Toggled"))
	void OnAdvancedViewToggled(bool bExpanded);

	// Actions (spec 6.3)

	/** Shows or hides a pin. A connected pin cannot be hidden: returns false and reports CNA011. */
	UFUNCTION(BlueprintCallable, Category = "NodeFactory|Actions")
	bool SetPinVisibility(FName PinName, bool bVisible);

	/** Changes the displayed name; the identity of the pin stays. */
	UFUNCTION(BlueprintCallable, Category = "NodeFactory|Actions")
	bool RenamePin(FName PinName, const FText& NewDisplayName);

	/** Changes the type of a Wildcard pin (and of all Wildcard pins of the node, spec 4.5). Other pins: CNA011. */
	UFUNCTION(BlueprintCallable, Category = "NodeFactory|Actions")
	bool ChangePinDataType(FName PinName, const FCNAPinTypeDesc& NewType);

	/** Overrides DisplayName for this node instance (TitleOverride). An empty text restores the asset title. */
	UFUNCTION(BlueprintCallable, Category = "NodeFactory|Actions")
	void SetNodeTitle(const FText& NewTitle);

	/** Red frame and message on the node until the next event (V22). Not saved. */
	UFUNCTION(BlueprintCallable, Category = "NodeFactory|Actions")
	void ThrowNodeError(const FString& Message);

	/** Yellow frame and message on the node until the next event (V22). Not saved. */
	UFUNCTION(BlueprintCallable, Category = "NodeFactory|Actions")
	void ThrowNodeWarning(const FString& Message);

	/** Connection rule of the pin on top of the rule of the asset (InstanceOverrides). */
	UFUNCTION(BlueprintCallable, Category = "NodeFactory|Actions")
	bool SetPinConnectionRule(FName PinName, ECNAConnectionRule Rule, TSubclassOf<UObject> ClassFilter);

	// Queries

	/** Default value of an input as text; empty for unknown pins. */
	UFUNCTION(BlueprintPure, Category = "NodeFactory|Queries")
	FString GetPinValue(FName PinName) const;

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Queries")
	bool IsPinConnected(FName PinName) const;

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Queries")
	bool IsPinVisible(FName PinName) const;

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Queries")
	bool HasPin(FName PinName) const;

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Queries")
	FCNAPinTypeDesc GetPinType(FName PinName) const;

	/** Number of element pins of the dynamic group (DynamicPinCount). */
	UFUNCTION(BlueprintPure, Category = "NodeFactory|Queries")
	int32 GetDynamicPinCount() const;

	/** Internal name of the dynamic element pin with the index, for RenamePin and SetPinVisibility. */
	UFUNCTION(BlueprintPure, Category = "NodeFactory|Queries")
	FName GetDynamicPinName(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Queries")
	TArray<FName> GetPinNames(bool bInputs, bool bOutputs) const;

	/** The node is the Live Preview of the asset window (no transactions, spec 7.1). */
	UFUNCTION(BlueprintPure, Category = "NodeFactory|Queries")
	bool IsPreview() const;

	// Pin type helpers

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeBooleanType();

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeIntegerType();

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeFloatType();

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeStringType();

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeNameType();

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeTextType();

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeVectorType();

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeRotatorType();

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeTransformType();

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeObjectType(UClass* Class);

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeClassType(UClass* MetaClass);

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeStructType(UScriptStruct* Struct);

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeEnumType(UEnum* Enum);

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static FCNAPinTypeDesc MakeArrayOf(const FCNAPinTypeDesc& ElementType);

	UFUNCTION(BlueprintPure, Category = "NodeFactory|Types")
	static bool IsSamePinType(const FCNAPinTypeDesc& A, const FCNAPinTypeDesc& B);

	// Host side

	/** Binds the object to a node instance (FCNABehaviorHost). */
	void BindToNode(UK2Node_CustomNode* Node) { BoundNode = Node; }
	UK2Node_CustomNode* GetBoundNode() const { return BoundNode.Get(); }

	/** Function names of the events, used to check whether the asset implements an event. */
	static const FName OnNodePlacedName;
	static const FName OnWireChangedName;
	static const FName OnPinValueChangedName;
	static const FName OnAddPinName;
	static const FName OnRemovePinName;
	static const FName OnPinSplitName;
	static const FName OnPinRecombinedName;
	static const FName OnAdvancedViewToggledName;

private:
	/** Reports a refused action as CNA011 (spec 6.3). */
	void ReportRefusedAction(const FText& Details) const;

	/** Node of this object; weak because the object must not keep the graph alive (spec 9.6). */
	TWeakObjectPtr<UK2Node_CustomNode> BoundNode;
};
