// Copyright (c) NodeFactory. Undo, Redo, copy and paste: check A9 of spec 12.2.

#include "CNATestUtils.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "ScopedTransaction.h"
#include "CNALocalization.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace CNATest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCNAUndoTest, "NodeFactory.Undo", CNA_TEST_FLAGS)

bool FCNAUndoTest::RunTest(const FString& Parameters)
{
	FScope Scope;
	UCustomNodeAsset* Asset = MakeAsset(Scope, TEXT("CNATest_Undo"));
	AddPin(Asset, EGPD_Input, UEdGraphSchema_K2::PC_Float, TEXT("Items"), EPinContainerType::Array);
	AddPin(Asset, EGPD_Input, UEdGraphSchema_K2::PC_Struct, TEXT("Location"), EPinContainerType::None, TBaseStructure<FVector>::Get());
	FCNAPinOverride& Override = Asset->FindOrAddPinOverride(TEXT("Items"));
	Override.bDynamicGroup = true;
	Override.DynamicMin = 1;
	Override.DynamicMax = 8;
	Commit(Asset);

	UEdGraph* Graph = MakeBlueprint(Scope)->UbergraphPages[0];
	UK2Node_CustomNode* Node = PlaceNode(Graph, Asset);
	auto HasState = [](const UK2Node_CustomNode* Target, int32 Count, bool bSplit)
	{
		return Target && Target->GetDynamicPinCount() == Count && (Target->FindPin(TEXT("Location_X")) != nullptr) == bSplit;
	};

	// Two steps: an element pin (its own transaction) and a split structure.
	Node->AddDynamicElement();
	{
		const FScopedTransaction Transaction(NSLOCTEXT("NodeFactory", "Tests.SplitPin", "Split Struct Pin"));
		GetDefault<UEdGraphSchema_K2>()->SplitPin(Node->FindPin(TEXT("Location")), /*bNotify*/ true);
	}
	TestTrue(TEXT("Edited"), HasState(Node, 2, true));
	TestTrue(TEXT("Undo split"), GEditor->UndoTransaction() && HasState(Node, 2, false));
	TestTrue(TEXT("Undo element"), GEditor->UndoTransaction() && HasState(Node, 1, false));
	TestTrue(TEXT("Redo both"), GEditor->RedoTransaction() && GEditor->RedoTransaction() && HasState(Node, 2, true));

	// Copy and paste (Duplicate takes the same path) keep the element count and the split pin (spec D6).
	TSet<UObject*> Selection;
	Selection.Add(Node);
	FString Text;
	FEdGraphUtilities::ExportNodesToText(Selection, Text);
	TSet<UEdGraphNode*> Pasted;
	FEdGraphUtilities::ImportNodesFromText(Graph, Text, Pasted);
	TestTrue(TEXT("Pasted copy"), Pasted.Num() == 1 && HasState(Cast<UK2Node_CustomNode>(*Pasted.CreateConstIterator()), 2, true));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
