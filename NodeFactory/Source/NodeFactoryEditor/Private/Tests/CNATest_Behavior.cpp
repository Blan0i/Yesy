// Copyright (c) NodeFactory. Behavior events and actions: check A10 of spec 12.2.

#include "CNATestUtils.h"
#include "CNABehaviorBase.h"
#include "CNABehaviorHost.h"
#include "K2Node_Event.h"
#include "CNALocalization.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace CNATest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNABehaviorTest, "NodeFactory.Behavior", CNA_TEST_FLAGS)

bool FCNABehaviorTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	UCustomNodeAsset* Asset = MakeAsset(Scope, TEXT("CNATest_Behavior"));
	AddPin(Asset, EGPD_Input, UEdGraphSchema_K2::PC_Float, TEXT("Extra"));
	Commit(Asset);

	// Behavior Graph: On Node Placed -> SetPinVisibility(Extra, false); On Pin Value Changed -> ThrowNodeError.
	auto AddReaction = [this, Asset](FName Event, const TCHAR* Action, const TCHAR* PinName, const TCHAR* Value)
	{
		UK2Node_Event* EventNode = Spawn<UK2Node_Event>(Asset->FindBehaviorGraph(), [Event](UK2Node_Event* Node)
		{
			Node->EventReference.SetExternalMember(Event, UCNABehaviorBase::StaticClass());
			Node->bOverrideFunction = true;
		});
		UK2Node_CallFunction* Call = SpawnCall(Asset->FindBehaviorGraph(), UCNABehaviorBase::StaticClass(), Action);
		UEdGraphPin* Argument = Call->FindPin(PinName);
		TestTrue(Action, Argument && Link(EventNode->FindPin(UEdGraphSchema_K2::PN_Then), Call->GetExecPin()));
		if (Argument)
		{
			Argument->DefaultValue = Value;
		}
	};
	AddReaction(UCNABehaviorBase::OnNodePlacedName, TEXT("SetPinVisibility"), TEXT("PinName"), TEXT("Extra"));
	AddReaction(UCNABehaviorBase::OnPinValueChangedName, TEXT("ThrowNodeError"), TEXT("Message"), TEXT("Bad value"));
	FKismetEditorUtilities::CompileBlueprint(Asset, EBlueprintCompileOptions::SkipGarbageCollection);
	TestTrue(TEXT("Events implemented"), FCNABehaviorHost::IsEventImplemented(Asset, ECNABehaviorEvent::NodePlaced)
		&& FCNABehaviorHost::IsEventImplemented(Asset, ECNABehaviorEvent::PinValueChanged));

	// Each dispatched event tries to raise another one on its node while it runs: the re-entry lock refuses it.
	FCNABehaviorHost& Host = FCNABehaviorHost::Get();
	int32 Dispatched = 0;
	int32 Reentered = 0;
	const FDelegateHandle Handle = Host.OnEventDispatched.AddLambda([&Host, &Dispatched, &Reentered](UK2Node_CustomNode* Node, ECNABehaviorEvent)
	{
		++Dispatched;
		Reentered += Host.NotifyNodePlaced(Node) ? 1 : 0;
	});

	UBlueprint* Blueprint = MakeBlueprint(Scope);
	UK2Node_CustomNode* Node = PlaceNode(Blueprint->UbergraphPages[0], Asset);
	UEdGraphPin* Extra = Node->FindPin(TEXT("Extra"));
	TestTrue(TEXT("OnNodePlaced hid the pin"), Extra && Extra->bHidden);
	if (Extra)
	{
		Extra->DefaultValue = TEXT("2.0");
		Node->PinDefaultValueChanged(Extra);
	}
	TestTrue(TEXT("ThrowNodeError is shown on the node"), Node->bHasCompilerMessage && Node->ErrorType == EMessageSeverity::Error
		&& Node->ErrorMsg.Equals(TEXT("Bad value")));
	Host.OnEventDispatched.Remove(Handle);
	TestTrue(TEXT("Two events and no re-entry"), Dispatched == 2 && Reentered == 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
