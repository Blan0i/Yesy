// Copyright (c) NodeFactory. JSON there and back: check A13 of spec 12.2.

#include "CNATestUtils.h"
#include "CNAJsonExporter.h"
#include "CNAJsonImporter.h"
#include "SCNALocalVariables.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "CNALocalization.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace CNATest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAJsonTest, "NodeFactory.Json", CNA_TEST_FLAGS)

bool FCNAJsonTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	UCustomNodeAsset* Source = MakeAsset(Scope, TEXT("CNATest_JsonSource"));
	AddPin(Source, EGPD_Input, UEdGraphSchema_K2::PC_Float, TEXT("Amount"));
	AddPin(Source, EGPD_Output, UEdGraphSchema_K2::PC_String, TEXT("Label"));
	AddPin(Source, EGPD_Input, UEdGraphSchema_K2::PC_String, TEXT("Mode"));
	SpawnCall(Source->FindExecuteGraph(), UKismetSystemLibrary::StaticClass(), TEXT("PrintString"));
	Source->DisplayName = FText::FromString(TEXT("Json Round Trip"));
	Source->bUseHeaderColor = true;
	Source->MinHeight = 200.0f; Source->CornerIconTextureSize = 42.0f;
	FCNABodyWidget& List = Source->BodyWidgets[Source->BodyWidgets.AddDefaulted()];
	List.PinName = TEXT("Mode"); List.Type = ECNABodyWidgetType::ScrollList; List.Options.Add(TEXT("Example")); List.Height = 150.0f; FCNABodyWidget& Slider = Source->BodyWidgets[Source->BodyWidgets.AddDefaulted()]; Slider.PinName = TEXT("Amount"); Slider.Type = ECNABodyWidgetType::Slider; Slider.SliderMin = -5.0f; Slider.SliderMax = 10.0f;
	Source->HeaderColor = FLinearColor(0.2f, 0.4f, 0.6f, 1.0f);
	Source->FindOrAddPinOverride(TEXT("Amount")).DisplayName = FText::FromString(TEXT("Amount, m"));
	FCNALocalVariableOps::Add(Source, TEXT("Counter"), MakeType(UEdGraphSchema_K2::PC_Int));
	Commit(Source);

	// Export, then import into a new asset: the same pins, nodes, settings and variables.
	const FString Json = FCNAJsonExporter::ExportToString(Source);
	FCNAImportData Data;
	TArray<FText> Errors;
	UCustomNodeAsset* Copy = MakeAsset(Scope, TEXT("CNATest_JsonCopy"));
	TestTrue(TEXT("Import"), FCNAJsonImporter::LoadString(Json, Data, Errors) && FCNAJsonImporter::ApplyToAsset(Copy, Data, /*bNewAsset*/ true, Errors));
	TestTrue(TEXT("Same pins"), FCNAGraphUtils::ComputeSignatureHash(Copy) == FCNAGraphUtils::ComputeSignatureHash(Source));
	TestTrue(TEXT("Same nodes"), Copy->FindExecuteGraph()->Nodes.Num() == Source->FindExecuteGraph()->Nodes.Num());
	TestTrue(TEXT("Same settings"), Copy->DisplayName.EqualTo(Source->DisplayName) && Copy->bUseHeaderColor && Copy->HeaderColor.Equals(Source->HeaderColor));
	const FCNAPinOverride* Override = Copy->FindPinOverride(TEXT("Amount"));
	TestTrue(TEXT("Height round trip"), Copy->MinHeight == 200.0f && Copy->CornerIconTextureSize == 42.0f);
	TestTrue(TEXT("List round trip"), Copy->BodyWidgets.Num() == 2 && Copy->BodyWidgets[0].Type == ECNABodyWidgetType::ScrollList && Copy->BodyWidgets[0].Height == 150.0f && Copy->BodyWidgets[1].Type == ECNABodyWidgetType::Slider && Copy->BodyWidgets[1].SliderMin == -5.0f && Copy->BodyWidgets[1].SliderMax == 10.0f);
	TestTrue(TEXT("Same override"), Override && Override->DisplayName.EqualTo(FText::FromString(TEXT("Amount, m"))));
	TestTrue(TEXT("Same variable"), Copy->LocalVariables.Num() == 1 && Copy->LocalVariables[0].Name == FName(TEXT("Counter"))
		&& Copy->LocalVariables[0].Type.PinCategory == UEdGraphSchema_K2::PC_Int);

	// A damaged file (cut text, unknown future schema) is refused and leaves the asset unchanged (spec 8.2).
	const FString Before = FCNAJsonExporter::ExportToString(Copy);
	const FString Path = FPaths::ProjectSavedDir() / TEXT("NodeFactoryTests") / TEXT("CNATest_Damaged.json");
	const FString Damaged[] = { Json.Left(Json.Len() / 2), Json.Replace(TEXT("\"schemaVersion\""), TEXT("\"schemaVersion\": 99, \"previousSchemaVersion\"")) };
	// The refused imports write errors with the asset path into the log. V-check: AddExpectedError in 4.20.3.
	AddExpectedError(Copy->GetName(), EAutomationExpectedErrorFlags::Contains, 0);
	for (const FString& Text : Damaged)
	{
		TestTrue(TEXT("Damaged file written"), FFileHelper::SaveStringToFile(Text, *Path));
		TestFalse(TEXT("Damaged file refused"), FCNAJsonImporter::ImportAndOverwrite(Copy, Path, /*bConfirm*/ false));
		TestTrue(TEXT("Asset unchanged"), FCNAJsonExporter::ExportToString(Copy).Equals(Before, ESearchCase::CaseSensitive));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
