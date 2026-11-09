// Copyright (c) NodeFactory. Entries of the Blueprint action menu for custom nodes and local variables (spec 9.8, V9).

#include "CNANodeMenu.h"
#include "CNAMessages.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "K2Node_GetLocal.h"
#include "K2Node_SetLocal.h"
#include "CNAPinUtils.h"
#include "NodeFactoryModule.h"
#include "NodeFactoryStyle.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "Containers/Ticker.h"
#include "EditorStyleSet.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNANodeMenuLocal
{
	const FName DefaultIconName(TEXT("Kismet.AllClasses.FunctionIcon"));
	const FName LocalVariableIconName(TEXT("Kismet.AllClasses.VariableIcon"));

	bool bStarted = false;
	bool bRefreshPending = false;
	int32 LastRegisteredCount = 0;

	FDelegateHandle TickerHandle;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle FilesLoadedHandle;
	FDelegateHandle AssetChangedHandle;
	FDelegateHandle AssetPreSaveHandle;

	/** Delay of the debounced refresh: bursts of registry events end up in one rebuild. */
	const float RefreshDelaySeconds = 0.25f;

	IAssetRegistry* GetRegistry()
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
		{
			return nullptr;
		}
		return &FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	}

	FSlateIcon MakeIcon(const UCustomNodeAsset* Asset, FLinearColor& OutTint)
	{
		OutTint = Asset ? Asset->IconTint : FLinearColor::White;
		const FName IconName = (Asset && !Asset->Icon.IsNone()) ? Asset->Icon : DefaultIconName;
		if (FNodeFactoryStyle::IsInitialized() && FNodeFactoryStyle::Get().GetOptionalBrush(IconName, nullptr, nullptr) != nullptr)
		{
			return FSlateIcon(FNodeFactoryStyle::GetStyleSetName(), IconName);
		}
		if (FEditorStyle::Get().GetOptionalBrush(IconName, nullptr, nullptr) != nullptr)
		{
			return FSlateIcon(FEditorStyle::GetStyleSetName(), IconName);
		}
		return FSlateIcon(FEditorStyle::GetStyleSetName(), DefaultIconName);
	}

	/** Search words never empty: an empty text makes the spawner build a template node to ask for them. */
	FText MakeKeywords(const FText& Keywords, const FString& AssetName)
	{
		const FString Words = FString::Printf(TEXT("%s %s CNA NodeFactory"), *Keywords.ToString(), *AssetName).TrimStartAndEnd();
		return FText::FromString(Words);
	}

	bool OnRefreshTick(float DeltaTime)
	{
		bRefreshPending = false;
		TickerHandle.Reset();
		if (!bStarted)
		{
			return false;
		}
		// During the first scan the registry is incomplete; OnFilesLoaded asks again.
		if (IAssetRegistry* Registry = GetRegistry())
		{
			if (Registry->IsLoadingAssets())
			{
				return false;
			}
		}
		FCNANodeMenu::RefreshNow();
		return false;
	}

	void OnRegistryAssetEvent(const FAssetData& AssetData)
	{
		if (AssetData.AssetClass == UCustomNodeAsset::StaticClass()->GetFName())
		{
			FCNANodeMenu::RequestRefresh();
		}
	}

	void OnRegistryAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
	{
		OnRegistryAssetEvent(AssetData);
	}

	void OnRegistryFilesLoaded()
	{
		FCNANodeMenu::RequestRefresh();
	}

	void OnAssetChanged(UCustomNodeAsset* Asset, FName PropertyName)
	{
		FCNANodeMenu::RequestRefresh();
	}

	void OnAssetPreSave(UCustomNodeAsset* Asset)
	{
		FCNANodeMenu::RequestRefresh();
	}
}

// Custom nodes

bool FCNANodeMenu::IsMenuAsset(const FAssetData& AssetData)
{
	if (AssetData.IsAssetLoaded())
	{
		const UCustomNodeAsset* Asset = Cast<UCustomNodeAsset>(AssetData.GetAsset());
		return Asset && !Asset->IsPendingKill() && !Asset->bHiddenInMenu;
	}
	FString Hidden;
	return !(AssetData.GetTagValue(CNATags::Hidden, Hidden) && CNATags::ToBool(Hidden));
}

void FCNANodeMenu::RegisterMenuActions(FBlueprintActionDatabaseRegistrar& Registrar, UClass* NodeClass)
{
	if (!NodeClass || !Registrar.IsOpenForRegistration(NodeClass))
	{
		return;
	}
	IAssetRegistry* Registry = CNANodeMenuLocal::GetRegistry();
	if (!Registry)
	{
		return;
	}

	TArray<FAssetData> Assets;
	Registry->GetAssetsByClass(UCustomNodeAsset::StaticClass()->GetFName(), Assets, /*bSearchSubClasses*/ true);

	int32 Count = 0;
	for (const FAssetData& AssetData : Assets)
	{
		if (!IsMenuAsset(AssetData))
		{
			continue;
		}

		const FSoftObjectPath AssetPath(AssetData.ObjectPath.ToString());
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(NodeClass);
		if (!Spawner)
		{
			continue;
		}
		Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(&FCNANodeMenu::CustomizeCustomNode, AssetPath);

		// The menu text comes from the asset when it is in memory (unsaved edits included), else from the tags.
		FBlueprintActionUiSpec& Signature = Spawner->DefaultMenuSignature;
		const UCustomNodeAsset* Loaded = AssetData.IsAssetLoaded() ? Cast<UCustomNodeAsset>(AssetData.GetAsset()) : nullptr;
		FCNATemplateData Data;
		Data.ReadFromAssetData(AssetData);
		const FText DisplayName = Loaded ? Loaded->GetEffectiveDisplayName() : Data.DisplayName;
		const FText Category = Loaded ? Loaded->Category : Data.Category;
		const FText Tooltip = Loaded ? Loaded->Tooltip : Data.Tooltip;
		const FText Keywords = Loaded ? Loaded->Keywords : Data.Keywords;

		Signature.MenuName = DisplayName;
		Signature.Category = Category.IsEmpty() ? NSLOCTEXT("NodeFactory", "DefaultMenuCategory", "Custom Nodes") : Category;
		Signature.Tooltip = Tooltip.IsEmpty() ? DisplayName : Tooltip;
		Signature.Keywords = CNANodeMenuLocal::MakeKeywords(Keywords, AssetData.AssetName.ToString());
		Signature.Icon = CNANodeMenuLocal::MakeIcon(Loaded, Signature.IconTint);

		if (Registrar.AddBlueprintAction(Spawner))
		{
			++Count;
		}
	}

	CNANodeMenuLocal::LastRegisteredCount = Count;
	CNA_VERBOSE(TEXT("Node menu: %d custom node entries registered."), Count);
}

void FCNANodeMenu::CustomizeCustomNode(UEdGraphNode* NewNode, bool bIsTemplateNode, FSoftObjectPath AssetPath)
{
	UK2Node_CustomNode* Node = Cast<UK2Node_CustomNode>(NewNode);
	if (!Node)
	{
		return;
	}

	if (bIsTemplateNode)
	{
		// Template nodes of the menu never load the asset (spec 9.8): a loaded asset is used, otherwise the tags.
		if (UCustomNodeAsset* Loaded = Cast<UCustomNodeAsset>(AssetPath.ResolveObject()))
		{
			Node->SetCustomNodeAsset(Loaded);
			return;
		}
		Node->AssetPath = AssetPath;
		if (IAssetRegistry* Registry = CNANodeMenuLocal::GetRegistry())
		{
			const FAssetData AssetData = Registry->GetAssetByObjectPath(FName(*AssetPath.ToString()));
			if (AssetData.IsValid())
			{
				FCNATemplateData Data;
				Data.ReadFromAssetData(AssetData);
				Node->InitTemplateData(Data);
			}
		}
		return;
	}

	// A placed node loads its asset: the pins are built from the macro Execute (spec 2.3).
	UCustomNodeAsset* Asset = Cast<UCustomNodeAsset>(AssetPath.TryLoad());
	if (Asset)
	{
		Node->SetCustomNodeAsset(Asset);
	}
	else
	{
		// The node stays with its path and reports CNA005 (spec 2.4, item 6).
		Node->AssetPath = AssetPath;
		UE_LOG(LogNodeFactory, Warning, TEXT("Custom Node Asset %s could not be loaded for a new node (CNA005)."), *AssetPath.ToString());
	}
}

// Local variables (spec 4.8)

void FCNANodeMenu::RegisterLocalVariableActions(FBlueprintActionDatabaseRegistrar& Registrar, UClass* NodeClass)
{
	if (!NodeClass || !Registrar.IsOpenForRegistration(NodeClass))
	{
		return;
	}

	const bool bSetter = NodeClass->IsChildOf(UK2Node_SetLocal::StaticClass());
	const FText Category = LOCTEXT("NodeMenu.LocalVariablesCategory", "Local Variables");
	FLinearColor IconTint = FLinearColor::White;
	const FSlateIcon Icon(FEditorStyle::GetStyleSetName(), CNANodeMenuLocal::LocalVariableIconName);

	// The unbound entry takes the first variable of the asset; the drop-down of the node chooses another one.
	if (UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(NodeClass))
	{
		Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(&FCNANodeMenu::CustomizeLocalVariableNode, FGuid(), FName());
		FBlueprintActionUiSpec& Signature = Spawner->DefaultMenuSignature;
		Signature.MenuName = bSetter ? LOCTEXT("NodeMenu.SetLocalEntry", "Set Local Variable") : LOCTEXT("NodeMenu.GetLocalEntry", "Get Local Variable");
		Signature.Category = Category;
		Signature.Tooltip = bSetter
			? LOCTEXT("NodeMenu.SetLocalEntryTooltip", "Writes a local variable of this node instance. Choose the variable on the node.")
			: LOCTEXT("NodeMenu.GetLocalEntryTooltip", "Reads a local variable of this node instance. Choose the variable on the node.");
		Signature.Keywords = LOCTEXT("NodeMenu.LocalEntryKeywords", "Local Variable Temporary CNA");
		Signature.Icon = Icon;
		Signature.IconTint = IconTint;
		Registrar.AddBlueprintAction(Spawner);
	}

	// One entry per variable of the assets in memory; IsCompatibleWithGraph keeps each entry in its own asset.
	for (TObjectIterator<UCustomNodeAsset> It; It; ++It)
	{
		const UCustomNodeAsset* Asset = *It;
		if (!Asset || Asset->IsPendingKill() || Asset->HasAnyFlags(RF_ClassDefaultObject | RF_Transient))
		{
			continue;
		}
		for (const FCNALocalVariable& Variable : Asset->LocalVariables)
		{
			if (!Variable.IsValid())
			{
				continue;
			}
			UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(NodeClass);
			if (!Spawner)
			{
				continue;
			}
			Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(&FCNANodeMenu::CustomizeLocalVariableNode, Variable.Guid, Variable.Name);
			FBlueprintActionUiSpec& Signature = Spawner->DefaultMenuSignature;
			const FText VariableText = FText::FromName(Variable.Name);
			Signature.MenuName = FText::Format(bSetter ? LOCTEXT("NodeMenu.SetLocalVariableEntry", "Set {0}") : LOCTEXT("NodeMenu.GetLocalVariableEntry", "Get {0}"), VariableText);
			Signature.Category = Category;
			Signature.Tooltip = FText::Format(LOCTEXT("NodeMenu.LocalVariableEntryTooltip", "{0} ({1})\n{2}"), VariableText, FCNAPinUtils::GetTypeText(Variable.Type), Variable.Tooltip);
			Signature.Keywords = FText::Format(LOCTEXT("NodeMenu.LocalVariableEntryKeywords", "{0} Local Variable CNA"), VariableText);
			Signature.Icon = Icon;
			Signature.IconTint = FCNAPinUtils::GetTypeColor(Variable.Type);
			Registrar.AddBlueprintAction(Spawner);
		}
	}
}

void FCNANodeMenu::CustomizeLocalVariableNode(UEdGraphNode* NewNode, bool bIsTemplateNode, FGuid VariableGuid, FName VariableName)
{
	UK2Node_CNALocalVariableBase* Node = Cast<UK2Node_CNALocalVariableBase>(NewNode);
	if (!Node)
	{
		return;
	}

	if (VariableGuid.IsValid())
	{
		// The asset of the target graph has the variable (IsCompatibleWithGraph); templates keep the cached name.
		const UCustomNodeAsset* Asset = bIsTemplateNode ? nullptr : Node->GetOwningAsset();
		if (const FCNALocalVariable* Variable = Asset ? Asset->FindLocalVariable(VariableGuid, VariableName) : nullptr)
		{
			Node->InitVariable(*Variable);
		}
		else
		{
			Node->VariableGuid = VariableGuid;
			Node->VariableName = VariableName;
		}
		return;
	}

	// Unbound entry: the first variable of the asset, if there is one.
	if (!bIsTemplateNode)
	{
		const UCustomNodeAsset* Asset = Node->GetOwningAsset();
		if (Asset && Asset->LocalVariables.Num() > 0)
		{
			Node->InitVariable(Asset->LocalVariables[0]);
		}
	}
}

// Refresh (V9)

void FCNANodeMenu::RequestRefresh()
{
	using namespace CNANodeMenuLocal;
	if (!bStarted || bRefreshPending)
	{
		return;
	}
	bRefreshPending = true;
	TickerHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&CNANodeMenuLocal::OnRefreshTick), RefreshDelaySeconds);
}

void FCNANodeMenu::RefreshNow()
{
	FBlueprintActionDatabase& Database = FBlueprintActionDatabase::Get();
	Database.RefreshClassActions(UK2Node_CustomNode::StaticClass());
	Database.RefreshClassActions(UK2Node_GetLocal::StaticClass());
	Database.RefreshClassActions(UK2Node_SetLocal::StaticClass());
}

int32 FCNANodeMenu::GetLastRegisteredCount()
{
	return CNANodeMenuLocal::LastRegisteredCount;
}

void FCNANodeMenu::Startup()
{
	using namespace CNANodeMenuLocal;
	if (bStarted)
	{
		return;
	}
	bStarted = true;

	if (IAssetRegistry* Registry = GetRegistry())
	{
		AssetAddedHandle = Registry->OnAssetAdded().AddStatic(&CNANodeMenuLocal::OnRegistryAssetEvent);
		AssetRemovedHandle = Registry->OnAssetRemoved().AddStatic(&CNANodeMenuLocal::OnRegistryAssetEvent);
		AssetRenamedHandle = Registry->OnAssetRenamed().AddStatic(&CNANodeMenuLocal::OnRegistryAssetRenamed);
		FilesLoadedHandle = Registry->OnFilesLoaded().AddStatic(&CNANodeMenuLocal::OnRegistryFilesLoaded);
	}
	AssetChangedHandle = FCNAEditorHooks::OnAssetChanged.AddStatic(&CNANodeMenuLocal::OnAssetChanged);
	AssetPreSaveHandle = FCNAEditorHooks::OnAssetPreSave.AddStatic(&CNANodeMenuLocal::OnAssetPreSave);
}

void FCNANodeMenu::Shutdown()
{
	using namespace CNANodeMenuLocal;
	if (!bStarted)
	{
		return;
	}
	bStarted = false;

	if (TickerHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	bRefreshPending = false;

	FCNAEditorHooks::OnAssetPreSave.Remove(AssetPreSaveHandle);
	FCNAEditorHooks::OnAssetChanged.Remove(AssetChangedHandle);
	AssetPreSaveHandle.Reset();
	AssetChangedHandle.Reset();

	// The registry may already be gone when the editor closes.
	if (IAssetRegistry* Registry = GetRegistry())
	{
		Registry->OnFilesLoaded().Remove(FilesLoadedHandle);
		Registry->OnAssetRenamed().Remove(AssetRenamedHandle);
		Registry->OnAssetRemoved().Remove(AssetRemovedHandle);
		Registry->OnAssetAdded().Remove(AssetAddedHandle);
	}
	FilesLoadedHandle.Reset();
	AssetRenamedHandle.Reset();
	AssetRemovedHandle.Reset();
	AssetAddedHandle.Reset();
}

#undef LOCTEXT_NAMESPACE
