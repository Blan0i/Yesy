// Copyright (c) NodeFactory. Shared helpers of the automation tests (spec 11.1).
// Every test builds its assets and Blueprints in the transient package; nothing is saved into the project.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CNAAssetFactory.h"
#include "CNAExpander.h"
#include "CNAGraphUtils.h"
#include "CNAUpdateService.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "SCNAPinManager.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/Package.h"

/** Editor context and the engine filter for every NodeFactory test (spec 11.1). */
#define CNA_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CNATest
{
	inline FEdGraphPinType MakeType(FName Category, EPinContainerType Container = EPinContainerType::None, UObject* SubObject = nullptr)
	{
		FEdGraphPinType Type;
		Type.PinCategory = Category;
		Type.ContainerType = Container;
		Type.PinSubCategoryObject = SubObject;
		return Type;
	}

	/** Releases the objects of one test to the garbage collector when the test ends. */
	struct FScope
	{
		TArray<TWeakObjectPtr<UObject>> Objects;

		template<typename ObjectType>
		ObjectType* Keep(ObjectType* Object)
		{
			Objects.Add(TWeakObjectPtr<UObject>(Object));
			return Object;
		}

		~FScope()
		{
			for (const TWeakObjectPtr<UObject>& Object : Objects)
			{
				if (UObject* Live = Object.Get())
				{
					Live->ClearFlags(RF_Public | RF_Standalone);
				}
			}
		}
	};

	/** A new asset as the factory makes it (impure, execute -> then) or a pure one without exec pins. */
	inline UCustomNodeAsset* MakeAsset(FScope& Scope, const TCHAR* BaseName, bool bPure = false)
	{
		const FName Name = MakeUniqueObjectName(GetTransientPackage(), UCustomNodeAsset::StaticClass(), FName(BaseName));
		UCustomNodeAsset* Asset = Scope.Keep(UCNAAssetFactory::CreateCustomNodeAsset(GetTransientPackage(), Name));
		if (Asset && bPure)
		{
			Asset->bPure = true;
			FCNAPinManagerOps::RemovePin(Asset, EGPD_Input, UEdGraphSchema_K2::PN_Execute);
			FCNAPinManagerOps::RemovePin(Asset, EGPD_Output, UEdGraphSchema_K2::PN_Then);
		}
		return Asset;
	}

	inline FName AddPin(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName Category, const TCHAR* Name,
		EPinContainerType Container = EPinContainerType::None, UObject* SubObject = nullptr)
	{
		return FCNAPinManagerOps::AddPin(Asset, Side, MakeType(Category, Container, SubObject), FName(Name));
	}

	/** Pin of Tunnel Input (EGPD_Input) or Tunnel Output (EGPD_Output) in the macro Execute. */
	inline UEdGraphPin* Tunnel(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName)
	{
		return FCNAPinManagerOps::FindTunnelPin(Asset, Side, PinName);
	}

	/** What a save does: cached registry data and the version of the signature (spec 9.1). */
	inline void Commit(UCustomNodeAsset* Asset)
	{
		FCNAGraphUtils::RefreshCachedAssetData(Asset);
		FCNAUpdateService::RefreshAssetVersion(Asset);
	}

	inline bool Link(UEdGraphPin* From, UEdGraphPin* To)
	{
		return From && To && GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(From, To);
	}

	/** Places a node the way the editor does: Setup runs first, then guid, PostPlacedNewNode and the pins. */
	template<typename NodeType, typename SetupType>
	NodeType* Spawn(UEdGraph* Graph, SetupType Setup)
	{
		FGraphNodeCreator<NodeType> Creator(*Graph);
		NodeType* Node = Creator.CreateNode(/*bSelectNewNode*/ false);
		Setup(Node);
		Creator.Finalize();
		return Node;
	}

	template<typename NodeType>
	NodeType* Spawn(UEdGraph* Graph)
	{
		return Spawn<NodeType>(Graph, [](NodeType*) {});
	}

	inline UK2Node_CallFunction* SpawnCall(UEdGraph* Graph, UClass* Class, const TCHAR* Function)
	{
		return Spawn<UK2Node_CallFunction>(Graph, [Class, Function](UK2Node_CallFunction* Node)
		{
			Node->SetFromFunction(Class->FindFunctionByName(Function));
		});
	}

	inline UK2Node_CustomNode* PlaceNode(UEdGraph* Graph, UCustomNodeAsset* Asset)
	{
		return Spawn<UK2Node_CustomNode>(Graph, [Asset](UK2Node_CustomNode* Node) { Node->SetCustomNodeAsset(Asset); });
	}

	/** Actor Blueprint: its self pin gives latent and world calls their world context. */
	inline UBlueprint* MakeBlueprint(FScope& Scope)
	{
		const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("CNATest_BP"));
		return Scope.Keep(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(), Name, BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("NodeFactoryTests")));
	}

	/** Exec output of a new custom event in the Event Graph. */
	inline UEdGraphPin* EventThen(UBlueprint* Blueprint, UEdGraph*& OutGraph)
	{
		static int32 Counter = 0;
		OutGraph = Blueprint->UbergraphPages[0];
		UK2Node_CustomEvent* Event = Spawn<UK2Node_CustomEvent>(OutGraph, [](UK2Node_CustomEvent* Node)
		{
			Node->CustomFunctionName = *FString::Printf(TEXT("CNATestEvent%d"), ++Counter);
		});
		return Event->FindPin(UEdGraphSchema_K2::PN_Then);
	}

	/** Exec output of the entry of a new function graph. */
	inline UEdGraphPin* FunctionThen(UBlueprint* Blueprint, UEdGraph*& OutGraph)
	{
		OutGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, TEXT("CNATestFunction")),
			UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, OutGraph, /*bIsUserCreated*/ true, nullptr);
		TArray<UK2Node_FunctionEntry*> Entries;
		OutGraph->GetNodesOfClass(Entries);
		return Entries.Num() > 0 ? Entries[0]->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr;
	}

	inline FString Describe(const FCompilerResultsLog& Results)
	{
		FString Text;
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			Text += Message->ToText().ToString() + TEXT(" | ");
		}
		return Text;
	}

	inline bool HasCode(const FCompilerResultsLog& Results, const TCHAR* Code)
	{
		return Describe(Results).Contains(Code);
	}

	/** Silent full compile; the test fails with the messages when the outcome differs from bExpectSuccess. */
	inline bool ExpectCompile(FAutomationTestBase& Test, UBlueprint* Blueprint, bool bExpectSuccess, FCompilerResultsLog& Results)
	{
		// V-check: EBlueprintCompileOptions::SkipGarbageCollection and the results argument in 4.20.3.
		Results.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		const bool bSuccess = Blueprint->Status != BS_Error && Results.NumErrors == 0;
		Test.TestTrue(*FString::Printf(TEXT("%s compiles = %d: %s"), *Blueprint->GetName(), bExpectSuccess ? 1 : 0, *Describe(Results)),
			bSuccess == bExpectSuccess);
		return bSuccess == bExpectSuccess;
	}
}
