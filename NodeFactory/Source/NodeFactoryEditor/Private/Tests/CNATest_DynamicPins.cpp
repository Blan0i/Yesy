// Copyright (c) NodeFactory. Dynamic pins and Make Array: check A7 of spec 12.2.

#include "CNATestUtils.h"
#include "CNAValidator.h"
#include "CNALocalization.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNADynamicOutputPinsTest, "NodeFactory.Pins.DynamicOutput", CNA_TEST_FLAGS)

bool FCNADynamicOutputPinsTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	// Values (array input) is returned as Results (array output); Results is a dynamic group of element outputs.
	UCustomNodeAsset* Asset = MakeAsset(Scope, TEXT("CNATest_DynamicOutput"));
	AddPin(Asset, EGPD_Input, UEdGraphSchema_K2::PC_Float, TEXT("Values"), EPinContainerType::Array);
	AddPin(Asset, EGPD_Output, UEdGraphSchema_K2::PC_Float, TEXT("Results"), EPinContainerType::Array);
	TestTrue(TEXT("Subgraph"), Link(Tunnel(Asset, EGPD_Input, TEXT("Values")), Tunnel(Asset, EGPD_Output, TEXT("Results"))));
	FCNAPinOverride& Override = Asset->FindOrAddPinOverride(TEXT("Results"));
	Override.bDynamicGroup = true;
	Override.DynamicMin = 2;
	Override.DynamicMax = 4;
	Commit(Asset);

	FCNADiagnosticList Diagnostics;
	FCNAValidator::ValidateAsset(Asset, Diagnostics);
	TestFalse(TEXT("An array output may be a dynamic group"), Diagnostics.Contains(ECNAMessage::CNA013));

	UBlueprint* Blueprint = MakeBlueprint(Scope);
	UEdGraph* Graph = nullptr;
	UEdGraphPin* Start = EventThen(Blueprint, Graph);
	UK2Node_CustomNode* Node = PlaceNode(Graph, Asset);
	const FName First = CNATypes::MakeDynamicPinName(TEXT("Results"), 0);
	const FName Second = CNATypes::MakeDynamicPinName(TEXT("Results"), 1);
	TestTrue(TEXT("Element outputs instead of the array"), Node->FindPin(First, EGPD_Output) && Node->FindPin(Second, EGPD_Output)
		&& !Node->FindPin(TEXT("Results")) && Node->GetDynamicPinCount() == 2);
	const UEdGraphPin* ValuesPin = Node->FindPin(TEXT("Values"), EGPD_Input);
	TestTrue(TEXT("The array input stays an array"), ValuesPin && ValuesPin->PinType.IsArray());

	Node->AddDynamicElement();
	TestTrue(TEXT("Add Pin adds an element output"), Node->FindPin(CNATypes::MakeDynamicPinName(TEXT("Results"), 2), EGPD_Output) != nullptr);

	// Removing an element moves the wires of the later ones down.
	UK2Node_CallFunction* Print = SpawnCall(Graph, UKismetSystemLibrary::StaticClass(), TEXT("PrintString"));
	TestTrue(TEXT("Wires"), Link(Start, Node->FindPin(UEdGraphSchema_K2::PN_Execute)) && Link(Node->FindPin(UEdGraphSchema_K2::PN_Then), Print->GetExecPin())
		&& Link(Node->FindPin(Second, EGPD_Output), Print->FindPin(TEXT("Duration"))));
	Node->RemoveDynamicElement(0);
	const UEdGraphPin* Moved = Node->FindPin(First, EGPD_Output);
	TestTrue(TEXT("The wire moved to the previous element"), Moved && Moved->LinkedTo.Contains(Print->FindPin(TEXT("Duration"))));

	// Each wired element reads the array output of the subgraph through Array Get.
	FCNAExpander::ResetStats();
	FCompilerResultsLog Results;
	ExpectCompile(*this, Blueprint, true, Results);
	TestTrue(TEXT("Array Get per wired element"), FCNAExpander::GetStats().ArrayGetNodes == 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
