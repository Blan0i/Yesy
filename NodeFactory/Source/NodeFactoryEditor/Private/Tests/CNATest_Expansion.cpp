// Copyright (c) NodeFactory. Expansion of the subgraph: checks A1-A4 of spec 12.2.

#include "CNATestUtils.h"
#include "CNAEditorSettings.h"
#include "CNAValidator.h"
#include "K2Node_GetCaller.h"
#include "K2Node_IfThenElse.h"
#include "CNALocalization.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace CNATest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAExpansionTest, "NodeFactory.Expansion", CNA_TEST_FLAGS)

bool FCNAExpansionTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	const FName Execute = UEdGraphSchema_K2::PN_Execute;
	const FName Then = UEdGraphSchema_K2::PN_Then;
	UEdGraph* Graph = nullptr;

	// A1: pure, Sum = (A + B) + C, used in a Function Graph.
	UCustomNodeAsset* Sum = MakeAsset(Scope, TEXT("CNATest_Sum3"), /*bPure*/ true);
	for (const TCHAR* Name : { TEXT("A"), TEXT("B"), TEXT("C") })
	{
		AddPin(Sum, EGPD_Input, UEdGraphSchema_K2::PC_Float, Name);
	}
	AddPin(Sum, EGPD_Output, UEdGraphSchema_K2::PC_Float, TEXT("Sum"));
	UK2Node_CallFunction* AddAB = SpawnCall(Sum->FindExecuteGraph(), UKismetMathLibrary::StaticClass(), TEXT("Add_FloatFloat"));
	UK2Node_CallFunction* AddC = SpawnCall(Sum->FindExecuteGraph(), UKismetMathLibrary::StaticClass(), TEXT("Add_FloatFloat"));
	TestTrue(TEXT("A1 subgraph"), Link(Tunnel(Sum, EGPD_Input, TEXT("A")), AddAB->FindPin(TEXT("A"))) && Link(Tunnel(Sum, EGPD_Input, TEXT("B")), AddAB->FindPin(TEXT("B")))
		&& Link(AddAB->GetReturnValuePin(), AddC->FindPin(TEXT("A"))) && Link(Tunnel(Sum, EGPD_Input, TEXT("C")), AddC->FindPin(TEXT("B")))
		&& Link(AddC->GetReturnValuePin(), Tunnel(Sum, EGPD_Output, TEXT("Sum"))));
	Commit(Sum);

	UBlueprint* PureBlueprint = MakeBlueprint(Scope);
	UEdGraphPin* FunctionStart = FunctionThen(PureBlueprint, Graph);
	UK2Node_CustomNode* SumNode = PlaceNode(Graph, Sum);
	UK2Node_CallFunction* Print = SpawnCall(Graph, UKismetSystemLibrary::StaticClass(), TEXT("PrintString"));
	TestTrue(TEXT("A1 instance"), SumNode->IsNodePure() && Link(FunctionStart, Print->GetExecPin()) && Link(SumNode->FindPin(TEXT("Sum")), Print->FindPin(TEXT("Duration"))));
	FCNAExpander::ResetStats();
	FCompilerResultsLog PureResults;
	ExpectCompile(*this, PureBlueprint, true, PureResults);
	TestTrue(TEXT("A1 expanded"), FCNAExpander::GetStats().ExpandedInstances > 0);

	// A2: one exec input, two exec outputs (Branch), in the Event Graph and in a Function Graph.
	UCustomNodeAsset* Branch = MakeAsset(Scope, TEXT("CNATest_Branch"));
	AddPin(Branch, EGPD_Input, UEdGraphSchema_K2::PC_Boolean, TEXT("Condition"));
	AddPin(Branch, EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("Else"));
	UK2Node_IfThenElse* IfNode = Spawn<UK2Node_IfThenElse>(Branch->FindExecuteGraph());
	Tunnel(Branch, EGPD_Input, Execute)->BreakAllPinLinks();
	TestTrue(TEXT("A2 subgraph"), Link(Tunnel(Branch, EGPD_Input, Execute), IfNode->GetExecPin()) && Link(Tunnel(Branch, EGPD_Input, TEXT("Condition")), IfNode->GetConditionPin())
		&& Link(IfNode->GetThenPin(), Tunnel(Branch, EGPD_Output, Then)) && Link(IfNode->GetElsePin(), Tunnel(Branch, EGPD_Output, TEXT("Else"))));
	Commit(Branch);

	UBlueprint* ImpureBlueprint = MakeBlueprint(Scope);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		UEdGraphPin* Start = Pass == 0 ? EventThen(ImpureBlueprint, Graph) : FunctionThen(ImpureBlueprint, Graph);
		UK2Node_CustomNode* Node = PlaceNode(Graph, Branch);
		TestTrue(TEXT("A2 instance"), Node->FindPin(TEXT("Else")) && Link(Start, Node->FindPin(Execute)));
	}
	FCNAExpander::ResetStats();
	FCompilerResultsLog ImpureResults;
	ExpectCompile(*this, ImpureBlueprint, true, ImpureResults);
	TestTrue(TEXT("A2 both instances expanded"), FCNAExpander::GetStats().ExpandedInstances >= 2);

	// A3: Delay works in the Event Graph; a Function Graph gives CNA003. Get Caller is the world context.
	UCustomNodeAsset* Wait = MakeAsset(Scope, TEXT("CNATest_Wait"));
	UK2Node_CallFunction* Delay = SpawnCall(Wait->FindExecuteGraph(), UKismetSystemLibrary::StaticClass(), TEXT("Delay"));
	UK2Node_GetCaller* Caller = Spawn<UK2Node_GetCaller>(Wait->FindExecuteGraph());
	Tunnel(Wait, EGPD_Input, Execute)->BreakAllPinLinks();
	TestTrue(TEXT("A3 subgraph"), Link(Tunnel(Wait, EGPD_Input, Execute), Delay->GetExecPin()) && Link(Delay->GetThenPin(), Tunnel(Wait, EGPD_Output, Then))
		&& Link(Caller->GetCallerPin(), Delay->FindPin(TEXT("WorldContextObject"))));
	Commit(Wait);
	TestTrue(TEXT("A3 latent found"), FCNAGraphUtils::FindLatentNodes(Wait, nullptr));

	UBlueprint* LatentBlueprints[2] = { MakeBlueprint(Scope), MakeBlueprint(Scope) };
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		UEdGraphPin* Start = Pass == 0 ? EventThen(LatentBlueprints[Pass], Graph) : FunctionThen(LatentBlueprints[Pass], Graph);
		Link(Start, PlaceNode(Graph, Wait)->FindPin(Execute));
		FCompilerResultsLog Results;
		ExpectCompile(*this, LatentBlueprints[Pass], Pass == 0, Results);
		TestTrue(TEXT("A3 CNA003 only in the Function Graph"), HasCode(Results, TEXT("CNA003")) == (Pass == 1));
	}

	// A4: A contains B and B contains A gives CNA001 without hanging; nesting beyond the limit gives CNA002.
	UCustomNodeAsset* CycleA = MakeAsset(Scope, TEXT("CNATest_CycleA"));
	UCustomNodeAsset* CycleB = MakeAsset(Scope, TEXT("CNATest_CycleB"));
	PlaceNode(CycleA->FindExecuteGraph(), CycleB);
	PlaceNode(CycleB->FindExecuteGraph(), CycleA);
	FCNADiagnosticList CycleDiagnostics;
	FCNAValidator::ValidateAsset(CycleA, CycleDiagnostics);
	TestTrue(TEXT("A4 CNA001 from the validator"), CycleDiagnostics.Contains(ECNAMessage::CNA001));
	UBlueprint* CycleBlueprint = MakeBlueprint(Scope);
	Link(EventThen(CycleBlueprint, Graph), PlaceNode(Graph, CycleA)->FindPin(Execute));
	FCompilerResultsLog CycleResults;
	ExpectCompile(*this, CycleBlueprint, false, CycleResults);
	TestTrue(TEXT("A4 CNA001 from the compiler"), HasCode(CycleResults, TEXT("CNA001")));

	const int32 Limit = FMath::Max(1, GetDefault<UCNAEditorSettings>()->MaxExpansionDepth);
	TArray<UCustomNodeAsset*> Chain;
	for (int32 Index = 0; Index <= Limit; ++Index)
	{
		Chain.Add(MakeAsset(Scope, TEXT("CNATest_Depth")));
		if (Index > 0)
		{
			PlaceNode(Chain[Index - 1]->FindExecuteGraph(), Chain[Index]);
		}
	}
	FCNADiagnosticList TooDeep;
	FCNADiagnosticList AtLimit;
	FCNAValidator::ValidateAsset(Chain[0], TooDeep);
	FCNAValidator::ValidateAsset(Chain[1], AtLimit);
	TestTrue(TEXT("A4 CNA002 beyond the limit"), TooDeep.Contains(ECNAMessage::CNA002) && !AtLimit.Contains(ECNAMessage::CNA002));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
