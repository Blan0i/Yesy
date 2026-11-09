// Copyright (c) NodeFactory. Curve widget: pins of the instance, Curve Value expansion, playback and validation.

#include "CNATestUtils.h"
#include "CNACurveUtils.h"
#include "CNAValidator.h"
#include "K2Node_CNACurveValue.h"
#include "Curves/CurveVector.h"
#include "CNALocalization.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace CNATest;

namespace CNACurveTest
{
	/** Curve widget "Name" with an own curve from 0 (time 0) to 10 (time 1) on every channel. Returns its Guid. */
	FGuid AddCurve(UCustomNodeAsset* Asset, const TCHAR* Name, ECNACurveMode Mode, ECNACurveValueType ValueType = ECNACurveValueType::Float)
	{
		const int32 Index = Asset->BodyWidgets.AddDefaulted();
		FCNABodyWidget& Widget = Asset->BodyWidgets[Index];
		Widget.Type = ECNABodyWidgetType::Curve;
		Widget.PinName = FName(Name);
		Widget.WidgetGuid = FGuid::NewGuid();
		Widget.Curve.Mode = Mode;
		Widget.Curve.ValueType = ValueType;
		FRuntimeFloatCurve* Channels[] = { &Widget.Curve.CurveX, &Widget.Curve.CurveY, &Widget.Curve.CurveZ, &Widget.Curve.CurveW };
		for (FRuntimeFloatCurve* Channel : Channels)
		{
			Channel->EditorCurveData.AddKey(0.0f, 0.0f);
			Channel->EditorCurveData.AddKey(1.0f, 10.0f);
		}
		return Widget.WidgetGuid;
	}

	/** Curve Value node bound to the curve at Index, placed in the macro Execute. */
	UK2Node_CNACurveValue* PlaceCurveValue(UCustomNodeAsset* Asset, int32 Index)
	{
		const FCNABodyWidget Widget = Asset->BodyWidgets[Index];
		return Spawn<UK2Node_CNACurveValue>(Asset->FindExecuteGraph(), [Widget](UK2Node_CNACurveValue* Node) { Node->InitCurve(Widget); });
	}

	bool IsExecPinOf(const UEdGraphPin* Pin, EEdGraphPinDirection Direction)
	{
		return Pin && Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNACurvePinsTest, "NodeFactory.Curves.Pins", CNA_TEST_FLAGS)

bool FCNACurvePinsTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	const FName Execute = UEdGraphSchema_K2::PN_Execute;
	const FName Then = UEdGraphSchema_K2::PN_Then;

	UCustomNodeAsset* Asset = MakeAsset(Scope, TEXT("CNATest_CurvePins"));
	const FGuid Guid = CNACurveTest::AddCurve(Asset, TEXT("Fade"), ECNACurveMode::EvaluateAtTime);
	Commit(Asset);

	UBlueprint* Blueprint = MakeBlueprint(Scope);
	UEdGraph* Graph = nullptr;
	UEdGraphPin* Start = EventThen(Blueprint, Graph);
	UK2Node_CustomNode* Node = PlaceNode(Graph, Asset);

	// EvaluateAtTime: one float input with the stable name, labelled with the role.
	const FName TimeName = CNATypes::MakeCurvePinName(Guid, ECNACurvePinRole::Time);
	UEdGraphPin* TimePin = Node->FindPin(TimeName, EGPD_Input);
	TestTrue(TEXT("Time pin"), TimePin && TimePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Float && TimePin->PinFriendlyName.ToString() == TEXT("Time"));
	TestTrue(TEXT("Exec flow of the macro stays"), Node->FindPin(Execute, EGPD_Input) && Node->FindPin(Then, EGPD_Output));
	UK2Node_CallFunction* TimeSource = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("Add_FloatFloat"));
	TestTrue(TEXT("Wires"), Link(TimeSource->GetReturnValuePin(), TimePin) && Link(Start, Node->FindPin(Execute, EGPD_Input)));

	// Renaming the label changes only the display name: same pin, same wire.
	Asset->BodyWidgets[0].Curve.TimeLabel = FText::FromString(TEXT("Alpha"));
	Commit(Asset);
	Node->ReconstructNode();
	TimePin = Node->FindPin(TimeName, EGPD_Input);
	TestTrue(TEXT("Label rename keeps the pin and its wire"), TimePin && TimePin->PinFriendlyName.ToString() == TEXT("Alpha")
		&& TimePin->LinkedTo.Contains(TimeSource->GetReturnValuePin()) && Node->GetLastLostWirePins().Num() == 0);

	// Playback: Play, Play from Start, Stop, Reverse in; Update, Finished out. The exec flow of the macro is driven
	// by the loop, so its pins are gone and their wires moved to Play and Update.
	Asset->BodyWidgets[0].Curve.Mode = ECNACurveMode::Playback;
	Commit(Asset);
	Node->ReconstructNode();
	const ECNACurvePinRole Inputs[] = { ECNACurvePinRole::Play, ECNACurvePinRole::PlayFromStart, ECNACurvePinRole::Stop, ECNACurvePinRole::Reverse };
	for (ECNACurvePinRole Role : Inputs)
	{
		TestTrue(*FString::Printf(TEXT("Playback input %d"), static_cast<int32>(Role)), CNACurveTest::IsExecPinOf(Node->FindCurvePin(Guid, Role), EGPD_Input));
	}
	TestTrue(TEXT("Update and Finished"), CNACurveTest::IsExecPinOf(Node->FindCurvePin(Guid, ECNACurvePinRole::Update), EGPD_Output)
		&& CNACurveTest::IsExecPinOf(Node->FindCurvePin(Guid, ECNACurvePinRole::Finished), EGPD_Output));
	TestTrue(TEXT("Playback has no Time pin and no exec flow of the macro"), !Node->FindPin(TimeName) && !Node->FindPin(Execute, EGPD_Input) && !Node->FindPin(Then, EGPD_Output));
	const UEdGraphPin* PlayPin = Node->FindCurvePin(Guid, ECNACurvePinRole::Play);
	TestTrue(TEXT("The wire of Execute moved to Play"), PlayPin && PlayPin->LinkedTo.Contains(Start));
	TestTrue(TEXT("Play is labelled with its role"), PlayPin && PlayPin->PinFriendlyName.ToString() == TEXT("Play"));

	// Optional roles follow their toggles.
	Asset->BodyWidgets[0].Curve.bShowReverse = false;
	Asset->BodyWidgets[0].Curve.bShowPlayFromStart = false;
	Commit(Asset);
	Node->ReconstructNode();
	TestTrue(TEXT("Reverse and Play from Start hidden by their toggles"), !Node->FindCurvePin(Guid, ECNACurvePinRole::Reverse)
		&& !Node->FindCurvePin(Guid, ECNACurvePinRole::PlayFromStart) && Node->FindCurvePin(Guid, ECNACurvePinRole::Stop));

	// Back to EvaluateAtTime: the wire of Play returns to Execute.
	Asset->BodyWidgets[0].Curve.Mode = ECNACurveMode::EvaluateAtTime;
	Commit(Asset);
	Node->ReconstructNode();
	const UEdGraphPin* ExecutePin = Node->FindPin(Execute, EGPD_Input);
	TestTrue(TEXT("The wire of Play returned to Execute"), ExecutePin && ExecutePin->LinkedTo.Contains(Start) && !Node->FindCurvePin(Guid, ECNACurvePinRole::Play));

	// A pure node gets the Time pin but no exec pins of a Playback curve.
	UCustomNodeAsset* PureAsset = MakeAsset(Scope, TEXT("CNATest_CurvePure"), /*bPure*/ true);
	const FGuid PureGuid = CNACurveTest::AddCurve(PureAsset, TEXT("Wave"), ECNACurveMode::Playback);
	Commit(PureAsset);
	UK2Node_CustomNode* PureNode = PlaceNode(Graph, PureAsset);
	TestTrue(TEXT("Pure node without playback pins"), !PureNode->FindCurvePin(PureGuid, ECNACurvePinRole::Play) && !PureNode->FindCurvePin(PureGuid, ECNACurvePinRole::Update));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNACurveExpansionTest, "NodeFactory.Curves.Expansion", CNA_TEST_FLAGS)

bool FCNACurveExpansionTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	const FName Execute = UEdGraphSchema_K2::PN_Execute;
	const FName Then = UEdGraphSchema_K2::PN_Then;
	UEdGraph* Graph = nullptr;

	// Own curve, EvaluateAtTime: Curve Value -> output Out. A wired Curve Value node that is not replaced fails the
	// compile in its own ExpandNode, so a successful compile means only engine nodes remain.
	UCustomNodeAsset* Asset = MakeAsset(Scope, TEXT("CNATest_CurveBake"));
	const FGuid Guid = CNACurveTest::AddCurve(Asset, TEXT("Fade"), ECNACurveMode::EvaluateAtTime);
	AddPin(Asset, EGPD_Output, UEdGraphSchema_K2::PC_Float, TEXT("Out"));
	UK2Node_CNACurveValue* CurveValue = CNACurveTest::PlaceCurveValue(Asset, 0);
	TestTrue(TEXT("Curve Value bound"), CurveValue->CurveGuid == Guid && CurveValue->GetValuePin() && CurveValue->GetTimePin());
	TestTrue(TEXT("Curve Value wired"), Link(CurveValue->GetValuePin(), Tunnel(Asset, EGPD_Output, TEXT("Out"))));
	Commit(Asset);
	FCNADiagnosticList AssetDiagnostics;
	FCNAValidator::ValidateAsset(Asset, AssetDiagnostics);
	TestFalse(TEXT("Curve asset is valid"), AssetDiagnostics.HasErrors());

	UBlueprint* Blueprint = MakeBlueprint(Scope);
	UEdGraphPin* Start = EventThen(Blueprint, Graph);
	UK2Node_CustomNode* Node = PlaceNode(Graph, Asset);
	UK2Node_CallFunction* TimeSource = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("Add_FloatFloat"));
	UK2Node_CallFunction* Print = SpawnCall(Graph, UKismetSystemLibrary::StaticClass(), TEXT("PrintString"));
	TestTrue(TEXT("Instance wired"), Link(Start, Node->FindPin(Execute)) && Link(Node->FindPin(Then), Print->GetExecPin())
		&& Link(Node->FindPin(TEXT("Out")), Print->FindPin(TEXT("Duration")))
		&& Link(TimeSource->GetReturnValuePin(), Node->FindCurvePin(Guid, ECNACurvePinRole::Time)));
	FCNAExpander::ResetStats();
	FCompilerResultsLog BakeResults;
	ExpectCompile(*this, Blueprint, true, BakeResults);
	const FCNAExpander::FStats BakeStats = FCNAExpander::GetStats();
	TestTrue(TEXT("Baked into Make Array, Array Get and Lerp"), BakeStats.CurveValueNodes == 1 && BakeStats.BakedChannels == 1
		&& BakeStats.ArrayGetNodes == 2 && BakeStats.CurveAssetCalls == 0);

	// Curve asset, Vector: the getter of the asset with the asset as an object literal.
	UCurveVector* VectorCurve = Scope.Keep(NewObject<UCurveVector>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UCurveVector::StaticClass(), TEXT("CNATest_VectorCurve"))));
	for (FRichCurve& Channel : VectorCurve->FloatCurves)
	{
		Channel.AddKey(0.0f, 0.0f);
		Channel.AddKey(2.0f, 1.0f);
	}
	UCustomNodeAsset* VectorAsset = MakeAsset(Scope, TEXT("CNATest_CurveAsset"));
	const FGuid VectorGuid = CNACurveTest::AddCurve(VectorAsset, TEXT("Offset"), ECNACurveMode::EvaluateAtTime, ECNACurveValueType::Vector);
	VectorAsset->BodyWidgets[0].Curve.bUseCurveAsset = true;
	VectorAsset->BodyWidgets[0].Curve.CurveAsset = VectorCurve;
	AddPin(VectorAsset, EGPD_Output, UEdGraphSchema_K2::PC_Struct, TEXT("Offset"), EPinContainerType::None, TBaseStructure<FVector>::Get());
	UK2Node_CNACurveValue* VectorValue = CNACurveTest::PlaceCurveValue(VectorAsset, 0);
	TestTrue(TEXT("Vector Curve Value"), Link(VectorValue->GetValuePin(), Tunnel(VectorAsset, EGPD_Output, TEXT("Offset"))));
	Commit(VectorAsset);

	UBlueprint* VectorBlueprint = MakeBlueprint(Scope);
	UEdGraphPin* VectorStart = EventThen(VectorBlueprint, Graph);
	UK2Node_CustomNode* VectorNode = PlaceNode(Graph, VectorAsset);
	UK2Node_CallFunction* VectorLength = SpawnCall(Graph, UKismetMathLibrary::StaticClass(), TEXT("VSize"));
	UK2Node_CallFunction* VectorPrint = SpawnCall(Graph, UKismetSystemLibrary::StaticClass(), TEXT("PrintString"));
	TestTrue(TEXT("Vector instance wired"), Link(VectorStart, VectorNode->FindPin(Execute)) && Link(VectorNode->FindPin(Then), VectorPrint->GetExecPin())
		&& Link(VectorNode->FindPin(TEXT("Offset"), EGPD_Output), VectorLength->FindPin(TEXT("A")))
		&& Link(VectorLength->GetReturnValuePin(), VectorPrint->FindPin(TEXT("Duration"))));
	TestTrue(TEXT("Time pin of the vector curve"), VectorNode->FindCurvePin(VectorGuid, ECNACurvePinRole::Time) != nullptr);
	FCNAExpander::ResetStats();
	FCompilerResultsLog AssetResults;
	ExpectCompile(*this, VectorBlueprint, true, AssetResults);
	TestTrue(TEXT("Curve asset read by its getter"), FCNAExpander::GetStats().CurveAssetCalls == 1);

	// Playback: Event Graph compiles with one loop; a Function Graph is refused (CNA020).
	UCustomNodeAsset* PlayAsset = MakeAsset(Scope, TEXT("CNATest_CurvePlay"));
	const FGuid PlayGuid = CNACurveTest::AddCurve(PlayAsset, TEXT("Wave"), ECNACurveMode::Playback);
	UK2Node_CNACurveValue* PlayValue = CNACurveTest::PlaceCurveValue(PlayAsset, 0);
	UK2Node_CallFunction* InnerPrint = SpawnCall(PlayAsset->FindExecuteGraph(), UKismetSystemLibrary::StaticClass(), TEXT("PrintString"));
	Tunnel(PlayAsset, EGPD_Input, Execute)->BreakAllPinLinks();
	TestTrue(TEXT("Playback subgraph"), Link(Tunnel(PlayAsset, EGPD_Input, Execute), InnerPrint->GetExecPin())
		&& Link(InnerPrint->GetThenPin(), Tunnel(PlayAsset, EGPD_Output, Then))
		&& Link(PlayValue->GetValuePin(), InnerPrint->FindPin(TEXT("Duration"))));
	Commit(PlayAsset);

	UBlueprint* PlayBlueprints[2] = { MakeBlueprint(Scope), MakeBlueprint(Scope) };
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		UEdGraphPin* PassStart = Pass == 0 ? EventThen(PlayBlueprints[Pass], Graph) : FunctionThen(PlayBlueprints[Pass], Graph);
		UK2Node_CustomNode* PlayNode = PlaceNode(Graph, PlayAsset);
		UK2Node_CallFunction* UpdatePrint = SpawnCall(Graph, UKismetSystemLibrary::StaticClass(), TEXT("PrintString"));
		TestTrue(TEXT("Playback instance wired"), Link(PassStart, PlayNode->FindCurvePin(PlayGuid, ECNACurvePinRole::Play))
			&& Link(PlayNode->FindCurvePin(PlayGuid, ECNACurvePinRole::Update), UpdatePrint->GetExecPin()));
		FCNAExpander::ResetStats();
		FCompilerResultsLog Results;
		ExpectCompile(*this, PlayBlueprints[Pass], Pass == 0, Results);
		TestTrue(TEXT("CNA020 only in the Function Graph"), HasCode(Results, TEXT("CNA020")) == (Pass == 1));
		if (Pass == 0)
		{
			TestTrue(TEXT("One playback loop"), FCNAExpander::GetStats().PlaybackLoops == 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNACurveValidationTest, "NodeFactory.Curves.Validation", CNA_TEST_FLAGS)

bool FCNACurveValidationTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	auto Validate = [](const UCustomNodeAsset* Asset)
	{
		FCNADiagnosticList Diagnostics;
		FCNAValidator::ValidateAsset(Asset, Diagnostics);
		return Diagnostics;
	};

	// CNA023: Curve Value of a curve that does not exist.
	UCustomNodeAsset* Missing = MakeAsset(Scope, TEXT("CNATest_CurveMissing"));
	CNACurveTest::AddCurve(Missing, TEXT("Fade"), ECNACurveMode::EvaluateAtTime);
	UK2Node_CNACurveValue* Orphan = CNACurveTest::PlaceCurveValue(Missing, 0);
	Orphan->CurveGuid = FGuid::NewGuid();
	Orphan->CurveName = TEXT("Gone");
	TestTrue(TEXT("CNA023: missing curve"), Validate(Missing).Contains(ECNAMessage::CNA023));

	// CNA022: Curve asset missing or of the wrong class; duplicated names are a warning.
	UCustomNodeAsset* Settings = MakeAsset(Scope, TEXT("CNATest_CurveSettings"));
	CNACurveTest::AddCurve(Settings, TEXT("Fade"), ECNACurveMode::EvaluateAtTime);
	Settings->BodyWidgets[0].Curve.bUseCurveAsset = true;
	FCNADiagnosticList NoAsset = Validate(Settings);
	TestTrue(TEXT("CNA022: no Curve asset"), NoAsset.Contains(ECNAMessage::CNA022) && NoAsset.HasErrors());
	UCurveVector* VectorCurve = Scope.Keep(NewObject<UCurveVector>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UCurveVector::StaticClass(), TEXT("CNATest_WrongCurve"))));
	Settings->BodyWidgets[0].Curve.CurveAsset = VectorCurve;
	TestTrue(TEXT("CNA022: Curve Vector for a Float curve"), Validate(Settings).HasErrors());
	Settings->BodyWidgets[0].Curve.ValueType = ECNACurveValueType::Vector;
	TestFalse(TEXT("Curve Vector for a Vector curve"), Validate(Settings).HasErrors());
	CNACurveTest::AddCurve(Settings, TEXT("Fade"), ECNACurveMode::EvaluateAtTime);
	FCNADiagnosticList Twice = Validate(Settings);
	TestTrue(TEXT("CNA022 warning: same name twice"), Twice.Contains(ECNAMessage::CNA022) && !Twice.HasErrors());

	// CNA021: two Playback curves. CNA020: Playback on a pure node.
	UCustomNodeAsset* TwoPlayback = MakeAsset(Scope, TEXT("CNATest_CurveTwoPlayback"));
	CNACurveTest::AddCurve(TwoPlayback, TEXT("A"), ECNACurveMode::Playback);
	CNACurveTest::AddCurve(TwoPlayback, TEXT("B"), ECNACurveMode::Playback);
	TestTrue(TEXT("CNA021: two Playback curves"), Validate(TwoPlayback).Contains(ECNAMessage::CNA021));
	UCustomNodeAsset* PurePlayback = MakeAsset(Scope, TEXT("CNATest_CurvePurePlayback"), /*bPure*/ true);
	CNACurveTest::AddCurve(PurePlayback, TEXT("A"), ECNACurveMode::Playback);
	TestTrue(TEXT("CNA020: Playback on a pure node"), Validate(PurePlayback).Contains(ECNAMessage::CNA020));

	// Pin names survive a label change and parse back to the widget and role.
	const FGuid Guid = Settings->BodyWidgets[0].WidgetGuid;
	FGuid ParsedGuid;
	ECNACurvePinRole ParsedRole = ECNACurvePinRole::Time;
	TestTrue(TEXT("Stable curve pin name"), CNATypes::ParseCurvePinName(CNATypes::MakeCurvePinName(Guid, ECNACurvePinRole::Finished), ParsedGuid, ParsedRole)
		&& ParsedGuid == Guid && ParsedRole == ECNACurvePinRole::Finished);

	// Assets without curves keep their signature hash.
	UCustomNodeAsset* Plain = MakeAsset(Scope, TEXT("CNATest_CurveNone"));
	TestTrue(TEXT("No curve text in the hash of an asset without curves"), FCNACurveUtils::GetPinSignatureText(Plain).IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
