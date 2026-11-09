// Copyright (c) NodeFactory. Local Variables panel of the asset window (spec 4.8, 7.1).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "EdGraph/EdGraphPin.h"
#include "EditorUndoClient.h"

class UCustomNodeAsset;
class UBlueprint;
class UK2Node_CNALocalVariableBase;
class SSearchBox;
class FUICommandList;
struct FCNALocalVariable;

/**
 * Operations on the local variables of an asset (spec 4.8). Every operation runs in its own transaction, keeps the
 * Get Local and Set Local nodes of the variable in sync and refreshes the node menu (the drop-down of the nodes).
 * Public so that the automation tests use the same code as the panel (spec 11.1).
 */
struct NODEFACTORYEDITOR_API FCNALocalVariableOps
{
	/** Index of the variable in UCustomNodeAsset::LocalVariables or INDEX_NONE. */
	static int32 FindIndex(const UCustomNodeAsset* Asset, const FGuid& Guid);

	/** Checks a name; Guid is the renamed variable or an invalid Guid for a new one. */
	static bool VerifyName(const UCustomNodeAsset* Asset, const FGuid& Guid, const FString& NewName, FText& OutError);

	/** Adds "Base", "Base1"... ("NewVar" for None) and returns the Guid, or an invalid Guid. */
	static FGuid Add(UCustomNodeAsset* Asset, FName BaseName, const FEdGraphPinType& Type);

	/** Renames the variable; its nodes follow because they reference the Guid. */
	static bool Rename(UCustomNodeAsset* Asset, const FGuid& Guid, FName NewName, FText& OutError);

	/** Changes the type and clears the default value; the nodes are rebuilt, incompatible wires become orphaned. */
	static bool ChangeType(UCustomNodeAsset* Asset, const FGuid& Guid, const FEdGraphPinType& NewType, FText& OutError);

	/** Default value in the text form of pin defaults (spec 4.8, item 4); validated against the type. */
	static bool SetDefault(UCustomNodeAsset* Asset, const FGuid& Guid, const FString& NewValue, FText* OutError = nullptr);

	static bool SetTooltip(UCustomNodeAsset* Asset, const FGuid& Guid, const FText& NewTooltip);

	/** Removes the variable; with bRemoveNodes its Get Local and Set Local nodes are deleted in the same transaction. */
	static bool Remove(UCustomNodeAsset* Asset, const FGuid& Guid, bool bRemoveNodes = true);

	/** Changes the order of the variables (the order of the drop-down of the nodes). */
	static bool Move(UCustomNodeAsset* Asset, const FGuid& Guid, int32 NewIndex);

	/** Get Local and Set Local nodes of the Execution Graph that reference the variable. */
	static void GetVariableNodes(const UCustomNodeAsset* Asset, const FGuid& Guid, TArray<UK2Node_CNALocalVariableBase*>& OutNodes);
	static int32 CountUses(const UCustomNodeAsset* Asset, const FGuid& Guid);

	/** Empty values are always valid: the variable then starts with the zero value of its type. */
	static bool IsDefaultValid(const FEdGraphPinType& Type, const FString& Value, FText* OutError = nullptr);

private:
	static void RefreshVariableNodes(const TArray<UK2Node_CNALocalVariableBase*>& Nodes, const FCNALocalVariable& Variable);
	static void Finish(UCustomNodeAsset* Asset);
};

/** One row of the panel; the Guid identifies the variable across refreshes. */
struct FCNALocalVariableItem
{
	FGuid Guid;
	FName Name;
	FEdGraphPinType Type;
	int32 Uses = 0;

	/** Bound by the row to the inline name editor; executed by F2. */
	FSimpleDelegate OnRenameRequested;
};

typedef TSharedPtr<FCNALocalVariableItem> FCNALocalVariableItemPtr;

/**
 * Local Variables (spec 4.8, 7.1): the name (F2), the type (SPinTypeSelector without Exec and Wildcard), the
 * default value, the tooltip and the number of Get Local and Set Local nodes. Ctrl+F filters, Delete removes,
 * Alt+Up and Alt+Down change the order.
 */
class NODEFACTORYEDITOR_API SCNALocalVariables : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SCNALocalVariables) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UCustomNodeAsset* InAsset);
	virtual ~SCNALocalVariables();

	/** Rebuilds the rows on the next frame when the variables or their uses changed. */
	void RequestRefresh(bool bForce = false);

	/** Ctrl+F: keyboard focus to the filter box. */
	void FocusSearch();

	UCustomNodeAsset* GetAsset() const { return Asset.Get(); }
	const FCNALocalVariable* FindVariable(const FGuid& Guid) const;
	bool IsEditingEnabled() const { return Asset.IsValid(); }

	/** Asks for confirmation when nodes use the variable, then removes it with its nodes. */
	void HandleRemove(FCNALocalVariableItemPtr Item);

	// SWidget
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override { RequestRefresh(true); }
	virtual void PostRedo(bool bSuccess) override { RequestRefresh(true); }

private:
	TSharedRef<class ITableRow> GenerateRow(FCNALocalVariableItemPtr Item, const TSharedRef<class STableViewBase>& OwnerTable);
	TSharedPtr<SWidget> MakeContextMenu();
	EActiveTimerReturnType HandleRefreshTimer(double InCurrentTime, float InDeltaTime);
	void Refresh();
	FString BuildStateKey() const;
	void HandleAssetChanged(UCustomNodeAsset* ChangedAsset, FName PropertyName);
	void HandleBlueprintChanged(UBlueprint* Blueprint);
	void HandleFilterChanged(const FText& NewText);
	FCNALocalVariableItemPtr GetSelectedItem() const;
	bool HasSelection() const;
	void AddVariable();
	void RenameSelected();
	void RemoveSelected();
	void MoveSelected(int32 Delta);
	FText GetSummaryText() const;
	EVisibility GetPureWarningVisibility() const;

	TWeakObjectPtr<UCustomNodeAsset> Asset;
	TSharedPtr<FUICommandList> CommandList;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<FCNALocalVariableItemPtr>> ListView;
	TArray<FCNALocalVariableItemPtr> Items;
	FString FilterText;
	FString LastStateKey;
	bool bRefreshPending = false;
	bool bForceRefresh = false;
	FDelegateHandle AssetChangedHandle;
	FDelegateHandle BlueprintChangedHandle;
};
