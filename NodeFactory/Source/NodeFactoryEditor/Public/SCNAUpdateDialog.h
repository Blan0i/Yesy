// Copyright (c) NodeFactory. Confirmation window of the update of node instances (spec 9.1, item 3).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SWindow;
class ITableRow;
class STableViewBase;

/** Answer of the confirmation window. */
enum class ECNAUpdateChoice : uint8
{
	/** Load every listed Blueprint and update its instances. */
	UpdateAll,
	/** Update only the Blueprints that are already in memory; nothing is loaded from disk. */
	Skip,
	/** Change nothing. */
	Cancel
};

/** One line of the list. */
struct FCNAUpdateDialogItem
{
	FString Name;
	FString Path;
	bool bLoaded = false;
};

/**
 * Lists the Blueprints that use a changed Custom Node Asset with the buttons Update All, Skip and Cancel and an
 * optional check out of the files when source control is connected. The window can be disabled in Editor
 * Preferences (bConfirmBulkUpdate).
 */
class NODEFACTORYEDITOR_API SCNAUpdateDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCNAUpdateDialog)
		: _bSourceControlEnabled(false)
	{}
		SLATE_ARGUMENT(FText, AssetName)
		SLATE_ARGUMENT(FText, Reason)
		SLATE_ARGUMENT(TArray<FCNAUpdateDialogItem>, Items)
		SLATE_ARGUMENT(bool, bSourceControlEnabled)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	ECNAUpdateChoice GetChoice() const { return Choice; }
	bool ShouldCheckOut() const { return bCheckOut; }

	/**
	 * Shows the window modally. Unattended sessions and automation tests get UpdateAll without a window.
	 * @param bOutCheckOut true when the user asked to check the files out.
	 */
	static ECNAUpdateChoice ShowModal(const FText& AssetName, const FText& Reason, const TArray<FCNAUpdateDialogItem>& Items, bool& bOutCheckOut);

private:
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FCNAUpdateDialogItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	FReply OnButtonClicked(ECNAUpdateChoice InChoice);
	FText GetSummaryText() const;

	TArray<TSharedPtr<FCNAUpdateDialogItem>> ListItems;
	TWeakPtr<SWindow> ParentWindow;
	ECNAUpdateChoice Choice = ECNAUpdateChoice::Cancel;
	bool bCheckOut = true;
	bool bSourceControlEnabled = false;
	int32 NumLoaded = 0;
};
