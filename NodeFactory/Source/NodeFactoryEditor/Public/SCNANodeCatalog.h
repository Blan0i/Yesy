// Copyright (c) NodeFactory. Node Catalog: presets in a folder, imported with one click (spec 8.3).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Framework/Docking/TabManager.h"

class SDockTab;
class SComboButton;
class UCustomNodeAsset;

/** One preset file; read with the header only, so the graphs are not parsed while browsing. */
struct FCNACatalogEntry
{
	FString FilePath;
	FString Name;
	FText DisplayName;
	FText Category;
	FString Description;
	FString Author;
	TArray<FString> Tags;
	bool bPure = false;
	/** Why the file cannot be imported (size, schema version, broken JSON). */
	FText Problem;

	FText GetTitle() const { return DisplayName.IsEmpty() ? FText::FromString(Name) : DisplayName; }
};

typedef TSharedPtr<FCNACatalogEntry> FCNACatalogEntryPtr;

/**
 * Node Catalog (spec 8.3): a nomad tab of the editor (Window, Tools). It scans the preset folder of the settings when
 * the tab opens or on Rescan, filters by name, category and tags and imports the selected preset into a Content
 * Browser folder; a taken name gets the suffix _1.
 */
class NODEFACTORYEDITOR_API SCNANodeCatalog : public SCompoundWidget
{
public:
	static const FName TabName;
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	/** Reads the headers of every *.json below Folder. */
	static void ScanFolder(const FString& Folder, TArray<FCNACatalogEntryPtr>& OutEntries);

	/** Name, display name, category or one of the tags contains every word of Filter. */
	static bool PassesFilter(const FCNACatalogEntry& Entry, const FString& Filter);

	SLATE_BEGIN_ARGS(SCNANodeCatalog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void Rescan();

private:
	TSharedRef<class ITableRow> GenerateRow(FCNACatalogEntryPtr Entry, const TSharedRef<class STableViewBase>& OwnerTable);
	void ApplyFilter();
	FCNACatalogEntryPtr GetSelectedEntry() const;
	bool CanImport() const;
	void ImportSelected();
	TSharedRef<SWidget> MakePathPicker();
	void HandlePathSelected(const FString& NewPath);
	FText GetSummaryText() const;
	FText GetDetailsText() const;

	TArray<FCNACatalogEntryPtr> AllEntries;
	TArray<FCNACatalogEntryPtr> FilteredEntries;
	TSharedPtr<SListView<FCNACatalogEntryPtr>> ListView;
	TSharedPtr<SComboButton> PathCombo;
	FString FilterText;
	FString TargetPath;
	FString ScannedFolder;
};
