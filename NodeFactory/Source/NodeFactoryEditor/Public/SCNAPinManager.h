// Copyright (c) NodeFactory. Pin Manager of the asset window (spec 7.1, 4.1, 4.2).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "EdGraph/EdGraphPin.h"
#include "EditorUndoClient.h"

class UCustomNodeAsset;
class UK2Node_Tunnel;
class UBlueprint;
class SSearchBox;
class FUICommandList;
struct FUserPinInfo;

/** Visibility flags of a pin that the Pin Manager edits directly (spec 4.2). */
enum class ECNAPinFlag : uint8
{
	Advanced,
	Hidden,
	Required
};

/** Setting of the asset that names a pin the macro Execute does not have (CNA014, CNA015). */
enum class ECNAStaleReferenceKind : uint8
{
	/** PinOverrides[Index].PinName */
	PinOverride,
	/** PinOverrides[Index].PassThroughOf */
	PassThrough,
	/** BodyWidgets[Index].PinName (not Curve widgets: their PinName is only the name of the curve) */
	BodyWidget,
	/** PinRedirects[Index].NewName, after following the chain */
	Redirect
};

struct NODEFACTORYEDITOR_API FCNAStaleReference
{
	ECNAStaleReferenceKind Kind = ECNAStaleReferenceKind::PinOverride;
	int32 Index = INDEX_NONE;
	/** The name that does not exist. */
	FName Name;

	/** "Pin Override Durati", "Body Widget Slider Duration"... for menus. */
	FText Describe() const;
};

/** Names and types of the signature pins of both sides, to recognise a rename made outside the Pin Manager. */
struct NODEFACTORYEDITOR_API FCNASignatureSnapshot
{
	TArray<FName> InputNames;
	TArray<FString> InputTypes;
	TArray<FName> OutputNames;
	TArray<FString> OutputTypes;
	bool bValid = false;
};

/**
 * Operations on the signature of an asset. The signature lives on Tunnel Input (inputs of the node) and Tunnel
 * Output (outputs of the node) of the macro Execute (spec 4.1). "Side" is always the side of the node:
 * EGPD_Input = a pin of Tunnel Input, EGPD_Output = a pin of Tunnel Output.
 *
 * Every operation runs in its own transaction, modifies the asset and the tunnel and marks the asset as
 * structurally modified, so the instances are updated on save or Recompile Node (spec 9.1, 9.4).
 * The operations are public so that the automation tests use the same code as the panel (spec 11.1).
 */
struct NODEFACTORYEDITOR_API FCNAPinManagerOps
{
	/** Tunnel Input for EGPD_Input, Tunnel Output for EGPD_Output. */
	static UK2Node_Tunnel* GetTunnel(const UCustomNodeAsset* Asset, EEdGraphPinDirection Side);

	/** User pin description on the tunnel of the side. */
	static TSharedPtr<FUserPinInfo> FindUserPin(const UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName);

	/** Pin of the tunnel node that belongs to the signature pin. */
	static UEdGraphPin* FindTunnelPin(const UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName);

	/** A pin name is used on either side of the node (names are unique across both sides). */
	static bool IsPinNameUsed(const UCustomNodeAsset* Asset, FName PinName);

	/** "Base", "Base1", "Base2"... unused on both sides. */
	static FName MakeUniquePinName(const UCustomNodeAsset* Asset, FName BaseName);

	/** Checks a new name for a pin; OldName may be None for a new pin. */
	static bool VerifyPinName(const UCustomNodeAsset* Asset, FName OldName, const FString& NewName, FText& OutError);

	/** Adds a pin; returns its name or None. */
	static FName AddPin(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, const FEdGraphPinType& Type, FName BaseName, FText* OutError = nullptr);

	/** Renames a pin, writes OldName -> NewName into PinRedirects and renames the references (V11). */
	static bool RenamePin(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName OldName, FName NewName, FText& OutError);

	/** Changes the type and resets the default value. */
	static bool ChangePinType(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName, const FEdGraphPinType& NewType, FText& OutError);

	/** Removes a pin, its wires inside the Execution Graph, its override and its body widgets. */
	static bool RemovePin(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName);

	/** Moves a pin to another position on its side; the pins of the node follow this order. */
	static bool MovePin(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName, int32 NewIndex);

	/** Default value of an input pin (string form of the K2 schema). */
	static bool SetPinDefault(UCustomNodeAsset* Asset, FName PinName, const FString& NewValue);

	/** Advanced, Hidden or Required in the pin override; an override without settings is removed. */
	static bool SetPinFlag(UCustomNodeAsset* Asset, FName PinName, ECNAPinFlag Flag, bool bValue);
	static bool GetPinFlag(const UCustomNodeAsset* Asset, FName PinName, ECNAPinFlag Flag);

	/**
	 * The default value widgets edit the pin of Tunnel Input; this copies the values into the user pin
	 * descriptions so that both stay equal. Returns true when something changed. Not called while transacting.
	 */
	static bool SyncTunnelDefaults(UCustomNodeAsset* Asset);

	/** Problem of a pin for the status column: missing type object, override problems (CNA010, CNA011...). */
	static bool GetPinProblem(const UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName, FText& OutProblem);

	// Renames made elsewhere (the Details of Tunnel Input / Tunnel Output, My Blueprint)

	static void CaptureSignature(const UCustomNodeAsset* Asset, FCNASignatureSnapshot& OutSnapshot);

	/**
	 * Compares the signature with the snapshot. A pin that kept its side, position and type but changed its name
	 * was renamed: its references follow (redirect for the wires of the instances, overrides, body widgets,
	 * PassThroughOf, Behavior Graph literals) in one transaction. Nothing is changed while Undo / Redo runs.
	 * The snapshot is updated. Returns the number of renames that were followed.
	 */
	static int32 FollowExternalRenames(UCustomNodeAsset* Asset, FCNASignatureSnapshot& InOutSnapshot);

	// Stale references (settings that name a pin that does not exist)

	static void FindStaleReferences(const UCustomNodeAsset* Asset, TArray<FCNAStaleReference>& OutReferences);

	/** Pins a stale reference may be pointed to (inputs for body widgets and PassThroughOf, both sides otherwise). */
	static void GetRemapCandidates(const UCustomNodeAsset* Asset, const FCNAStaleReference& Reference, TArray<FName>& OutPins);

	/** Closest candidate by name ("Durati" -> "Duration"), None when nothing is close. */
	static FName SuggestRemap(const UCustomNodeAsset* Asset, const FCNAStaleReference& Reference);

	/** Points the reference to NewPin in one transaction. Fails when the target already has an override. */
	static bool RemapStaleReference(UCustomNodeAsset* Asset, const FCNAStaleReference& Reference, FName NewPin, FText& OutError);

	/** Removes the override, body widget or redirect, or clears PassThroughOf, in one transaction. */
	static bool RemoveStaleReference(UCustomNodeAsset* Asset, const FCNAStaleReference& Reference, FText& OutError);

private:
	static void AddRedirect(UCustomNodeAsset& Asset, FName OldName, FName NewName);
	static void RenameReferences(UCustomNodeAsset& Asset, FName OldName, FName NewName);
	/** Name literals of the Behavior Graph that name the pin (actions of UCNABehaviorBase, Switch on Name, ==). */
	static int32 RenameBehaviorReferences(UCustomNodeAsset& Asset, FName OldName, FName NewName, bool bApply);
	/** Something still names OldName or the redirect OldName -> NewName is missing. */
	static bool NeedsReferenceUpdate(const UCustomNodeAsset& Asset, FName OldName, FName NewName);
	static bool IsStaleReferenceCurrent(const UCustomNodeAsset* Asset, const FCNAStaleReference& Reference);
	static void FinishStructuralChange(UCustomNodeAsset* Asset, UK2Node_Tunnel* Tunnel);
};

/** One row of the Pin Manager. */
struct FCNAPinManagerItem
{
	FName PinName;
	EEdGraphPinDirection Side = EGPD_Input;
	FEdGraphPinType PinType;
	int32 Index = 0;

	/** Bound by the row to the inline name editor; executed by F2. */
	FSimpleDelegate OnRenameRequested;
};

typedef TSharedPtr<FCNAPinManagerItem> FCNAPinManagerItemPtr;

/**
 * Pin Manager (spec 7.1): two lists, inputs (Tunnel Input) and outputs (Tunnel Output), with the name (F2), the
 * type with containers (SPinTypeSelector), the default value (the default value widget of the K2 pin), the flags
 * Advanced, Hidden and Required and a status icon. Rows are reordered by drag and drop inside their list.
 * Ctrl+F focuses the filter, Delete removes the selected pin.
 */
class NODEFACTORYEDITOR_API SCNAPinManager : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SCNAPinManager) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UCustomNodeAsset* InAsset);
	virtual ~SCNAPinManager();

	/** Rebuilds the rows on the next frame when the signature or the flags changed. */
	void RequestRefresh(bool bForce = false);

	/** Ctrl+F: keyboard focus to the filter box. */
	void FocusSearch();

	UCustomNodeAsset* GetAsset() const { return Asset.Get(); }

	// SWidget
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// Used by the rows
	bool HandleVerifyName(const FText& NewText, FText& OutError, FCNAPinManagerItemPtr Item) const;
	void HandleNameCommitted(const FText& NewText, ETextCommit::Type CommitType, FCNAPinManagerItemPtr Item);
	FEdGraphPinType GetItemType(FCNAPinManagerItemPtr Item) const;
	void HandleTypeChanged(const FEdGraphPinType& NewType, FCNAPinManagerItemPtr Item);
	void HandleRemove(FCNAPinManagerItemPtr Item);
	void HandleMove(FCNAPinManagerItemPtr Item, FCNAPinManagerItemPtr Target, bool bBelow);
	bool IsEditingEnabled() const;

private:
	TSharedRef<SWidget> BuildSection(EEdGraphPinDirection Side);
	TSharedRef<class ITableRow> GenerateRow(FCNAPinManagerItemPtr Item, const TSharedRef<class STableViewBase>& OwnerTable);
	TSharedPtr<SWidget> MakeContextMenu();
	void HandleSelectionChanged(FCNAPinManagerItemPtr Item, ESelectInfo::Type SelectInfo, EEdGraphPinDirection Side);
	void HandleFilterChanged(const FText& NewText);

	EActiveTimerReturnType HandleRefreshTimer(double InCurrentTime, float InDeltaTime);
	void Refresh();
	FString BuildStateKey() const;
	bool PassesFilter(const FCNAPinManagerItem& Item) const;

	void HandleAssetChanged(UCustomNodeAsset* ChangedAsset, FName PropertyName);
	void HandleBlueprintChanged(UBlueprint* Blueprint);

	FCNAPinManagerItemPtr GetSelectedItem() const;
	bool HasSelection() const;
	void RenameSelected();
	void RemoveSelected();
	void MoveSelected(int32 Delta);
	void AddPin(EEdGraphPinDirection Side);
	FText GetSectionTitle(EEdGraphPinDirection Side) const;
	EVisibility GetProblemVisibility() const;

	TWeakObjectPtr<UCustomNodeAsset> Asset;
	TSharedPtr<FUICommandList> CommandList;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SListView<FCNAPinManagerItemPtr>> InputList;
	TSharedPtr<SListView<FCNAPinManagerItemPtr>> OutputList;
	TArray<FCNAPinManagerItemPtr> InputItems;
	TArray<FCNAPinManagerItemPtr> OutputItems;
	FString FilterText;
	FString LastStateKey;
	int32 NumInputs = 0;
	int32 NumOutputs = 0;
	bool bRefreshPending = false;
	bool bForceRefresh = false;
	bool bHasTunnels = true;
	FDelegateHandle AssetChangedHandle;
	FDelegateHandle BlueprintChangedHandle;
	FDelegateHandle BlueprintCompiledHandle;
};
