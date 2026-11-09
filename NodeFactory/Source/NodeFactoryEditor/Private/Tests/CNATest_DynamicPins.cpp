// Copyright (c) NodeFactory. Dynamic pins and Make Array: check A7 of spec 12.2.

#include "CNATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace CNATest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNADynamicPinsTest, "NodeFactory.Pins.Dynamic", CNA_TEST_FLAGS)

bool FCNADynamicPinsTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	UCustomNodeAsset* Asset = MakeAsset(Scope, TEXT("CNATest_Dynamic"));
	AddPin(Asset, EGPD_Input, UEdGraphSchema_K2::PC_Float, TEXT("Items"), EPinContainerType::Array);
	FCNAPinOverride& Override = Asset->FindOrAddPinOverride(TEXT("Items"));
	Override.bDynamicGroup = true;
	Override.DynamicMin = 2;
	Override.DynamicMax = 4;
	Commit(Asset);

	UBlueprint* Blueprint = MakeBlueprint(Scope);
	UEdGraph* Graph = nullptr;
	UEdGraphPin* Start = EventThen(Blueprint, Graph);
	UK2Node_CustomNode* Node = PlaceNode(Graph, Asset);
	Link(Start, Node->FindPin(UEdGraphSchema_K2::PN_Execute));
	TestEqual(TEXT("A new node starts at DynamicMin"), Node->GetDynamicPinCount(), 2);

	Node->SetDynamicPinCount(10, /*bFireEvents*/ false);
	TestTrue(TEXT("Clamped to DynamicMax"), Node->GetDynamicPinCount() == 4 && !Node->CanAddDynamicElement());
	for (int32 Step = 0; Step < 3; ++Step)
	{
		Node->RemoveDynamicElement(0);
	}
	TestTrue(TEXT("Stops at DynamicMin"), Node->GetDynamicPinCount() == 2 && !Node->CanRemoveDynamicElement());
	Node->AddDynamicElement();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const bool bExists = Node->FindPin(CNATypes::MakeDynamicPinName(TEXT("Items"), Index)) != nullptr;
		TestTrue(*FString::Printf(TEXT("Element pin %d"), Index), bExists == (Index < 3));
	}

	// The subgraph receives an array of exactly the element count, at both ends of the range.
	for (const int32 Count : { 2, 4 })
	{
		Node->SetDynamicPinCount(Count, false);
		FCNAExpander::ResetStats();
		FCompilerResultsLog Results;
		ExpectCompile(*this, Blueprint, true, Results);
		const FCNAExpander::FStats& Stats = FCNAExpander::GetStats();
		TestTrue(*FString::Printf(TEXT("Make Array of %d elements"), Count), Stats.MakeArrayNodes > 0 && Stats.MakeArrayElements == Count * Stats.MakeArrayNodes);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
