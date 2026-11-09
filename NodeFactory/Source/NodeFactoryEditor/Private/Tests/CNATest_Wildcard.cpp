// Copyright (c) NodeFactory. Wildcard pins: check A8 of spec 12.2.

#include "CNATestUtils.h"
#include "CNALocalization.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace CNATest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAWildcardTest, "NodeFactory.Pins.Wildcard", CNA_TEST_FLAGS)

bool FCNAWildcardTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	UCustomNodeAsset* Asset = MakeAsset(Scope, TEXT("CNATest_Wildcard"));
	AddPin(Asset, EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, TEXT("Value"));
	AddPin(Asset, EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, TEXT("Result"));
	TestTrue(TEXT("Subgraph"), Link(Tunnel(Asset, EGPD_Input, TEXT("Value")), Tunnel(Asset, EGPD_Output, TEXT("Result"))));
	Commit(Asset);

	UBlueprint* Blueprint = MakeBlueprint(Scope);
	UEdGraph* Graph = nullptr;
	UEdGraphPin* Start = EventThen(Blueprint, Graph);
	UK2Node_CustomNode* Node = PlaceNode(Graph, Asset);
	Link(Start, Node->FindPin(UEdGraphSchema_K2::PN_Execute));
	auto ResultCategory = [Node]() { const UEdGraphPin* Pin = Node->FindPin(TEXT("Result")); return Pin ? Pin->PinType.PinCategory : NAME_None; };
	TestTrue(TEXT("Open Wildcard"), Node->HasWildcardPins() && !Node->IsWildcardResolved());
	FCompilerResultsLog OpenResults;
	ExpectCompile(*this, Blueprint, false, OpenResults);
	TestTrue(TEXT("CNA007 without a wire"), HasCode(OpenResults, TEXT("CNA007")));

	// A Float wire resolves every Wildcard pin of the node.
	UEdGraphPin* Source = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("RandomFloat"))->GetReturnValuePin();
	TestTrue(TEXT("Wire"), Link(Source, Node->FindPin(TEXT("Value"))));
	TestTrue(TEXT("Resolved to Float"), Node->IsWildcardResolved() && ResultCategory() == UEdGraphSchema_K2::PC_Float);
	FCompilerResultsLog Results;
	ExpectCompile(*this, Blueprint, true, Results);

	// Breaking the wire returns the Wildcard.
	GetDefault<UEdGraphSchema_K2>()->BreakPinLinks(*Node->FindPin(TEXT("Value")), /*bSendsNodeNotification*/ true);
	TestTrue(TEXT("Wildcard again"), !Node->IsWildcardResolved() && ResultCategory() == UEdGraphSchema_K2::PC_Wildcard);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
