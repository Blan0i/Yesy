// Copyright (c) NodeFactory. Command Validate: the asset checks of spec 10.2.

#include "CNATestUtils.h"
#include "CNAValidator.h"
#include "K2Node_GetLocal.h"
#include "SCNALocalVariables.h"
#include "CNALocalization.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace CNATest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAValidationTest, "NodeFactory.Validation", CNA_TEST_FLAGS)

bool FCNAValidationTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	UCustomNodeAsset* Asset = MakeAsset(Scope, TEXT("CNATest_Validation"));
	AddPin(Asset, EGPD_Input, UEdGraphSchema_K2::PC_Float, TEXT("Value"));
	AddPin(Asset, EGPD_Output, UEdGraphSchema_K2::PC_Float, TEXT("Result"));
	Commit(Asset);
	auto Reports = [Asset](ECNAMessage Code)
	{
		FCNADiagnosticList Diagnostics;
		FCNAValidator::ValidateAsset(Asset, Diagnostics);
		return Diagnostics.Contains(Code);
	};
	FCNADiagnosticList Initial;
	TestTrue(TEXT("A new asset has no errors"), FCNAValidator::ValidateAsset(Asset, Initial));

	Asset->FindOrAddPinOverride(TEXT("Missing")).DisplayName = FText::FromString(TEXT("Missing"));
	TestTrue(TEXT("CNA014: override of a pin the macro lacks"), Reports(ECNAMessage::CNA014));
	Asset->PinOverrides.Reset();
	Asset->FindOrAddPinOverride(TEXT("Result")).PassThroughOf = TEXT("Nowhere");
	TestTrue(TEXT("CNA015: PassThroughOf without a target"), Reports(ECNAMessage::CNA015));
	Asset->PinOverrides.Reset();
	Asset->Icon = TEXT("NodeFactoryTests.NoSuchBrush");
	TestTrue(TEXT("CNA019: unknown icon brush"), Reports(ECNAMessage::CNA019));
	Asset->Icon = NAME_None;

	// CNA017: Get Local of a removed variable; removing a variable together with its nodes leaves nothing behind.
	UEdGraph* Execute = Asset->FindExecuteGraph();
	UK2Node_GetLocal* GetNodes[2] = { nullptr, nullptr };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FGuid Guid = FCNALocalVariableOps::Add(Asset, Index == 0 ? TEXT("Counter") : TEXT("Total"), MakeType(UEdGraphSchema_K2::PC_Int));
		const FCNALocalVariable* Variable = Asset->FindLocalVariable(Guid);
		GetNodes[Index] = Variable ? Spawn<UK2Node_GetLocal>(Execute, [Variable](UK2Node_GetLocal* Node) { Node->InitVariable(*Variable); }) : nullptr;
		TestTrue(TEXT("Get Local counted"), GetNodes[Index] && FCNALocalVariableOps::CountUses(Asset, Guid) == 1);
		FCNALocalVariableOps::Remove(Asset, Guid, /*bRemoveNodes*/ Index == 1);
	}
	TestTrue(TEXT("CNA017: Get Local of a removed variable"), Reports(ECNAMessage::CNA017));
	// A node that only knows the name (as after a paste) finds a new variable of that name.
	FCNALocalVariableOps::Add(Asset, TEXT("Counter"), MakeType(UEdGraphSchema_K2::PC_Int));
	TestTrue(TEXT("Variable removed with its nodes"), !Execute->Nodes.Contains(GetNodes[1]) && !Reports(ECNAMessage::CNA017));

	// CNA018 and CNA004: a pure node keeps no state (the variable above) and has no exec pins.
	Asset->bPure = true;
	TestTrue(TEXT("CNA018 and CNA004 for a pure node"), Reports(ECNAMessage::CNA018) && Reports(ECNAMessage::CNA004));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
