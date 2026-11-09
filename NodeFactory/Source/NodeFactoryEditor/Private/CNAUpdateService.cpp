// Copyright (c) NodeFactory. Update of all node instances after a change of the asset (spec 9.1).

#include "CNAUpdateService.h"
#include "CNAEditorSettings.h"
#include "CNAGraphUtils.h"
#include "CNAMessages.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "NodeFactoryModule.h"
#include "SCNAUpdateDialog.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IPackageAutoSaver.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "SourceControlOperations.h"
#include "TimerManager.h"
#include "Toolkits/AssetEditorManager.h"
#include "UnrealEdGlobals.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

// State

namespace CNAUpdateServiceLocal
{
	bool bStarted = false;

	/** Recursion guard of the update (spec 9.6). */
	bool bUpdating = false;

	/** The confirmation window is open; deferred work waits for it. */
	bool bDialogOpen = false;

	FDelegateHandle PreSaveHandle;
	FDelegateHandle AssetChangedHandle;
	FDelegateHandle EditorOpenedHandle;

	/** Deferred work: dialogs are never shown inside a save or while an asset editor is being built. */
	FTimerHandle PendingTimer;

	/** Debounce of the redraw of instances after a change of appearance. */
	FTimerHandle WidgetTimer;

	/** Assets whose AssetVersion was bumped by a save. */
	TArray<TWeakObjectPtr<UCustomNodeAsset>> PendingSavedAssets;

	/** Blueprints that were opened in an editor; their outdated nodes are checked when the editor is built. */
	TArray<TWeakObjectPtr<UBlueprint>> PendingOpenedBlueprints;

	/** Assets whose appearance changed. */
	TArray<TWeakObjectPtr<UCustomNodeAsset>> PendingWidgetAssets;

	const float PendingDelaySeconds = 0.1f;
	const float RetryDelaySeconds = 0.5f;
	const float WidgetRefreshDelaySeconds = 0.3f;

	/** Name of the registry tag every Blueprint asset writes (UBlueprint::GetAssetRegistryTags). */
	const FName GeneratedClassTag(TEXT("GeneratedClass"));

	IAssetRegistry& GetRegistry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	}

	FTimerManager* GetTimerManager()
	{
		// V-check: UEditorEngine::GetTimerManager returns TSharedRef<FTimerManager> in 4.20.3.
		return GEditor ? &GEditor->GetTimerManager().Get() : nullptr;
	}

	/** Autosave writes copies of the packages; it must not bump versions or open windows. */
	bool IsAutoSaving()
	{
		// 4.20.3 keeps the auto saver in UUnrealEdEngine (V-check confirmed by the build).
		return GUnrealEd && GUnrealEd->GetPackageAutoSaver().IsAutoSaving();
	}

	/** Windows and package loads wait for a calm editor: no save, no load, no GC, no PIE, no modal window. */
	bool CanRunDeferredWork()
	{
		if (GIsSavingPackage || IsGarbageCollecting() || GIsEditorLoadingPackage)
		{
			return false;
		}
		if (GEditor && GEditor->PlayWorld)
		{
			return false;
		}
		if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetActiveModalWindow().IsValid())
		{
			return false;
		}
		return !bUpdating && !bDialogOpen;
	}

	void ProcessPending();

	void SchedulePending(float Delay)
	{
		if (FTimerManager* TimerManager = GetTimerManager())
		{
			TimerManager->SetTimer(PendingTimer, FTimerDelegate::CreateStatic(&ProcessPending), FMath::Max(Delay, 0.01f), false);
		}
	}

	template <typename ObjectType>
	TArray<ObjectType*> TakeValid(TArray<TWeakObjectPtr<ObjectType>>& Pending)
	{
		TArray<ObjectType*> Result;
		for (const TWeakObjectPtr<ObjectType>& Weak : Pending)
		{
			if (ObjectType* Object = Weak.Get())
			{
				Result.AddUnique(Object);
			}
		}
		Pending.Reset();
		return Result;
	}

	void ProcessPending()
	{
		PendingTimer.Invalidate();
		if (!bStarted)
		{
			return;
		}
		if (!CanRunDeferredWork())
		{
			SchedulePending(RetryDelaySeconds);
			return;
		}

		const TArray<UCustomNodeAsset*> SavedAssets = TakeValid(PendingSavedAssets);
		const TArray<UBlueprint*> OpenedBlueprints = TakeValid(PendingOpenedBlueprints);

		if (SavedAssets.Num() > 0)
		{
			FCNAUpdateService::RequestUpdate(SavedAssets, ECNAUpdateTrigger::AssetSaved);
		}
		for (UBlueprint* Blueprint : OpenedBlueprints)
		{
			FCNAUpdateService::HandleBlueprintOpened(Blueprint);
		}
	}

	/** Packages of the assets, for the registry query. */
	TSet<FName> GetPackageNames(const TArray<UCustomNodeAsset*>& Assets)
	{
		TSet<FName> Result;
		for (const UCustomNodeAsset* Asset : Assets)
		{
			if (Asset)
			{
				Result.Add(Asset->GetOutermost()->GetFName());
			}
		}
		return Result;
	}

	bool ContainsEntry(const TArray<FCNAReferencingBlueprint>& Entries, const UBlueprint* Blueprint)
	{
		for (const FCNAReferencingBlueprint& Entry : Entries)
		{
			if (Entry.Loaded.Get() == Blueprint)
			{
				return true;
			}
		}
		return false;
	}

	/** Blueprints (and maps) of one referencing package, read from the registry without loading. */
	void AddPackageEntries(IAssetRegistry& Registry, FName PackageName, TArray<FCNAReferencingBlueprint>& OutEntries)
	{
		TArray<FAssetData> PackageAssets;
		Registry.GetAssetsByPackageName(PackageName, PackageAssets);

		for (const FAssetData& Data : PackageAssets)
		{
			UClass* AssetClass = Data.GetClass();
			const bool bIsWorld = AssetClass && AssetClass->IsChildOf(UWorld::StaticClass());

			FString GeneratedClass;
			const bool bIsBlueprint = (AssetClass && AssetClass->IsChildOf(UBlueprint::StaticClass()))
				|| (!AssetClass && Data.GetTagValue(GeneratedClassTag, GeneratedClass));

			if (!bIsWorld && !bIsBlueprint)
			{
				continue;
			}

			FCNAReferencingBlueprint& Entry = OutEntries[OutEntries.AddDefaulted()];
			Entry.ObjectPath = FSoftObjectPath(Data.ObjectPath.ToString());
			Entry.PackageName = PackageName;
			Entry.DisplayName = Data.AssetName.ToString();
			Entry.bIsMap = bIsWorld;

			// FAssetData::GetAsset loads; it is only called for assets that are already in memory.
			if (!Data.IsAssetLoaded())
			{
				continue;
			}
			if (bIsWorld)
			{
				// The level Blueprint lives inside the map and exists only while the map is loaded.
				const UWorld* World = Cast<UWorld>(Data.GetAsset());
				if (World && World->PersistentLevel)
				{
					Entry.Loaded = World->PersistentLevel->GetLevelScriptBlueprint(/*bDontCreate*/ true);
				}
			}
			else
			{
				Entry.Loaded = Cast<UBlueprint>(Data.GetAsset());
			}
		}
	}

	/** Checks the files of the Blueprints out (spec 9.1, item 3). Failures are added to the report. */
	void CheckOutBlueprints(const TArray<UBlueprint*>& Blueprints, FCNAUpdateReport& Report)
	{
		ISourceControlModule& SourceControl = ISourceControlModule::Get();
		if (!SourceControl.IsEnabled() || !SourceControl.GetProvider().IsAvailable())
		{
			return;
		}
		ISourceControlProvider& Provider = SourceControl.GetProvider();

		TArray<FString> Files;
		TMap<FString, FString> FileToBlueprint;
		for (const UBlueprint* Blueprint : Blueprints)
		{
			const UPackage* Package = Blueprint ? Blueprint->GetOutermost() : nullptr;
			if (!Package)
			{
				continue;
			}
			const FString Extension = Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
			FString File;
			if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), File, Extension))
			{
				continue;
			}
			File = FPaths::ConvertRelativePathToFull(File);
			Files.AddUnique(File);
			FileToBlueprint.Add(File, Blueprint->GetName());
		}
		if (Files.Num() == 0)
		{
			return;
		}

		TArray<FSourceControlStateRef> States;
		if (Provider.GetState(Files, States, EStateCacheUsage::ForceUpdate) != ECommandResult::Succeeded)
		{
			UE_LOG(LogNodeFactory, Warning, TEXT("Source control state of %d file(s) could not be read; nothing was checked out."), Files.Num());
			return;
		}

		TArray<FString> ToCheckOut;
		for (const FSourceControlStateRef& State : States)
		{
			if (!State->IsSourceControlled() || State->IsCheckedOut() || State->IsAdded())
			{
				continue;
			}
			const FString* BlueprintName = FileToBlueprint.Find(State->GetFilename());
			const FString Name = BlueprintName ? *BlueprintName : FPaths::GetBaseFilename(State->GetFilename());
			if (State->CanCheckout())
			{
				ToCheckOut.Add(State->GetFilename());
			}
			else
			{
				Report.FailedBlueprints.Add(FText::Format(LOCTEXT("Update.CannotCheckOut", "{0} (cannot be checked out)"), FText::FromString(Name)).ToString());
			}
		}

		if (ToCheckOut.Num() > 0
			&& Provider.Execute(ISourceControlOperation::Create<FCheckOut>(), ToCheckOut) != ECommandResult::Succeeded)
		{
			Report.FailedBlueprints.Add(FText::Format(LOCTEXT("Update.CheckOutFailed", "Check out of {0} file(s) failed"), FText::AsNumber(ToCheckOut.Num())).ToString());
		}
	}

	FText GetReason(ECNAUpdateTrigger Trigger, const FText& AssetNames)
	{
		switch (Trigger)
		{
		case ECNAUpdateTrigger::RecompileCommand:
			return FText::Format(LOCTEXT("Update.ReasonRecompile",
				"Recompile Node rebuilds every instance of {0}. Wires are kept by pin name, by the pin redirects and by type and position; losses are reported as CNA016."),
				AssetNames);
		case ECNAUpdateTrigger::BlueprintOpened:
			return FText::Format(LOCTEXT("Update.ReasonOpened",
				"This Blueprint contains nodes of {0} built from an older version of the asset. Updating rebuilds their pins; wires are kept by name, by the pin redirects and by type and position."),
				AssetNames);
		case ECNAUpdateTrigger::AssetSaved:
		default:
			return FText::Format(LOCTEXT("Update.ReasonSaved",
				"The pins of {0} changed. The nodes in the Blueprints below are outdated (CNA006). Updating rebuilds their pins; wires are kept by name, by the pin redirects and by type and position."),
				AssetNames);
		}
	}

	/**
	 * Shows the confirmation window when Editor Preferences enable it (spec 9.1, item 3). Without the window
	 * every listed Blueprint is updated and nothing is checked out. Returns false on Cancel.
	 */
	bool AskUser(const TArray<UCustomNodeAsset*>& Assets, const TArray<FCNAReferencingBlueprint>& Blueprints, ECNAUpdateTrigger Trigger,
		bool& bOutLoadFromDisk, bool& bOutCheckOut)
	{
		bOutLoadFromDisk = true;
		bOutCheckOut = false;

		const UCNAEditorSettings* Settings = GetDefault<UCNAEditorSettings>();
		if (!Settings || !Settings->bConfirmBulkUpdate)
		{
			return true;
		}

		TArray<FCNAUpdateDialogItem> Items;
		for (const FCNAReferencingBlueprint& Entry : Blueprints)
		{
			FCNAUpdateDialogItem& Item = Items[Items.AddDefaulted()];
			Item.Name = Entry.bIsMap && !Entry.IsLoaded()
				? FText::Format(LOCTEXT("Update.MapItem", "{0} (level Blueprint, open the level to update it)"), FText::FromString(Entry.DisplayName)).ToString()
				: Entry.DisplayName;
			Item.Path = Entry.PackageName.ToString();
			Item.bLoaded = Entry.IsLoaded();
		}

		const FText AssetNames = FCNAUpdateService::DescribeAssets(Assets);
		ECNAUpdateChoice Choice = ECNAUpdateChoice::Cancel;
		{
			TGuardValue<bool> DialogGuard(bDialogOpen, true);
			Choice = SCNAUpdateDialog::ShowModal(AssetNames, GetReason(Trigger, AssetNames), Items, bOutCheckOut);
		}

		if (Choice == ECNAUpdateChoice::Cancel)
		{
			return false;
		}
		bOutLoadFromDisk = Choice == ECNAUpdateChoice::UpdateAll;
		return true;
	}

	// Event handlers

	void OnAssetPreSave(UCustomNodeAsset* Asset)
	{
		if (!Asset || IsAutoSaving())
		{
			return;
		}

		// Registry tags are written after PreSave; the cached values must describe the saved graphs (V19).
		FCNAGraphUtils::RefreshCachedAssetData(Asset);

		if (!FCNAUpdateService::RefreshAssetVersion(Asset))
		{
			return;
		}

		// The window is never shown inside the save: the request runs after the save finished.
		if (!IsRunningCommandlet() && !GIsAutomationTesting)
		{
			PendingSavedAssets.AddUnique(Asset);
			SchedulePending(PendingDelaySeconds);
		}
	}

	void OnAssetChanged(UCustomNodeAsset* Asset, FName PropertyName)
	{
		FCNAUpdateService::RequestWidgetRefresh(Asset);
	}

	/** V-check: FAssetEditorManager::OnAssetOpenedInEditor (UObject*, IAssetEditorInstance*) in 4.20.3. */
	void OnAssetOpenedInEditor(UObject* Asset, IAssetEditorInstance* Instance)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		if (!Blueprint || GIsAutomationTesting)
		{
			return;
		}
		// The editor is still being built; the check runs a moment later.
		PendingOpenedBlueprints.AddUnique(Blueprint);
		SchedulePending(PendingDelaySeconds);
	}

	void RefreshPendingWidgets()
	{
		WidgetTimer.Invalidate();
		const TArray<UCustomNodeAsset*> Assets = TakeValid(PendingWidgetAssets);
		if (Assets.Num() == 0)
		{
			return;
		}

		// Graphs with instances: their widgets are rebuilt and read the new appearance (title, colors, icon,
		// badges, body widgets). Pins are not touched; a change of the pins is handled by the version.
		TSet<UEdGraph*> Graphs;
		for (TObjectIterator<UK2Node_CustomNode> It; It; ++It)
		{
			UK2Node_CustomNode* Node = *It;
			if (!Node || Node->IsPendingKill() || Node->IsTemplate() || Node->IsPreviewNode() || Node->HasAnyFlags(RF_Transient))
			{
				continue;
			}
			if (Assets.Contains(Node->GetCustomNodeAsset()))
			{
				if (UEdGraph* Graph = Node->GetGraph())
				{
					Graphs.Add(Graph);
				}
			}
		}
		for (UEdGraph* Graph : Graphs)
		{
			Graph->NotifyGraphChanged();
		}
		CNA_VERBOSE(TEXT("Redrew the instances of %d asset(s) in %d graph(s)."), Assets.Num(), Graphs.Num());
	}
}

// Report

void FCNAUpdateReport::Append(const FCNAUpdateReport& Other)
{
	NumBlueprints += Other.NumBlueprints;
	NumNodes += Other.NumNodes;
	LostWires.Append(Other.LostWires);
	FailedBlueprints.Append(Other.FailedBlueprints);
	SkippedBlueprints.Append(Other.SkippedBlueprints);
	bCancelled |= Other.bCancelled;
}

FText FCNAUpdateReport::ToText() const
{
	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("Nodes"), NumNodes);
	Arguments.Add(TEXT("Blueprints"), NumBlueprints);
	Arguments.Add(TEXT("Lost"), LostWires.Num());
	Arguments.Add(TEXT("Failed"), FailedBlueprints.Num());
	Arguments.Add(TEXT("Skipped"), SkippedBlueprints.Num());

	FText Text = FText::Format(LOCTEXT("Update.SummaryNodes",
		"Updated {Nodes} {Nodes}|plural(one=node,other=nodes) in {Blueprints} {Blueprints}|plural(one=Blueprint,other=Blueprints)."), Arguments);

	const FTextFormat Join = FText::FromString(TEXT("{0} {1}"));
	if (LostWires.Num() > 0)
	{
		Text = FText::Format(Join, Text, FText::Format(LOCTEXT("Update.SummaryLost", "Lost wires: {Lost}."), Arguments));
	}
	if (FailedBlueprints.Num() > 0)
	{
		Text = FText::Format(Join, Text, FText::Format(LOCTEXT("Update.SummaryFailed", "Failed: {Failed}."), Arguments));
	}
	if (SkippedBlueprints.Num() > 0)
	{
		Text = FText::Format(Join, Text, FText::Format(LOCTEXT("Update.SummarySkipped", "Left outdated: {Skipped}."), Arguments));
	}
	if (bCancelled)
	{
		Text = FText::Format(Join, Text, LOCTEXT("Update.SummaryCancelled", "Cancelled by the user."));
	}
	return Text;
}

// Lifetime

void FCNAUpdateService::Startup()
{
	using namespace CNAUpdateServiceLocal;
	if (bStarted)
	{
		return;
	}
	bStarted = true;

	PreSaveHandle = FCNAEditorHooks::OnAssetPreSave.AddStatic(&OnAssetPreSave);
	AssetChangedHandle = FCNAEditorHooks::OnAssetChanged.AddStatic(&OnAssetChanged);
	EditorOpenedHandle = FAssetEditorManager::Get().OnAssetOpenedInEditor().AddStatic(&OnAssetOpenedInEditor);
}

void FCNAUpdateService::Shutdown()
{
	using namespace CNAUpdateServiceLocal;
	if (!bStarted)
	{
		return;
	}
	bStarted = false;

	if (FTimerManager* TimerManager = GetTimerManager())
	{
		TimerManager->ClearTimer(WidgetTimer);
		TimerManager->ClearTimer(PendingTimer);
	}
	PendingWidgetAssets.Reset();
	PendingOpenedBlueprints.Reset();
	PendingSavedAssets.Reset();

	// The asset editor manager is destroyed with the editor; it is only touched while the editor exists.
	if (GEditor && !GIsRequestingExit)
	{
		FAssetEditorManager::Get().OnAssetOpenedInEditor().Remove(EditorOpenedHandle);
	}
	FCNAEditorHooks::OnAssetChanged.Remove(AssetChangedHandle);
	FCNAEditorHooks::OnAssetPreSave.Remove(PreSaveHandle);
	EditorOpenedHandle.Reset();
	AssetChangedHandle.Reset();
	PreSaveHandle.Reset();
}

bool FCNAUpdateService::IsStarted()
{
	return CNAUpdateServiceLocal::bStarted;
}

bool FCNAUpdateService::IsUpdating()
{
	return CNAUpdateServiceLocal::bUpdating;
}

// Version

bool FCNAUpdateService::RefreshAssetVersion(UCustomNodeAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	const uint32 Hash = FCNAGraphUtils::ComputeSignatureHash(Asset);
	if (Hash == Asset->SignatureHash)
	{
		return false;
	}

	const bool bFirstHash = Asset->SignatureHash == 0;
	Asset->Modify();
	Asset->SignatureHash = Hash;

	if (bFirstHash)
	{
		// An asset without a stored hash (older format version, imported file) records it without a bump:
		// its instances were built from the same pins.
		CNA_VERBOSE(TEXT("%s: signature hash %u recorded."), *Asset->GetName(), Hash);
		return false;
	}

	Asset->BumpAssetVersion();
	return true;
}

// Search

void FCNAUpdateService::FindReferencingBlueprints(const TArray<UCustomNodeAsset*>& Assets, TArray<FCNAReferencingBlueprint>& OutBlueprints)
{
	using namespace CNAUpdateServiceLocal;
	OutBlueprints.Reset();

	const TSet<FName> AssetPackages = GetPackageNames(Assets);
	if (AssetPackages.Num() == 0)
	{
		return;
	}

	IAssetRegistry& Registry = GetRegistry();
	if (Registry.IsLoadingAssets())
	{
		UE_LOG(LogNodeFactory, Warning, TEXT("The asset registry is still scanning; Blueprints that are not scanned yet are not listed for the update."));
	}

	// 1. Packages that reference the assets (spec 9.1, item 2). Nothing is loaded here.
	TSet<FName> SeenPackages;
	for (const FName& AssetPackage : AssetPackages)
	{
		TArray<FName> Referencers;
		Registry.GetReferencers(AssetPackage, Referencers, EAssetRegistryDependencyType::All);
		for (const FName& Referencer : Referencers)
		{
			bool bAlreadySeen = false;
			SeenPackages.Add(Referencer, &bAlreadySeen);
			if (!bAlreadySeen)
			{
				AddPackageEntries(Registry, Referencer, OutBlueprints);
			}
		}
	}

	// 2. Loaded Blueprints the registry does not know yet: new, unsaved or renamed in this session.
	for (TObjectIterator<UK2Node_CustomNode> It; It; ++It)
	{
		UK2Node_CustomNode* Node = *It;
		if (!Node || Node->IsPendingKill() || Node->IsTemplate() || Node->IsPreviewNode() || Node->HasAnyFlags(RF_Transient))
		{
			continue;
		}
		if (!Assets.Contains(Node->GetCustomNodeAsset()))
		{
			continue;
		}
		UBlueprint* Blueprint = FCNAGraphUtils::GetBlueprint(Node);
		if (!Blueprint || Blueprint->HasAnyFlags(RF_Transient) || Blueprint->GetOutermost() == GetTransientPackage())
		{
			continue;
		}
		if (ContainsEntry(OutBlueprints, Blueprint))
		{
			continue;
		}

		// A registry entry of the same package that was not resolved to this object is replaced.
		const FName PackageName = Blueprint->GetOutermost()->GetFName();
		OutBlueprints.RemoveAll([PackageName](const FCNAReferencingBlueprint& Entry)
		{
			return Entry.PackageName == PackageName && !Entry.IsLoaded();
		});

		FCNAReferencingBlueprint& Entry = OutBlueprints[OutBlueprints.AddDefaulted()];
		Entry.ObjectPath = FSoftObjectPath(Blueprint);
		Entry.PackageName = PackageName;
		Entry.DisplayName = Blueprint->GetName();
		Entry.Loaded = Blueprint;
		Entry.bIsMap = Blueprint->IsA<ULevelScriptBlueprint>();
	}

	OutBlueprints.Sort([](const FCNAReferencingBlueprint& A, const FCNAReferencingBlueprint& B)
	{
		return A.DisplayName < B.DisplayName;
	});
}

void FCNAUpdateService::FindInstances(UBlueprint* Blueprint, const TArray<UCustomNodeAsset*>& Assets, bool bOnlyOutdated, TArray<UK2Node_CustomNode*>& OutNodes)
{
	OutNodes.Reset();
	if (!Blueprint)
	{
		return;
	}

	// All graphs of the Blueprint: event graphs, functions, macros (the Execute graph of nested assets) and
	// collapsed graphs.
	TArray<UK2Node_CustomNode*> AllNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_CustomNode>(Blueprint, AllNodes);

	for (UK2Node_CustomNode* Node : AllNodes)
	{
		if (!Node || Node->IsPendingKill() || Node->IsPreviewNode())
		{
			continue;
		}
		// A node without its asset keeps its pins (CNA005); there is nothing to rebuild it from.
		UCustomNodeAsset* NodeAsset = Node->GetCustomNodeAsset();
		if (!NodeAsset)
		{
			continue;
		}
		if (Assets.Num() > 0 && !Assets.Contains(NodeAsset))
		{
			continue;
		}
		if (bOnlyOutdated && !Node->IsOutdated())
		{
			continue;
		}
		OutNodes.Add(Node);
	}
}

int32 FCNAUpdateService::CountLoadedInstances(const UCustomNodeAsset* Asset, bool bOnlyOutdated)
{
	int32 Count = 0;
	for (TObjectIterator<UK2Node_CustomNode> It; It; ++It)
	{
		const UK2Node_CustomNode* Node = *It;
		if (!Node || Node->IsPendingKill() || Node->IsTemplate() || Node->IsPreviewNode() || Node->HasAnyFlags(RF_Transient))
		{
			continue;
		}
		if (Node->GetCustomNodeAsset() == Asset && (!bOnlyOutdated || Node->IsOutdated()) && FCNAGraphUtils::GetBlueprint(Node))
		{
			++Count;
		}
	}
	return Count;
}

// Update

FCNAUpdateReport FCNAUpdateService::RequestUpdate(UCustomNodeAsset* Asset, ECNAUpdateTrigger Trigger)
{
	TArray<UCustomNodeAsset*> Assets;
	if (Asset)
	{
		Assets.Add(Asset);
	}
	return RequestUpdate(Assets, Trigger);
}

FCNAUpdateReport FCNAUpdateService::RequestUpdate(const TArray<UCustomNodeAsset*>& InAssets, ECNAUpdateTrigger Trigger)
{
	using namespace CNAUpdateServiceLocal;
	FCNAUpdateReport Report;

	TArray<UCustomNodeAsset*> Assets;
	for (UCustomNodeAsset* Asset : InAssets)
	{
		if (Asset && !Asset->IsPendingKill())
		{
			Assets.AddUnique(Asset);
		}
	}
	if (Assets.Num() == 0 || bUpdating || bDialogOpen)
	{
		return Report;
	}

	const bool bOnlyOutdated = Trigger != ECNAUpdateTrigger::RecompileCommand;

	TArray<FCNAReferencingBlueprint> Blueprints;
	FindReferencingBlueprints(Assets, Blueprints);

	// Loaded Blueprints without work are dropped. Blueprints on disk were saved before the version changed
	// and are kept: whether they really contain instances is only known after loading.
	Blueprints.RemoveAll([&Assets, bOnlyOutdated](const FCNAReferencingBlueprint& Entry)
	{
		UBlueprint* Blueprint = Entry.Loaded.Get();
		if (!Blueprint)
		{
			return false;
		}
		TArray<UK2Node_CustomNode*> Nodes;
		FindInstances(Blueprint, Assets, bOnlyOutdated, Nodes);
		return Nodes.Num() == 0;
	});

	if (Blueprints.Num() == 0)
	{
		CNA_VERBOSE(TEXT("No Blueprint needs an update for %s."), *DescribeAssets(Assets).ToString());
		if (Trigger == ECNAUpdateTrigger::RecompileCommand)
		{
			FCNAMessages::LogInfo(FText::Format(LOCTEXT("Update.NoInstances", "{0}: no Blueprint uses the node; nothing to update."), DescribeAssets(Assets)),
				Assets[0]);
		}
		return Report;
	}

	bool bLoadFromDisk = true;
	bool bCheckOut = false;
	if (!AskUser(Assets, Blueprints, Trigger, bLoadFromDisk, bCheckOut))
	{
		Report.bCancelled = true;
		for (const FCNAReferencingBlueprint& Entry : Blueprints)
		{
			Report.SkippedBlueprints.Add(Entry.DisplayName);
		}
		ReportToLog(Assets, Report, Trigger);
		return Report;
	}

	FCNAMessages::NewPage(FText::Format(LOCTEXT("Update.PageTitle", "Update of {0}"), DescribeAssets(Assets)));
	Report = UpdateBlueprints(Assets, Blueprints, bLoadFromDisk, bCheckOut, bOnlyOutdated);
	ReportToLog(Assets, Report, Trigger);
	return Report;
}

FCNAUpdateReport FCNAUpdateService::UpdateBlueprints(const TArray<UCustomNodeAsset*>& Assets, const TArray<FCNAReferencingBlueprint>& Blueprints,
	bool bLoadFromDisk, bool bCheckOut, bool bOnlyOutdated)
{
	using namespace CNAUpdateServiceLocal;
	FCNAUpdateReport Report;
	if (bUpdating || Blueprints.Num() == 0)
	{
		return Report;
	}
	TGuardValue<bool> UpdateGuard(bUpdating, true);

	FScopedSlowTask SlowTask(static_cast<float>(Blueprints.Num() * 2), LOCTEXT("Update.Progress", "Updating custom nodes..."));
	if (!FApp::IsUnattended() && !GIsAutomationTesting)
	{
		SlowTask.MakeDialog(/*bShowCancelButton*/ true);
	}

	// 1. Loading (spec 9.1, item 4). Packages are loaded before the transaction opens, so loading is not recorded.
	TArray<UBlueprint*> ToUpdate;
	int32 Index = 0;
	for (; Index < Blueprints.Num(); ++Index)
	{
		const FCNAReferencingBlueprint& Entry = Blueprints[Index];
		SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Update.Loading", "Loading {0}"), FText::FromString(Entry.DisplayName)));
		if (SlowTask.ShouldCancel())
		{
			Report.bCancelled = true;
			break;
		}

		UBlueprint* Blueprint = Entry.Loaded.Get();
		if (!Blueprint && Entry.bIsMap)
		{
			// Maps are never loaded by the update: opening a level changes the editor world.
			Report.SkippedBlueprints.Add(FText::Format(LOCTEXT("Update.MapSkipped", "{0} (level Blueprint, open the level to update it)"),
				FText::FromString(Entry.DisplayName)).ToString());
			continue;
		}
		if (!Blueprint && bLoadFromDisk)
		{
			Blueprint = Cast<UBlueprint>(Entry.ObjectPath.TryLoad());
			if (!Blueprint)
			{
				UE_LOG(LogNodeFactory, Warning, TEXT("Blueprint %s could not be loaded for the update."), *Entry.ObjectPath.ToString());
				Report.FailedBlueprints.Add(Entry.DisplayName);
				continue;
			}
		}
		if (!Blueprint)
		{
			Report.SkippedBlueprints.Add(Entry.DisplayName);
			continue;
		}
		ToUpdate.AddUnique(Blueprint);
	}

	if (Report.bCancelled)
	{
		// Cancel during loading changes nothing.
		for (const UBlueprint* Blueprint : ToUpdate)
		{
			Report.SkippedBlueprints.Add(Blueprint->GetName());
		}
		for (; Index < Blueprints.Num(); ++Index)
		{
			Report.SkippedBlueprints.Add(Blueprints[Index].DisplayName);
		}
		return Report;
	}

	// 2. Check out (spec 9.1, item 3).
	if (bCheckOut && ToUpdate.Num() > 0)
	{
		CheckOutBlueprints(ToUpdate, Report);
	}

	// 3. Rebuild in one transaction: one Undo restores every Blueprint (spec 9.4). Nothing is saved.
	if (ToUpdate.Num() > 0)
	{
		const FScopedTransaction Transaction(LOCTEXT("Update.Transaction", "Update Custom Nodes"));
		for (UBlueprint* Blueprint : ToUpdate)
		{
			SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Update.Updating", "Updating {0}"), FText::FromString(Blueprint->GetName())));
			if (Report.bCancelled || SlowTask.ShouldCancel())
			{
				Report.bCancelled = true;
				Report.SkippedBlueprints.Add(Blueprint->GetName());
				continue;
			}
			UpdateBlueprint(Blueprint, Assets, bOnlyOutdated, Report);
		}
	}
	return Report;
}

int32 FCNAUpdateService::UpdateBlueprint(UBlueprint* Blueprint, const TArray<UCustomNodeAsset*>& Assets, bool bOnlyOutdated, FCNAUpdateReport& InOutReport)
{
	if (!Blueprint)
	{
		return 0;
	}

	TArray<UK2Node_CustomNode*> Nodes;
	FindInstances(Blueprint, Assets, bOnlyOutdated, Nodes);
	if (Nodes.Num() == 0)
	{
		return 0;
	}

	Blueprint->Modify();
	for (UK2Node_CustomNode* Node : Nodes)
	{
		Node->Modify();
		Node->ReconstructNode();

		// Each loss was reported as CNA016 by the node; the summary lists them again (spec 9.1, item 6).
		for (const FName& PinName : Node->GetLastLostWirePins())
		{
			InOutReport.LostWires.Add(FString::Printf(TEXT("%s: %s.%s"), *Blueprint->GetName(), *FCNAGraphUtils::GetNodeName(Node), *PinName.ToString()));
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	InOutReport.NumNodes += Nodes.Num();
	++InOutReport.NumBlueprints;

	CNA_VERBOSE(TEXT("Rebuilt %d custom node(s) in %s."), Nodes.Num(), *Blueprint->GetName());
	return Nodes.Num();
}

FCNAUpdateReport FCNAUpdateService::RecompileNode(UCustomNodeAsset* Asset)
{
	FCNAUpdateReport Report;
	if (!Asset || IsUpdating())
	{
		return Report;
	}

	FCNAGraphUtils::RefreshCachedAssetData(Asset);
	{
		const FScopedTransaction Transaction(LOCTEXT("Update.RecompileTransaction", "Recompile Node"));
		RefreshAssetVersion(Asset);
	}

	// The asset class holds the Behavior; its compilation also runs the validation of the asset (spec 10.2).
	FKismetEditorUtilities::CompileBlueprint(Asset);
	Asset->NotifyAssetChanged(NAME_None);

	return RequestUpdate(Asset, ECNAUpdateTrigger::RecompileCommand);
}

void FCNAUpdateService::HandleBlueprintOpened(UBlueprint* Blueprint)
{
	using namespace CNAUpdateServiceLocal;
	if (!Blueprint || Blueprint->IsPendingKill() || bUpdating || bDialogOpen)
	{
		return;
	}

	TArray<UK2Node_CustomNode*> Outdated;
	FindInstances(Blueprint, TArray<UCustomNodeAsset*>(), /*bOnlyOutdated*/ true, Outdated);
	if (Outdated.Num() == 0)
	{
		return;
	}

	TArray<UCustomNodeAsset*> Assets;
	for (const UK2Node_CustomNode* Node : Outdated)
	{
		if (UCustomNodeAsset* NodeAsset = Node->GetCustomNodeAsset())
		{
			Assets.AddUnique(NodeAsset);
		}
	}

	TArray<FCNAReferencingBlueprint> Blueprints;
	FCNAReferencingBlueprint& Entry = Blueprints[Blueprints.AddDefaulted()];
	Entry.ObjectPath = FSoftObjectPath(Blueprint);
	Entry.PackageName = Blueprint->GetOutermost()->GetFName();
	Entry.DisplayName = Blueprint->GetName();
	Entry.Loaded = Blueprint;
	Entry.bIsMap = Blueprint->IsA<ULevelScriptBlueprint>();

	bool bLoadFromDisk = false;
	bool bCheckOut = false;
	if (!AskUser(Assets, Blueprints, ECNAUpdateTrigger::BlueprintOpened, bLoadFromDisk, bCheckOut))
	{
		FCNAMessages::LogInfo(FText::Format(LOCTEXT("Update.OpenedKept", "{0} keeps {1} outdated custom {1}|plural(one=node,other=nodes) (CNA006)."),
			FText::FromString(Blueprint->GetName()), FText::AsNumber(Outdated.Num())), Blueprint);
		return;
	}

	FCNAMessages::NewPage(FText::Format(LOCTEXT("Update.PageTitle", "Update of {0}"), DescribeAssets(Assets)));
	const FCNAUpdateReport Report = UpdateBlueprints(Assets, Blueprints, /*bLoadFromDisk*/ false, bCheckOut, /*bOnlyOutdated*/ true);
	ReportToLog(Assets, Report, ECNAUpdateTrigger::BlueprintOpened);
}

// Messages

void FCNAUpdateService::ReportToLog(const TArray<UCustomNodeAsset*>& Assets, const FCNAUpdateReport& Report, ECNAUpdateTrigger Trigger)
{
	const UObject* Context = Assets.Num() > 0 ? Assets[0] : nullptr;
	const FText AssetNames = DescribeAssets(Assets);
	const FText Summary = FText::Format(FText::FromString(TEXT("{0}: {1}")), AssetNames, Report.ToText());

	if (Report.HasProblems())
	{
		FCNAMessages::ReportTextToLogWithSeverity(EMessageSeverity::Warning, Summary, Context);
	}
	else
	{
		FCNAMessages::LogInfo(Summary, Context);
	}

	if (Report.LostWires.Num() > 0)
	{
		FCNAMessages::ReportTextToLogWithSeverity(EMessageSeverity::Warning,
			FText::Format(LOCTEXT("Update.LostList", "Lost wires (CNA016): {0}"), FText::FromString(FCNAGraphUtils::JoinNames(Report.LostWires, 20))), Context);
	}
	if (Report.FailedBlueprints.Num() > 0)
	{
		FCNAMessages::ReportTextToLogWithSeverity(EMessageSeverity::Warning,
			FText::Format(LOCTEXT("Update.FailedList", "Not updated: {0}"), FText::FromString(FCNAGraphUtils::JoinNames(Report.FailedBlueprints, 20))), Context);
	}
	if (Report.SkippedBlueprints.Num() > 0)
	{
		FCNAMessages::LogInfo(FText::Format(LOCTEXT("Update.SkippedList", "Left outdated, the compiler reports CNA006 until they are updated: {0}"),
			FText::FromString(FCNAGraphUtils::JoinNames(Report.SkippedBlueprints, 20))), Context);
	}

	if (Report.LostWires.Num() > 0)
	{
		FCNAMessages::OpenLog(EMessageSeverity::Warning);
	}

	// Notification in the corner of the editor with a link to the log.
	if (!FSlateApplication::IsInitialized() || FApp::IsUnattended() || GIsAutomationTesting)
	{
		return;
	}
	if (!Report.HasChanges() && !Report.HasProblems() && Trigger != ECNAUpdateTrigger::RecompileCommand)
	{
		return;
	}
	FNotificationInfo Info(Summary);
	Info.ExpireDuration = Report.HasProblems() ? 8.0f : 4.0f;
	Info.bFireAndForget = true;
	Info.Hyperlink = FSimpleDelegate::CreateLambda([]()
	{
		FCNAMessages::OpenLog(EMessageSeverity::Info);
	});
	Info.HyperlinkText = LOCTEXT("Update.ShowLog", "Show Message Log");
	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(Report.HasProblems() ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
	}
}

void FCNAUpdateService::RequestWidgetRefresh(const UCustomNodeAsset* Asset)
{
	using namespace CNAUpdateServiceLocal;
	if (!Asset || !bStarted)
	{
		return;
	}
	PendingWidgetAssets.AddUnique(const_cast<UCustomNodeAsset*>(Asset));

	// SetTimer on an active handle restarts it: a burst of changes (a color picker) causes one redraw.
	if (FTimerManager* TimerManager = GetTimerManager())
	{
		TimerManager->SetTimer(WidgetTimer, FTimerDelegate::CreateStatic(&RefreshPendingWidgets), WidgetRefreshDelaySeconds, false);
	}
}

FText FCNAUpdateService::DescribeAssets(const TArray<UCustomNodeAsset*>& Assets)
{
	TArray<FString> Names;
	for (const UCustomNodeAsset* Asset : Assets)
	{
		if (Asset)
		{
			Names.Add(Asset->GetName());
		}
	}
	return FText::FromString(FCNAGraphUtils::JoinNames(Names, 3));
}

#undef LOCTEXT_NAMESPACE
