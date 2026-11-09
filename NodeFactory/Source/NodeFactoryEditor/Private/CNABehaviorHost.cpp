// Copyright (c) NodeFactory. Runs the Editor Behavior Graph of custom nodes (spec 6.1, 9.6).

#include "CNABehaviorHost.h"
#include "CNABehaviorBase.h"
#include "CNAMessages.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "NodeFactoryModule.h"
#include "Engine/Blueprint.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/Script.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

FCNABehaviorHost* FCNABehaviorHost::Instance = nullptr;

void FCNABehaviorHost::Startup()
{
	if (!Instance)
	{
		Instance = new FCNABehaviorHost();
	}
}

void FCNABehaviorHost::Shutdown()
{
	delete Instance;
	Instance = nullptr;
}

FCNABehaviorHost& FCNABehaviorHost::Get()
{
	if (!Instance)
	{
		Startup();
	}
	return *Instance;
}

FCNABehaviorHost::FCNABehaviorHost()
{
	ScriptExceptionHandle = FBlueprintCoreDelegates::OnScriptException.AddRaw(this, &FCNABehaviorHost::HandleScriptException);
}

FCNABehaviorHost::~FCNABehaviorHost()
{
	FBlueprintCoreDelegates::OnScriptException.Remove(ScriptExceptionHandle);
	Objects.Empty();
}

// Events

bool FCNABehaviorHost::NotifyNodePlaced(UK2Node_CustomNode* Node)
{
	return Dispatch(Node, ECNABehaviorEvent::NodePlaced, [Node](UCNABehaviorBase& Behavior)
	{
		Behavior.OnNodePlaced(Node);
	});
}

bool FCNABehaviorHost::NotifyWireChanged(UK2Node_CustomNode* Node, FName PinName, bool bConnected, const FEdGraphPinType& OtherPinType)
{
	const FCNAPinTypeDesc OtherType = FCNAPinTypeDesc::FromPinType(OtherPinType);
	return Dispatch(Node, ECNABehaviorEvent::WireChanged, [PinName, bConnected, &OtherType](UCNABehaviorBase& Behavior)
	{
		Behavior.OnWireChanged(PinName, bConnected, OtherType);
	});
}

bool FCNABehaviorHost::NotifyPinValueChanged(UK2Node_CustomNode* Node, FName PinName, const FString& NewValue)
{
	return Dispatch(Node, ECNABehaviorEvent::PinValueChanged, [PinName, &NewValue](UCNABehaviorBase& Behavior)
	{
		Behavior.OnPinValueChanged(PinName, NewValue);
	});
}

bool FCNABehaviorHost::NotifyAddPin(UK2Node_CustomNode* Node, int32 TotalDynamicPins)
{
	return Dispatch(Node, ECNABehaviorEvent::AddPin, [TotalDynamicPins](UCNABehaviorBase& Behavior)
	{
		Behavior.OnAddPin(TotalDynamicPins);
	});
}

bool FCNABehaviorHost::NotifyRemovePin(UK2Node_CustomNode* Node, int32 TotalDynamicPins, FName RemovedPinName)
{
	return Dispatch(Node, ECNABehaviorEvent::RemovePin, [TotalDynamicPins, RemovedPinName](UCNABehaviorBase& Behavior)
	{
		Behavior.OnRemovePin(TotalDynamicPins, RemovedPinName);
	});
}

bool FCNABehaviorHost::NotifyPinSplit(UK2Node_CustomNode* Node, FName PinName)
{
	return Dispatch(Node, ECNABehaviorEvent::PinSplit, [PinName](UCNABehaviorBase& Behavior)
	{
		Behavior.OnPinSplit(PinName);
	});
}

bool FCNABehaviorHost::NotifyPinRecombined(UK2Node_CustomNode* Node, FName PinName)
{
	return Dispatch(Node, ECNABehaviorEvent::PinRecombined, [PinName](UCNABehaviorBase& Behavior)
	{
		Behavior.OnPinRecombined(PinName);
	});
}

bool FCNABehaviorHost::NotifyAdvancedViewToggled(UK2Node_CustomNode* Node, bool bExpanded)
{
	return Dispatch(Node, ECNABehaviorEvent::AdvancedViewToggled, [bExpanded](UCNABehaviorBase& Behavior)
	{
		Behavior.OnAdvancedViewToggled(bExpanded);
	});
}

// Dispatch

FName FCNABehaviorHost::GetEventFunctionName(ECNABehaviorEvent Event)
{
	switch (Event)
	{
	case ECNABehaviorEvent::NodePlaced:          return UCNABehaviorBase::OnNodePlacedName;
	case ECNABehaviorEvent::WireChanged:         return UCNABehaviorBase::OnWireChangedName;
	case ECNABehaviorEvent::PinValueChanged:     return UCNABehaviorBase::OnPinValueChangedName;
	case ECNABehaviorEvent::AddPin:              return UCNABehaviorBase::OnAddPinName;
	case ECNABehaviorEvent::RemovePin:           return UCNABehaviorBase::OnRemovePinName;
	case ECNABehaviorEvent::PinSplit:            return UCNABehaviorBase::OnPinSplitName;
	case ECNABehaviorEvent::PinRecombined:       return UCNABehaviorBase::OnPinRecombinedName;
	case ECNABehaviorEvent::AdvancedViewToggled: return UCNABehaviorBase::OnAdvancedViewToggledName;
	default:                                     return NAME_None;
	}
}

bool FCNABehaviorHost::CanRunBehavior(const UCustomNodeAsset* Asset, FText& OutReason)
{
	if (!Asset)
	{
		OutReason = LOCTEXT("Behavior.NoAsset", "the asset is missing.");
		return false;
	}

	UClass* BehaviorClass = Asset->GeneratedClass;
	if (!BehaviorClass || Asset->Status == BS_Unknown)
	{
		OutReason = LOCTEXT("Behavior.NotCompiled", "the asset is not compiled, Behavior events are not called. Compile the asset.");
		return false;
	}
	if (Asset->Status == BS_Error)
	{
		OutReason = LOCTEXT("Behavior.CompileFailed", "the asset has compile errors, Behavior events are not called.");
		return false;
	}
	if (!BehaviorClass->IsChildOf(UCNABehaviorBase::StaticClass()))
	{
		OutReason = LOCTEXT("Behavior.WrongParent", "the parent class of the asset is not CNA Behavior Base.");
		return false;
	}
	if (BehaviorClass->HasAnyClassFlags(CLASS_NewerVersionExists | CLASS_Abstract) && BehaviorClass != UCNABehaviorBase::StaticClass())
	{
		OutReason = LOCTEXT("Behavior.StaleClass", "the compiled class is outdated, compile the asset.");
		return false;
	}
	return true;
}

bool FCNABehaviorHost::IsEventImplemented(const UCustomNodeAsset* Asset, ECNABehaviorEvent Event)
{
	UClass* BehaviorClass = Asset ? Asset->GeneratedClass : nullptr;
	if (!BehaviorClass)
	{
		return false;
	}
	const UFunction* Function = BehaviorClass->FindFunctionByName(GetEventFunctionName(Event));
	// A BlueprintImplementableEvent that the asset implements is overridden by a function of the generated class.
	return Function && Function->GetOuterUClass() != UCNABehaviorBase::StaticClass() && Function->Script.Num() > 0;
}

void FCNABehaviorHost::ReportUnusableOnce(UK2Node_CustomNode* Node, const UCustomNodeAsset* Asset, const FText& Reason)
{
	// One report per asset state: compiling the asset again (new status or class) may report again.
	const FString Key = Asset
		? FString::Printf(TEXT("%s|%d|%s"), *Asset->GetPathName(), static_cast<int32>(Asset->Status), Asset->GeneratedClass ? *Asset->GeneratedClass->GetPathName() : TEXT(""))
		: FString();
	if (ReportedUnusableAssets.Contains(Key))
	{
		return;
	}
	ReportedUnusableAssets.Add(Key);
	ReportProblem(Node, Reason);
}

bool FCNABehaviorHost::Dispatch(UK2Node_CustomNode* Node, ECNABehaviorEvent Event, TFunctionRef<void(UCNABehaviorBase&)> Invoke)
{
	if (!Node || Node->IsTemplate() || Node->IsPendingKill() || Node->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return false;
	}
	// Undo / Redo restores the recorded state; Behavior must not run again (spec 9.4).
	if (GIsTransacting || IsGarbageCollecting() || GIsSavingPackage || Node->IsBeingCompiled())
	{
		return false;
	}
	// Intermediate nodes of the compiler live in transient graphs; only the Live Preview graph is allowed.
	const UEdGraph* Graph = Node->GetGraph();
	if (!Graph || (Graph->HasAnyFlags(RF_Transient) && !Node->IsPreviewNode()))
	{
		return false;
	}
	// Re-entry lock: an event raised by an action of the same Behavior is not dispatched (spec 6.1, 9.6).
	if (Node->bBehaviorLock)
	{
		CNA_VERBOSE(TEXT("Behavior event %d of %s skipped: re-entry lock."), static_cast<int32>(Event), *Node->GetName());
		return false;
	}

	UCustomNodeAsset* Asset = Node->GetCustomNodeAsset();
	if (!Asset)
	{
		return false;
	}

	FText Reason;
	if (!CanRunBehavior(Asset, Reason))
	{
		// Assets without any Behavior page simply have nothing to run.
		if (Asset->UbergraphPages.Num() > 0)
		{
			ReportUnusableOnce(Node, Asset, Reason);
		}
		return false;
	}

	if (!IsEventImplemented(Asset, Event))
	{
		return false;
	}

	UCNABehaviorBase* Behavior = GetOrCreateObject(Node, Asset->GeneratedClass);
	if (!Behavior)
	{
		return false;
	}

	TGuardValue<bool> Lock(Node->bBehaviorLock, true);

	// ThrowNodeError / ThrowNodeWarning last until the next event (V22).
	Node->ClearBehaviorMessage();

	// One Undo transaction per event (spec 6.1). Live Preview nodes are not transactional (spec 7.1).
	const bool bTransact = !Node->IsPreviewNode();
	FScopedTransaction Transaction(FText::Format(LOCTEXT("Behavior.Transaction", "NodeFactory Behavior: {0}"),
		FText::FromName(GetEventFunctionName(Event))), bTransact);
	if (bTransact)
	{
		Node->Modify();
	}

	UCNABehaviorBase* PreviousObject = ActiveObject;
	TArray<FString> PreviousErrors = MoveTemp(ActiveErrors);
	ActiveObject = Behavior;
	ActiveErrors.Reset();

	Node->BeginBehaviorChanges();
	{
		// Script execution of editor objects (spec 6.1); the VM keeps its own infinite loop protection.
		FEditorScriptExecutionGuard ScriptGuard;
		Invoke(*Behavior);
	}
	Node->EndBehaviorChanges();

	const TArray<FString> Errors = MoveTemp(ActiveErrors);
	ActiveObject = PreviousObject;
	ActiveErrors = MoveTemp(PreviousErrors);

	for (const FString& Error : Errors)
	{
		ReportProblem(Node, FText::FromString(Error));
	}

	CNA_VERBOSE(TEXT("Behavior event %s executed on %s."), *GetEventFunctionName(Event).ToString(), *Node->GetName());
	OnEventDispatched.Broadcast(Node, Event);
	return true;
}

UCNABehaviorBase* FCNABehaviorHost::GetOrCreateObject(UK2Node_CustomNode* Node, UClass* BehaviorClass)
{
	PurgeStaleEntries();

	UCNABehaviorBase*& Slot = Objects.FindOrAdd(Node);
	if (!Slot || Slot->IsPendingKill() || Slot->GetClass() != BehaviorClass)
	{
		// Objects of a recompiled class are replaced; the old ones are released to the GC (spec 9.6).
		Slot = NewObject<UCNABehaviorBase>(GetTransientPackage(), BehaviorClass, NAME_None, RF_Transient);
	}
	if (Slot)
	{
		Slot->BindToNode(Node);
	}
	return Slot;
}

void FCNABehaviorHost::PurgeStaleEntries()
{
	for (auto It = Objects.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value())
		{
			It.RemoveCurrent();
		}
	}
}

void FCNABehaviorHost::ReportProblem(UK2Node_CustomNode* Node, const FText& Details)
{
	FFormatNamedArguments Args = FCNAMessages::AssetArgs(Node ? Node->GetCustomNodeAsset() : nullptr);
	Args.Add(TEXT("Details"), Details);
	FCNAMessages::ReportToLog(ECNAMessage::CNA011, Args, Node);
}

void FCNABehaviorHost::HandleScriptException(const UObject* ExceptionObject, const FFrame& StackFrame, const FBlueprintExceptionInfo& Info)
{
	if (ActiveObject && ExceptionObject == ActiveObject)
	{
		// Accessed None, infinite loop protection and other VM errors become CNA011 (spec 6.1).
		ActiveErrors.Add(Info.GetDescription().ToString());
	}
}

// Lifetime

void FCNABehaviorHost::ForgetNode(const UK2Node_CustomNode* Node)
{
	for (auto It = Objects.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Key().Get() == Node)
		{
			It.RemoveCurrent();
		}
	}
}

void FCNABehaviorHost::ForgetAsset(const UCustomNodeAsset* Asset)
{
	if (!Asset)
	{
		return;
	}
	const FString Prefix = Asset->GetPathName() + TEXT("|");
	for (auto It = ReportedUnusableAssets.CreateIterator(); It; ++It)
	{
		if (It->StartsWith(Prefix))
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = Objects.CreateIterator(); It; ++It)
	{
		const UK2Node_CustomNode* Node = It.Key().Get();
		if (!Node || Node->GetCustomNodeAsset() == Asset)
		{
			It.RemoveCurrent();
		}
	}
}

int32 FCNABehaviorHost::GetNumBehaviorObjects() const
{
	int32 Count = 0;
	for (const auto& Pair : Objects)
	{
		Count += (Pair.Key.IsValid() && Pair.Value) ? 1 : 0;
	}
	return Count;
}

bool FCNABehaviorHost::IsDispatching(const UK2Node_CustomNode* Node) const
{
	return Node && Node->bBehaviorLock;
}

void FCNABehaviorHost::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& Pair : Objects)
	{
		if (Pair.Value)
		{
			Collector.AddReferencedObject(Pair.Value);
		}
	}
	if (ActiveObject)
	{
		Collector.AddReferencedObject(ActiveObject);
	}
}

#undef LOCTEXT_NAMESPACE
