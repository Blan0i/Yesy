// Copyright (c) NodeFactory. Confirmation window of the update of node instances (spec 9.1, item 3).

#include "SCNAUpdateDialog.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

void SCNAUpdateDialog::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;
	bSourceControlEnabled = InArgs._bSourceControlEnabled;
	bCheckOut = bSourceControlEnabled;

	for (const FCNAUpdateDialogItem& Item : InArgs._Items)
	{
		ListItems.Add(MakeShareable(new FCNAUpdateDialogItem(Item)));
		if (Item.bLoaded)
		{
			++NumLoaded;
		}
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(FMargin(12.0f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Font(FEditorStyle::GetFontStyle(TEXT("BoldFont")))
				.Text(FText::Format(LOCTEXT("UpdateDialog.Title", "Custom Node Asset {0} changed"), InArgs._AssetName))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(InArgs._Reason.IsEmpty()
					? LOCTEXT("UpdateDialog.DefaultReason", "The nodes of this asset in the Blueprints below are outdated. Updating rebuilds their pins; wires are kept by name, by the pin redirects and by type and position.")
					: InArgs._Reason)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(this, &SCNAUpdateDialog::GetSummaryText)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush(TEXT("ToolPanel.DarkGroupBorder")))
				.Padding(FMargin(2.0f))
				[
					SNew(SListView<TSharedPtr<FCNAUpdateDialogItem>>)
					.ListItemsSource(&ListItems)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SCNAUpdateDialog::OnGenerateRow)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SCheckBox)
				.Visibility(bSourceControlEnabled ? EVisibility::Visible : EVisibility::Collapsed)
				.IsChecked_Lambda([this]() { return bCheckOut ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bCheckOut = NewState == ECheckBoxState::Checked; })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UpdateDialog.CheckOut", "Check out the Blueprints from source control"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
					.ForegroundColor(FLinearColor::White)
					.ContentPadding(FMargin(10.0f, 3.0f))
					.ToolTipText(LOCTEXT("UpdateDialog.UpdateAllTooltip", "Load every listed Blueprint and update its nodes. Nothing is saved automatically."))
					.OnClicked(this, &SCNAUpdateDialog::OnButtonClicked, ECNAUpdateChoice::UpdateAll)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("UpdateDialog.UpdateAll", "Update All"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(10.0f, 3.0f))
					.ToolTipText(LOCTEXT("UpdateDialog.SkipTooltip", "Update only the Blueprints that are already loaded; Blueprints on disk are not loaded and keep outdated nodes (CNA006)."))
					.OnClicked(this, &SCNAUpdateDialog::OnButtonClicked, ECNAUpdateChoice::Skip)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("UpdateDialog.Skip", "Skip"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(10.0f, 3.0f))
					.ToolTipText(LOCTEXT("UpdateDialog.CancelTooltip", "Change nothing. The nodes stay outdated and the compiler reports CNA006."))
					.OnClicked(this, &SCNAUpdateDialog::OnButtonClicked, ECNAUpdateChoice::Cancel)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("UpdateDialog.Cancel", "Cancel"))
					]
				]
			]
		]
	];
}

TSharedRef<ITableRow> SCNAUpdateDialog::OnGenerateRow(TSharedPtr<FCNAUpdateDialogItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FText Status = Item->bLoaded ? LOCTEXT("UpdateDialog.Loaded", "loaded") : LOCTEXT("UpdateDialog.OnDisk", "on disk");
	return SNew(STableRow<TSharedPtr<FCNAUpdateDialogItem>>, OwnerTable)
		.Padding(FMargin(4.0f, 1.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Item->Name))
				.ToolTipText(FText::FromString(Item->Path))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(Status)
			]
		];
}

FText SCNAUpdateDialog::GetSummaryText() const
{
	return FText::Format(LOCTEXT("UpdateDialog.Summary", "{0} Blueprint(s), {1} of them loaded:"), FText::AsNumber(ListItems.Num()), FText::AsNumber(NumLoaded));
}

FReply SCNAUpdateDialog::OnButtonClicked(ECNAUpdateChoice InChoice)
{
	Choice = InChoice;
	if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
	return FReply::Handled();
}

ECNAUpdateChoice SCNAUpdateDialog::ShowModal(const FText& AssetName, const FText& Reason, const TArray<FCNAUpdateDialogItem>& Items, bool& bOutCheckOut)
{
	bOutCheckOut = false;
	if (FApp::IsUnattended() || GIsAutomationTesting || !FSlateApplication::IsInitialized())
	{
		return ECNAUpdateChoice::UpdateAll;
	}

	bool bSourceControl = false;
	{
		// The module is a private dependency; it is loaded with the editor.
		static const FName SourceControlModuleName(TEXT("SourceControl"));
		if (FModuleManager::Get().IsModuleLoaded(SourceControlModuleName))
		{
			ISourceControlModule& SourceControl = FModuleManager::GetModuleChecked<ISourceControlModule>(SourceControlModuleName);
			bSourceControl = SourceControl.IsEnabled() && SourceControl.GetProvider().IsAvailable();
		}
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("UpdateDialog.WindowTitle", "Update Custom Nodes"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(560.0f, 420.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	TSharedRef<SCNAUpdateDialog> Dialog = SNew(SCNAUpdateDialog)
		.AssetName(AssetName)
		.Reason(Reason)
		.Items(Items)
		.bSourceControlEnabled(bSourceControl)
		.ParentWindow(Window);
	Window->SetContent(Dialog);

	const TSharedPtr<SWindow> RootWindow = FGlobalTabmanager::Get()->GetRootWindow();
	FSlateApplication::Get().AddModalWindow(Window, RootWindow);

	bOutCheckOut = bSourceControl && Dialog->ShouldCheckOut();
	return Dialog->GetChoice();
}

#undef LOCTEXT_NAMESPACE
