// Copyright (c) NodeFactory. Update of the instances: checks A5 and A6 of spec 12.2.

#include "CNATestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace CNATest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAUpdateTest, "NodeFactory.Update", CNA_TEST_FLAGS)

bool FCNAUpdateTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	UCustomNodeAsset* Asset = MakeAsset(Scope, TEXT("CNATest_Update"));
	AddPin(Asset, EGPD_Input, UEdGraphSchema_K2::PC_Float, TEXT("Value"));
	Commit(Asset);
	const TArray<UCustomNodeAsset*> Assets = { Asset };

	UBlueprint* Blueprint = MakeBlueprint(Scope);
	UEdGraph* Graph = nullptr;
	UEdGraphPin* Start = EventThen(Blueprint, Graph);
	UK2Node_CustomNode* Node = PlaceNode(Graph, Asset);
	UEdGraphPin* Source = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("RandomFloat"))->GetReturnValuePin();
	TestTrue(TEXT("Wired"), Link(Start, Node->FindPin(UEdGraphSchema_K2::PN_Execute)) && Link(Source, Node->FindPin(TEXT("Value"))));
	auto IsLinked = [Node, Source](const TCHAR* PinName)
	{
		const UEdGraphPin* Pin = Node->FindPin(PinName);
		return Pin && Pin->LinkedTo.Contains(Source);
	};

	// A5: a new pin of the macro reaches the instance and the wires stay.
	AddPin(Asset, EGPD_Input, UEdGraphSchema_K2::PC_Int, TEXT("Count"));
	Commit(Asset);
	TestTrue(TEXT("A5 outdated"), Node->IsOutdated());
	FCNAUpdateReport Report;
	FCNAUpdateService::UpdateBlueprint(Blueprint, Assets, /*bOnlyOutdated*/ true, Report);
	TestTrue(TEXT("A5 updated"), Report.NumNodes == 1 && !Node->IsOutdated() && Node->FindPin(TEXT("Count")) && Report.LostWires.Num() == 0);
	TestTrue(TEXT("A5 wires kept"), IsLinked(TEXT("Value")) && Node->FindPin(UEdGraphSchema_K2::PN_Execute)->LinkedTo.Contains(Start));

	FCNAPinOverride& BeforeRename = Asset->FindOrAddPinOverride(TEXT("Value")); BeforeRename.DisplayName = FText::FromString(TEXT("Original"));
	FCNABodyWidget& BeforeWidget = Asset->BodyWidgets[Asset->BodyWidgets.AddDefaulted()]; BeforeWidget.PinName = TEXT("Value"); BeforeWidget.Type = ECNABodyWidgetType::Slider;
	// A6: a rename recorded in PinRedirects keeps the wire.
	FText Error;
	TestTrue(TEXT("A6 rename"), FCNAPinManagerOps::RenamePin(Asset, EGPD_Input, TEXT("Value"), TEXT("Amount"), Error));
	TestTrue(TEXT("A6 persistent name"), FCNAPinManagerOps::FindUserPin(Asset, EGPD_Input, TEXT("Amount")).IsValid() && FCNAPinManagerOps::FindTunnelPin(Asset, EGPD_Input, TEXT("Amount")) != nullptr);
	TestTrue(TEXT("A6 references"), Asset->FindPinOverride(TEXT("Amount")) != nullptr && Asset->FindPinOverride(TEXT("Value")) == nullptr && Asset->BodyWidgets.Num() == 1 && Asset->BodyWidgets[0].PinName == FName(TEXT("Amount")));
	TestTrue(TEXT("A6 redirect"), Asset->ResolvePinRedirect(TEXT("Value")) == FName(TEXT("Amount")));
	Commit(Asset);
	TestTrue(TEXT("A6 name after compile"), FCNAPinManagerOps::FindUserPin(Asset, EGPD_Input, TEXT("Amount")).IsValid() && FCNAPinManagerOps::FindTunnelPin(Asset, EGPD_Input, TEXT("Amount")) != nullptr);
	FCNAUpdateService::UpdateBlueprint(Blueprint, Assets, true, Report);
	TestTrue(TEXT("A6 wire kept"), IsLinked(TEXT("Amount")) && Report.LostWires.Num() == 0);

	// Without a redirect and with another type nothing matches: CNA016 on the node and in the report.
	TestTrue(TEXT("A6 rename and retype"), FCNAPinManagerOps::RenamePin(Asset, EGPD_Input, TEXT("Amount"), TEXT("Label"), Error)
		&& FCNAPinManagerOps::ChangePinType(Asset, EGPD_Input, TEXT("Label"), MakeType(UEdGraphSchema_K2::PC_String), Error));
	Asset->PinRedirects.Reset();
	Commit(Asset);
	FCNAUpdateReport LossReport;
	FCNAUpdateService::UpdateBlueprint(Blueprint, Assets, true, LossReport);
	TestTrue(TEXT("A6 lost wire reported"), LossReport.LostWires.Num() == 1 && Node->GetLastLostWirePins().Num() == 1 && !IsLinked(TEXT("Label")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
