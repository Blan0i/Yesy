// Copyright (c) NodeFactory. Runs the Editor Behavior Graph of custom nodes (spec 6.1, 9.6).
//
// For every node instance the host lazily creates one transient object of the compiled class of the
// asset (objects are created on the first event, spec 9.8) and calls the event on it:
//   * class missing, not compiled or compiled with errors -> no call, CNA011 once per asset;
//   * events the asset does not implement are skipped without a transaction;
//   * an event raised by an action of the same Behavior is not dispatched again (re-entry lock, TGuardValue);
//   * every event runs inside one Undo transaction (none for Live Preview nodes and while transacting);
//   * script exceptions (Accessed None, infinite loop protection) become CNA011 and never stop the editor.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "EdGraph/EdGraphPin.h"

class UK2Node_CustomNode;
class UCustomNodeAsset;
class UCNABehaviorBase;
class UBlueprint;
struct FFrame;
struct FBlueprintExceptionInfo;

/** Events of spec 6.2. */
enum class ECNABehaviorEvent : uint8
{
	NodePlaced,
	WireChanged,
	PinValueChanged,
	AddPin,
	RemovePin,
	PinSplit,
	PinRecombined,
	AdvancedViewToggled
};

class NODEFACTORYEDITOR_API FCNABehaviorHost : public FGCObject
{
public:
	/** Created by the editor module (spec 9.5). Get() creates the host on demand for automation tests. */
	static void Startup();
	static void Shutdown();
	static FCNABehaviorHost& Get();
	static bool IsAvailable() { return Instance != nullptr; }

	FCNABehaviorHost();
	virtual ~FCNABehaviorHost();

	// Event entry points (called by UK2Node_CustomNode and SGraphNode_NodeFactory)

	bool NotifyNodePlaced(UK2Node_CustomNode* Node);
	bool NotifyWireChanged(UK2Node_CustomNode* Node, FName PinName, bool bConnected, const FEdGraphPinType& OtherPinType);
	bool NotifyPinValueChanged(UK2Node_CustomNode* Node, FName PinName, const FString& NewValue);
	bool NotifyAddPin(UK2Node_CustomNode* Node, int32 TotalDynamicPins);
	bool NotifyRemovePin(UK2Node_CustomNode* Node, int32 TotalDynamicPins, FName RemovedPinName);
	bool NotifyPinSplit(UK2Node_CustomNode* Node, FName PinName);
	bool NotifyPinRecombined(UK2Node_CustomNode* Node, FName PinName);
	bool NotifyAdvancedViewToggled(UK2Node_CustomNode* Node, bool bExpanded);

	/** CNA011 in the Message Log with a link to the node (spec 6.1: errors never stop the editor). */
	void ReportProblem(UK2Node_CustomNode* Node, const FText& Details);

	// Lifetime of the transient objects

	/** The node was destroyed or its asset changed. */
	void ForgetNode(const UK2Node_CustomNode* Node);

	/** The asset was recompiled: the objects of the old class are dropped and CNA011 may be reported again. */
	void ForgetAsset(const UCustomNodeAsset* Asset);

	/** Number of live Behavior objects (automation tests, spec 9.8). */
	int32 GetNumBehaviorObjects() const;

	/** The node is inside one of its events right now. */
	bool IsDispatching(const UK2Node_CustomNode* Node) const;

	/** The asset implements the event in its Behavior Graph. */
	static bool IsEventImplemented(const UCustomNodeAsset* Asset, ECNABehaviorEvent Event);

	/** The compiled class can run events; otherwise OutReason explains why (CNA011). */
	static bool CanRunBehavior(const UCustomNodeAsset* Asset, FText& OutReason);

	static FName GetEventFunctionName(ECNABehaviorEvent Event);

	/** Broadcast after an event was executed (used by the automation tests of A10). */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnEventDispatched, UK2Node_CustomNode* /*Node*/, ECNABehaviorEvent /*Event*/);
	FOnEventDispatched OnEventDispatched;

	//~ Begin FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ End FGCObject Interface

private:
	bool Dispatch(UK2Node_CustomNode* Node, ECNABehaviorEvent Event, TFunctionRef<void(UCNABehaviorBase&)> Invoke);
	UCNABehaviorBase* GetOrCreateObject(UK2Node_CustomNode* Node, UClass* BehaviorClass);
	void PurgeStaleEntries();
	void HandleScriptException(const UObject* ActiveObject, const FFrame& StackFrame, const FBlueprintExceptionInfo& Info);
	void ReportUnusableOnce(UK2Node_CustomNode* Node, const UCustomNodeAsset* Asset, const FText& Reason);

	/** One Behavior object per node instance. Values are kept alive through AddReferencedObjects. */
	TMap<TWeakObjectPtr<UK2Node_CustomNode>, UCNABehaviorBase*> Objects;

	/** Asset states that already reported CNA011 "cannot run" (key: path, status and class). */
	TSet<FString> ReportedUnusableAssets;

	/** Object that executes an event right now; script exceptions of this object become CNA011. */
	UCNABehaviorBase* ActiveObject = nullptr;
	TArray<FString> ActiveErrors;

	FDelegateHandle ScriptExceptionHandle;

	static FCNABehaviorHost* Instance;
};
