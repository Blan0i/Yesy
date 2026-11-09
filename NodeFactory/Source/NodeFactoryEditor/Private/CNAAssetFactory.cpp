// Copyright (c) NodeFactory. Factory of Custom Node Assets: Content Browser > Add New > Blueprints (spec 7.2).

#include "CNAAssetFactory.h"
#include "CNABehaviorBase.h"
#include "CNAGraphUtils.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryModule.h"

#include "AssetTypeCategories.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

UCNAAssetFactory::UCNAAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UCustomNodeAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = false;
	bText = false;
}

UObject* UCNAAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return CreateCustomNodeAsset(InParent, InName);
}

uint32 UCNAAssetFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Blueprint;
}

FText UCNAAssetFactory::GetDisplayName() const
{
	return LOCTEXT("Factory.DisplayName", "Custom Node Asset");
}

FText UCNAAssetFactory::GetToolTip() const
{
	return LOCTEXT("Factory.Tooltip", "A Blueprint node of your own: logic in the macro Execute, look and pins in the Details, editor reactions in the Behavior Graph.");
}

FString UCNAAssetFactory::GetDefaultNewAssetName() const
{
	return TEXT("CNA_NewNode");
}

UCustomNodeAsset* UCNAAssetFactory::CreateCustomNodeAsset(UObject* Outer, FName Name)
{
	// V23: a Blueprint subclass with its own generated class; the parent class is the only allowed one (D4).
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UCNABehaviorBase::StaticClass(), Outer, Name, BPTYPE_Normal,
		UCustomNodeAsset::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("NodeFactory"));
	UCustomNodeAsset* Asset = Cast<UCustomNodeAsset>(Blueprint);
	if (!Asset)
	{
		UE_LOG(LogNodeFactory, Error, TEXT("Custom Node Asset %s could not be created."), *Name.ToString());
		return nullptr;
	}

	CreateDefaultGraphs(Asset, /*bWithDefaultPins*/ true);
	FCNAGraphUtils::RefreshCachedAssetData(Asset);
	// The first save must not count as a change of the pin structure (spec 9.1).
	Asset->SignatureHash = FCNAGraphUtils::ComputeSignatureHash(Asset);
	FKismetEditorUtilities::CompileBlueprint(Asset);
	return Asset;
}

void UCNAAssetFactory::CreateDefaultGraphs(UCustomNodeAsset* Asset, bool bWithDefaultPins)
{
	if (!Asset)
	{
		return;
	}
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	// Macro Execute (D1). A graph object of that name that is not listed (damaged asset) is listed again.
	if (!Asset->FindExecuteGraph())
	{
		UEdGraph* Graph = FindObject<UEdGraph>(Asset, *UCustomNodeAsset::ExecuteGraphName.ToString());
		const bool bNewGraph = Graph == nullptr;
		if (bNewGraph)
		{
			Graph = FBlueprintEditorUtils::CreateNewGraph(Asset, UCustomNodeAsset::ExecuteGraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddMacroGraph(Asset, Graph, /*bIsUserCreated*/ true, /*SignatureFromClass*/ nullptr);
		}
		else
		{
			Graph->Modify();
			Asset->MacroGraphs.AddUnique(Graph);
			if (!FCNAGraphUtils::FindEntryTunnel(Graph) || !FCNAGraphUtils::FindExitTunnel(Graph))
			{
				Schema->CreateMacroGraphTerminators(*Graph, nullptr);
			}
		}

		UK2Node_Tunnel* Entry = FCNAGraphUtils::FindEntryTunnel(Graph);
		UK2Node_Tunnel* Exit = FCNAGraphUtils::FindExitTunnel(Graph);
		if (bWithDefaultPins && bNewGraph && Entry && Exit)
		{
			FEdGraphPinType ExecType;
			ExecType.PinCategory = UEdGraphSchema_K2::PC_Exec;
			UEdGraphPin* In = Entry->CreateUserDefinedPin(UEdGraphSchema_K2::PN_Execute, ExecType, EGPD_Output, /*bUseUniqueName*/ false);
			UEdGraphPin* Out = Exit->CreateUserDefinedPin(UEdGraphSchema_K2::PN_Then, ExecType, EGPD_Input, /*bUseUniqueName*/ false);
			if (In && Out)
			{
				In->MakeLinkTo(Out);
			}
			Entry->NodePosX = -400;
			Entry->NodePosY = 0;
			Exit->NodePosX = 400;
			Exit->NodePosY = 0;
		}
	}

	// Editor Behavior Graph: the Event Graph page (D4). A new asset names it after its role.
	UEdGraph* Behavior = Asset->FindBehaviorGraph();
	if (!Behavior)
	{
		Behavior = FBlueprintEditorUtils::CreateNewGraph(Asset, UCustomNodeAsset::BehaviorGraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		Behavior->bAllowDeletion = false;
		FBlueprintEditorUtils::AddUbergraphPage(Asset, Behavior);
	}
	else if (bWithDefaultPins && Behavior->GetFName() != UCustomNodeAsset::BehaviorGraphName
		&& !FindObject<UObject>(Asset, *UCustomNodeAsset::BehaviorGraphName.ToString()))
	{
		FBlueprintEditorUtils::RenameGraph(Behavior, UCustomNodeAsset::BehaviorGraphName.ToString());
	}

	Asset->RefreshGraphPointers();
}

#undef LOCTEXT_NAMESPACE
