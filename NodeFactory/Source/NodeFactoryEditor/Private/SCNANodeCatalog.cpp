// Copyright (c) NodeFactory. Node Catalog: presets in a folder, imported with one click (spec 8.3).

#include "SCNANodeCatalog.h"
#include "CNAEditorSettings.h"
#include "CNAJsonImporter.h"
#include "CNAValidator.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryStyle.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "EditorStyleSet.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

const FName SCNANodeCatalog::TabName(TEXT("NodeFactoryCatalog"));

TSharedRef<SDockTab> SCNANodeCatalog::SpawnTab(const FSpawnTabArgs& Args)
{
	// The folder is scanned here, when the tab opens, not at editor start (spec 8.3).
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SCNANodeCatalog)
		];
}

void SCNANodeCatalog::ScanFolder(const FString& Folder, TArray<FCNACatalogEntryPtr>& OutEntries)
{
	TArray<FString> Files;
	if (!Folder.IsEmpty() && IFileManager::Get().DirectoryExists(*Folder))
	{
		IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.json"), /*Files*/ true, /*Directories*/ false);
	}
	Files.Sort();
	for (const FString& File : Files)
	{
		FCNACatalogEntryPtr Entry = MakeShareable(new FCNACatalogEntry);
		Entry->FilePath = File;
		Entry->Name = FPaths::GetBaseFilename(File);

		FCNAImportData Data;
		TArray<FText> Errors;
		if (FCNAJsonImporter::LoadFile(File, Data, Errors, /*bHeaderOnly*/ true))
		{
			if (!Data.Name.IsEmpty())
			{
				Entry->Name = Data.Name;
			}
			Entry->DisplayName = Data.DisplayName;
			Entry->Category = Data.Category;
			Entry->Description = Data.Description;
			Entry->Author = Data.Author;
			Entry->Tags = Data.Tags;
			Entry->bPure = Data.bPure;
		}
		else
		{
			FString Lines;
			for (const FText& Error : Errors)
			{
				Lines += (Lines.IsEmpty() ? TEXT("") : TEXT("\n")) + Error.ToString();
			}
			Entry->Problem = FText::FromString(Lines);
		}
		OutEntries.Add(Entry);
	}
}

bool SCNANodeCatalog::PassesFilter(const FCNACatalogEntry& Entry, const FString& Filter)
{
	TArray<FString> Words;
	Filter.ParseIntoArrayWS(Words);
	FString Haystack = Entry.Name + TEXT(" ") + Entry.DisplayName.ToString() + TEXT(" ") + Entry.Category.ToString();
	for (const FString& Tag : Entry.Tags)
	{
		Haystack += TEXT(" ") + Tag;
	}
	for (const FString& Word : Words)
	{
		if (!Haystack.Contains(Word))
		{
			return false;
		}
	}
	return true;
}

void SCNANodeCatalog::Construct(const FArguments& InArgs)
{
	TargetPath = TEXT("/Game/NodeFactory");

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("Catalog.SearchHint", "Search by name, category or tag"))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					FilterText = Text.ToString().TrimStartAndEnd();
					ApplyFilter();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Catalog.Rescan", "Rescan"))
				.ToolTipText(LOCTEXT("Catalog.RescanTip", "Read the preset folder again."))
				.OnClicked_Lambda([this]() { Rescan(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Catalog.OpenFolder", "Open Folder"))
				.ToolTipText(LOCTEXT("Catalog.OpenFolderTip", "Show the preset folder in the file explorer. The folder is set in Editor Preferences, NodeFactory."))
				.OnClicked_Lambda([this]()
				{
					IFileManager::Get().MakeDirectory(*ScannedFolder, /*Tree*/ true);
					FPlatformProcess::ExploreFolder(*ScannedFolder);
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Catalog.ValidateAll", "Validate All"))
				.ToolTipText(LOCTEXT("Catalog.ValidateAllTip", "Check every Custom Node Asset of the project and write the results into the Message Log NodeFactory."))
				.OnClicked_Lambda([]() { FCNAValidator::ValidateAllAssetsToLog(/*bOpenLog*/ true); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+ SSplitter::Slot()
			.Value(0.7f)
			[
				SAssignNew(ListView, SListView<FCNACatalogEntryPtr>)
				.ListItemsSource(&FilteredEntries)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SCNANodeCatalog::GenerateRow)
				.OnMouseButtonDoubleClick_Lambda([this](FCNACatalogEntryPtr) { ImportSelected(); })
			]
			+ SSplitter::Slot()
			.Value(0.3f)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
				.Padding(6.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(this, &SCNANodeCatalog::GetDetailsText)
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Catalog.TargetLabel", "Import into"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(PathCombo, SComboButton)
				.OnGetMenuContent(this, &SCNANodeCatalog::MakePathPicker)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return FText::FromString(TargetPath); })
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
				.ForegroundColor(FLinearColor::White)
				.Text(LOCTEXT("Catalog.Import", "Import"))
				.ToolTipText(LOCTEXT("Catalog.ImportTip", "Create a Custom Node Asset from the selected preset in the folder on the left; a taken name gets _1 (double click does the same)."))
				.IsEnabled(this, &SCNANodeCatalog::CanImport)
				.OnClicked_Lambda([this]() { ImportSelected(); return FReply::Handled(); })
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 0.0f, 6.0f, 4.0f)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(this, &SCNANodeCatalog::GetSummaryText)
		]
	];

	Rescan();
}

void SCNANodeCatalog::Rescan()
{
	ScannedFolder = GetDefault<UCNAEditorSettings>()->GetCatalogFolderAbsolute();
	AllEntries.Reset();
	ScanFolder(ScannedFolder, AllEntries);
	ApplyFilter();
}

void SCNANodeCatalog::ApplyFilter()
{
	FilteredEntries.Reset();
	for (const FCNACatalogEntryPtr& Entry : AllEntries)
	{
		if (Entry.IsValid() && (FilterText.IsEmpty() || PassesFilter(*Entry, FilterText)))
		{
			FilteredEntries.Add(Entry);
		}
	}
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SCNANodeCatalog::GenerateRow(FCNACatalogEntryPtr Entry, const TSharedRef<STableViewBase>& OwnerTable)
{
	FString Tags;
	for (const FString& Tag : Entry->Tags)
	{
		Tags += (Tags.IsEmpty() ? TEXT("") : TEXT(", ")) + Tag;
	}
	const FText Second = FText::Format(LOCTEXT("Catalog.RowDetails", "{0}  |  {1}  |  {2}"),
		Entry->Category.IsEmpty() ? LOCTEXT("Catalog.NoCategory", "No category") : Entry->Category,
		Entry->bPure ? LOCTEXT("Catalog.Pure", "Pure") : LOCTEXT("Catalog.Impure", "Impure"),
		FText::FromString(Tags));

	return SNew(STableRow<FCNACatalogEntryPtr>, OwnerTable)
		.Padding(FMargin(4.0f, 2.0f))
		.ToolTipText(FText::FromString(Entry->FilePath))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SImage)
				.Image(Entry->Problem.IsEmpty() ? FNodeFactoryStyle::FindBrush(FNodeFactoryStyle::ClassIconName) : FEditorStyle::GetBrush(TEXT("Icons.Warning")))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle(TEXT("BoldFont")))
					.Text(Entry->GetTitle())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(Second)
				]
			]
		];
}

FCNACatalogEntryPtr SCNANodeCatalog::GetSelectedEntry() const
{
	if (ListView.IsValid())
	{
		const TArray<FCNACatalogEntryPtr> Selection = ListView->GetSelectedItems();
		if (Selection.Num() > 0)
		{
			return Selection[0];
		}
	}
	return FCNACatalogEntryPtr();
}

bool SCNANodeCatalog::CanImport() const
{
	const FCNACatalogEntryPtr Entry = GetSelectedEntry();
	return Entry.IsValid() && Entry->Problem.IsEmpty() && !TargetPath.IsEmpty();
}

void SCNANodeCatalog::ImportSelected()
{
	if (!CanImport())
	{
		return;
	}
	const FCNACatalogEntryPtr Entry = GetSelectedEntry();
	TArray<FText> Errors;
	UCustomNodeAsset* Asset = FCNAJsonImporter::CreateAssetFromFile(Entry->FilePath, TargetPath, Errors);
	if (!Asset)
	{
		FString Lines;
		for (const FText& Error : Errors)
		{
			Lines += TEXT("\n") + Error.ToString();
		}
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("Catalog.ImportFailed", "{0} was not imported:{1}"),
			Entry->GetTitle(), FText::FromString(Lines)));
		return;
	}

	FNotificationInfo Info(FText::Format(LOCTEXT("Catalog.Imported", "Imported {0} as {1}"), Entry->GetTitle(), FText::FromString(Asset->GetPathName())));
	Info.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Info);

	TArray<UObject*> Assets;
	Assets.Add(Asset);
	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	ContentBrowser.Get().SyncBrowserToAssets(Assets);
}

TSharedRef<SWidget> SCNANodeCatalog::MakePathPicker()
{
	FPathPickerConfig Config;
	Config.DefaultPath = TargetPath;
	Config.OnPathSelected = FOnPathSelected::CreateSP(this, &SCNANodeCatalog::HandlePathSelected);
	Config.bAllowContextMenu = true;
	Config.bAllowClassesFolder = false;
	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	return SNew(SBox)
		.WidthOverride(300.0f)
		.HeightOverride(400.0f)
		[
			ContentBrowser.Get().CreatePathPicker(Config)
		];
}

void SCNANodeCatalog::HandlePathSelected(const FString& NewPath)
{
	TargetPath = NewPath;
	if (PathCombo.IsValid())
	{
		PathCombo->SetIsOpen(false);
	}
}

FText SCNANodeCatalog::GetSummaryText() const
{
	return FText::Format(LOCTEXT("Catalog.Summary", "{0} of {1} preset(s) in {2}"),
		FText::AsNumber(FilteredEntries.Num()), FText::AsNumber(AllEntries.Num()), FText::FromString(ScannedFolder));
}

FText SCNANodeCatalog::GetDetailsText() const
{
	const FCNACatalogEntryPtr Entry = GetSelectedEntry();
	if (!Entry.IsValid())
	{
		return AllEntries.Num() == 0
			? LOCTEXT("Catalog.Empty", "The preset folder has no JSON files. Export a node with Export JSON into this folder or change the folder in Editor Preferences, NodeFactory.")
			: LOCTEXT("Catalog.SelectHint", "Select a preset to see its description.");
	}
	if (!Entry->Problem.IsEmpty())
	{
		return FText::Format(LOCTEXT("Catalog.Problem", "{0}\n\nThe file cannot be imported:\n{1}"), Entry->GetTitle(), Entry->Problem);
	}
	return FText::Format(LOCTEXT("Catalog.Details", "{0}\n{1}\n\nAuthor: {2}\nFile: {3}"),
		Entry->GetTitle(), FText::FromString(Entry->Description),
		FText::FromString(Entry->Author.IsEmpty() ? FString(TEXT("-")) : Entry->Author), FText::FromString(Entry->FilePath));
}

#undef LOCTEXT_NAMESPACE
