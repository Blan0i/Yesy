// Copyright (c) NodeFactory. The Blueprint node of a Custom Node Asset (spec 2.4, 3, 4, 5, 6).

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/SoftObjectPath.h"
#include "CNATypes.h"
#include "K2Node_CustomNode.generated.h"

class UCustomNodeAsset;
class UEdGraph;
class UEdGraphPin;
class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class FCompilerResultsLog;
struct FCNASignaturePin;
struct FAssetData;

/**
 * Instance data written by Behavior actions (spec 2.3, 6.3). ReconstructNode applies it to the new pins,
 * so Behavior changes survive copy, paste, Undo and reloading.
 */
USTRUCT()
struct NODEFACTORYEDITOR_API FCNAInstanceOverride
{
	GENERATED_BODY()

	UPROPERTY()
	FName PinName;

	UPROPERTY()
	bool bOverrideVisibility = false;

	UPROPERTY()
	bool bVisible = true;

	UPROPERTY()
	bool bOverrideDisplayName = false;

	UPROPERTY()
	FText DisplayName;

	UPROPERTY()
	bool bOverrideRule = false;

	UPROPERTY()
	ECNAConnectionRule Rule = ECNAConnectionRule::None;

	UPROPERTY()
	TSubclassOf<UObject> ClassFilter;

	bool IsEmpty() const { return !bOverrideVisibility && !bOverrideDisplayName && !bOverrideRule; }
};

/**
 * Data of the menu template of an asset that is not loaded yet, read from the asset registry tags (spec 9.8).
 * Only template nodes of the action menu use it.
 */
struct NODEFACTORYEDITOR_API FCNATemplateData
{
	bool bValid = false;
	FText DisplayName;
	FText Category;
	FText Keywords;
	FText Tooltip;
	int32 AllowedGraphs = 15;
	FString ContextClassPath;
	bool bLatent = false;
	bool bPure = false;
	bool bDeprecated = false;

	/** Fills the data from the tags of a CNA asset. */
	void ReadFromAssetData(const FAssetData& AssetData);
};

/**
 * Node of a Custom Node Asset (spec D3). The pins repeat the signature of the macro Execute, the asset overrides
 * their properties, and ExpandNode replaces the node by a clone of the macro when a Blueprint is compiled.
 */
UCLASS(meta = (Keywords = "Custom Node NodeFactory CNA"))
class NODEFACTORYEDITOR_API UK2Node_CustomNode : public UK2Node
{
	GENERATED_BODY()

	friend class FCNABehaviorHost;
	friend class FCNAExpander;

public:
	UK2Node_CustomNode(const FObjectInitializer& ObjectInitializer);

	// Instance data (spec 2.3)

	/** The asset. A hard reference: a Blueprint that uses the node loads the asset with it. */
	UPROPERTY()
	UCustomNodeAsset* CNAAsset;

	/** Path of the asset, kept when the asset is deleted so CNA005 can name it (spec 2.4, item 6). */
	UPROPERTY()
	FSoftObjectPath AssetPath;

	/** AssetVersion the pins were built from; a difference means the node is outdated (CNA006). */
	UPROPERTY()
	int32 NodeVersion;

	/** Number of element pins of the dynamic groups (spec 4.4). Changing it rebuilds the node. */
	UPROPERTY(EditAnywhere, Category = "Custom Node", meta = (ClampMin = "0", ClampMax = "64", UIMin = "0", UIMax = "64"))
	int32 DynamicPinCount;

	/** Type shared by all Wildcard pins once a wire resolved it (spec 4.5). PinCategory None: unresolved. */
	UPROPERTY()
	FEdGraphPinType ResolvedWildcardType;

	/** Label of this instance, second line of the title (spec 3.1, bAllowInstanceLabel). */
	UPROPERTY(EditAnywhere, Category = "Custom Node")
	FText InstanceLabel;

	/** Title set by the Behavior action SetNodeTitle. */
	UPROPERTY()
	FText TitleOverride;

	/** Pins hidden, renamed or restricted by the Behavior (spec 6.3). */
	UPROPERTY()
	TArray<FCNAInstanceOverride> InstanceOverrides;

	/** Names of the pins that were split when the events OnPinSplit and OnPinRecombined were last raised (V15). */
	UPROPERTY()
	TArray<FName> SplitPinSnapshot;

	// Asset

	/** The asset, or the asset at AssetPath when it is loaded under that path. */
	UCustomNodeAsset* GetCustomNodeAsset() const;

	/** Binds the node to an asset (spawner, tests). Does not rebuild the pins. */
	void SetCustomNodeAsset(UCustomNodeAsset* InAsset);

	/** Name for messages, also when the asset is missing. */
	FString GetAssetNameForMessages() const;

	/** NodeVersion differs from AssetVersion (CNA006). */
	bool IsOutdated() const;

	/** The node lives in the Live Preview of the asset window (spec D10). */
	bool IsPreviewNode() const { return bIsPreviewNode; }
	void SetPreviewNode(bool bPreview) { bIsPreviewNode = bPreview; }

	/** The Blueprint that owns the node is compiling right now. */
	bool IsBeingCompiled() const;

	/** Template of the action menu with data from the registry tags. */
	void InitTemplateData(const FCNATemplateData& InData) { TemplateData = InData; }
	const FCNATemplateData& GetTemplateData() const { return TemplateData; }

	// Dynamic pins (spec 4.4)

	bool HasDynamicPins() const;

	/** Range shared by all dynamic groups of the asset. */
	void GetDynamicRange(int32& OutMin, int32& OutMax) const;

	/** Current number of element pins (0 without dynamic groups). */
	int32 GetDynamicPinCount() const;

	/** Name of element pin Index of the first dynamic group, NAME_None when out of range. */
	FName GetDynamicPinName(int32 Index) const;

	bool CanAddDynamicElement() const;
	bool CanRemoveDynamicElement() const;

	/** The "+" button and the context menu (spec 4.4, items 2 and 3). Each call is one transaction. */
	void AddDynamicElement();
	void RemoveDynamicElement(int32 Index);
	void RemoveDynamicElementPin(UEdGraphPin* Pin);

	/** Sets the count, rebuilds the node and raises OnAddPin or OnRemovePin. */
	void SetDynamicPinCount(int32 NewCount, bool bFireEvents);

	// Wildcard (spec 4.5)

	bool HasWildcardPins() const;
	bool IsWildcardResolved() const { return !ResolvedWildcardType.PinCategory.IsNone(); }

	/** The pin is Wildcard in the signature of the macro (it may already have a resolved type). */
	bool IsSignatureWildcardPin(const UEdGraphPin* Pin) const;

	/** Applies ResolvedWildcardType (or Wildcard) to every Wildcard pin and breaks wires that no longer fit. */
	void RefreshWildcardTypes();

	// Appearance used by the widgets (spec 3.2, 4.7)

	const FCNAInstanceOverride* FindInstanceOverride(FName PinName) const;
	FCNAInstanceOverride& FindOrAddInstanceOverride(FName PinName);

	/** Removes everything the Behavior wrote into the instance and rebuilds the node. */
	void ResetInstanceOverrides();

	/** Rule of the instance, then of the asset (spec 6.3, SetPinConnectionRule). */
	ECNAConnectionRule GetEffectiveConnectionRule(const UEdGraphPin* Pin, UClass*& OutClassFilter) const;

	/** Own color of the pin from its override; false when the type color applies. */
	bool GetPinColorOverride(const UEdGraphPin* Pin, FLinearColor& OutColor) const;

	/** Label color of the pin: LabelColor of its override or PinLabelColor of the asset. */
	FLinearColor GetPinLabelColor(const UEdGraphPin* Pin) const;

	/** Body widget that replaces the value field of the pin, if any. Curve widgets belong to no pin. */
	const FCNABodyWidget* FindBodyWidget(FName PinName) const;

	// Curves

	/** Pin of a curve widget (CNATypes::MakeCurvePinName), nullptr when the mode of the curve has no such role. */
	UEdGraphPin* FindCurvePin(const FGuid& WidgetGuid, ECNACurvePinRole Role) const;

	/** Title without the second line. */
	FText GetMainTitle() const;

	/** Second line of the title: Subtitle of the asset. */
	FText GetSubtitle() const;

	/** Advanced arrow of SGraphNode_NodeFactory (spec 6.2, OnAdvancedViewToggled). */
	void SetAdvancedViewExpanded(bool bExpanded);

	// Behavior actions (spec 6.3); called by UCNABehaviorBase while an event runs

	bool SetPinVisibilityFromBehavior(FName PinName, bool bVisible, FText& OutError);
	bool RenamePinFromBehavior(FName PinName, const FText& NewDisplayName, FText& OutError);
	bool ChangeWildcardTypeFromBehavior(FName PinName, const FEdGraphPinType& NewType, FText& OutError);
	void SetTitleOverrideFromBehavior(const FText& NewTitle);
	bool SetPinConnectionRuleFromBehavior(FName PinName, ECNAConnectionRule Rule, UClass* ClassFilter, FText& OutError);

	/** Red or yellow frame with a message until the next event (V22). */
	void SetBehaviorMessage(EMessageSeverity::Type Severity, const FString& Message);
	void ClearBehaviorMessage();

	/** Changes made inside one event are collected and the graph is notified once at the end. */
	void BeginBehaviorChanges();
	void EndBehaviorChanges();

	/** Compares the split pins with SplitPinSnapshot and raises OnPinSplit and OnPinRecombined (V15). */
	void RefreshSplitPinState(bool bFireEvents);

	// Compilation and updates

	/** Assets of the custom nodes that are being expanded around this node (cycle detection, spec 5.2). */
	const TArray<const UCustomNodeAsset*>& GetExpansionStack() const { return ExpansionStack; }

	/** Pins whose wires were lost by the last ReconstructNode (CNA016, spec 9.1). */
	const TArray<FName>& GetLastLostWirePins() const { return LastLostWirePins; }

	/** Rebuilds the node inside a transaction and marks the Blueprint as modified (Refresh Node). */
	void RefreshNode();

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual void ReconstructNode() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetKeywords() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual bool IsDeprecated() const override;
	virtual bool ShouldWarnOnDeprecation() const override { return false; }
	virtual FString GetDeprecationMessage() const override;
	virtual bool IsCompatibleWithGraph(const UEdGraph* TargetGraph) const override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PostPlacedNewNode() override;
	virtual void PostPasteNode() override;
	virtual void ValidateNodeDuringCompilation(FCompilerResultsLog& MessageLog) const override;
	virtual void GetContextMenuActions(const FGraphNodeContextMenuBuilder& Context) const override;
	virtual UObject* GetJumpTargetForDoubleClick() const override;
	virtual void OnRenameNode(const FString& NewName) override;
	virtual TSharedPtr<class INameValidatorInterface> MakeNameValidator() const override;
	virtual void DestroyNode() override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;
	virtual bool ShouldShowNodeProperties() const override { return true; }

	// UK2Node
	virtual bool IsNodePure() const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual FName GetCornerIcon() const override;
	virtual bool ShouldDrawCompact() const override;
	virtual FText GetCompactNodeTitle() const override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual UK2Node::ERedirectType DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const override;
	virtual void GetRedirectPinNames(const UEdGraphPin& Pin, TArray<FString>& RedirectPinNames) const override;
	virtual void PostReconstructNode() override;
	virtual bool HasExternalDependencies(TArray<class UStruct*>* OptionalOutput = nullptr) const override;
	virtual bool NodeCausesStructuralBlueprintChange() const override { return false; }

	// UObject
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;

private:
	/** Creates the pins of one signature entry; dynamic groups create their element pins. */
	void CreatePinsForSignaturePin(const UCustomNodeAsset& Asset, const FCNASignaturePin& SignaturePin);

	/**
	 * Pins of the curve widgets of one direction, exec or data. Names are stable (Guid + role), the label of the
	 * role is only the display name.
	 */
	void CreateCurvePins(const UCustomNodeAsset& Asset, EEdGraphPinDirection Direction, bool bExecPins);

	/**
	 * Pin that takes over the wires of an old pin when a Playback curve is switched on or off: the first exec input
	 * of the macro <-> Play, its first exec output <-> Update. NAME_None when nothing takes over.
	 */
	FName GetCurveTakeoverPinName(FName OldName, EEdGraphPinDirection Direction) const;

	/** Display name, tooltip, Advanced and hidden state from the asset override and the instance override. */
	void ApplyPinOverrides(const UCustomNodeAsset& Asset, UEdGraphPin* Pin, const FCNAPinOverride* Override, int32 DynamicIndex);

	/** Pins of a signature entry: one pin, or every element pin of a dynamic group. */
	void GetPinsOfSignaturePin(FName SignatureName, TArray<UEdGraphPin*>& OutPins) const;

	/** Recomputes ResolvedWildcardType from the wires of the Wildcard pins. Returns true if it changed. */
	bool UpdateResolvedWildcardFromLinks();

	/** Element type to store in ResolvedWildcardType when a Wildcard pin is connected to OtherPin. */
	static FEdGraphPinType MakeResolvedTypeFrom(const UEdGraphPin* OtherPin);

	/** Number of element pins of the first dynamic group that exist right now. */
	int32 CountElementPins() const;

	/** Name of element pin Index of every dynamic group. */
	void GetElementPinNames(int32 Index, TArray<FName>& OutNames) const;

	/** Name the redirects of the asset give an old pin name; dynamic element pins keep their index. */
	FName RedirectPinName(FName OldName) const;

	/** Raises the Behavior event OnNodePlaced (deferred when the pins do not exist yet). */
	void FireNodePlaced();

	/** Development Only state of the node (spec 9.2, V5). bOnlyIfNotUserSet keeps a state the user chose. */
	void ApplyDevelopmentOnlyState(bool bDevelopmentOnly, bool bOnlyIfNotUserSet = false);

	/** Wires of all pins before a rebuild, compared afterwards for CNA016. */
	struct FLinkSnapshot
	{
		FName PinName;
		EEdGraphPinDirection Direction = EGPD_Input;
		TArray<TPair<TWeakObjectPtr<UEdGraphNode>, FName>> Links;
	};
	void TakeLinkSnapshot(TArray<FLinkSnapshot>& OutSnapshot) const;
	void ReportLostLinks(const TArray<FLinkSnapshot>& Snapshot);

	/** Template data of the menu (not saved). */
	FCNATemplateData TemplateData;

	/** Old pin name -> new pin name matched by type and position during the current reconstruction (spec 9.1). */
	TMap<FName, FName> ReconstructPositionalMatches;

	/** Pins whose wires the last rebuild lost. */
	TArray<FName> LastLostWirePins;

	/** Assets that are being expanded around this node; set on the clones of nested nodes (spec 5.1, item 10). */
	TArray<const UCustomNodeAsset*> ExpansionStack;

	/** Re-entry lock of Behavior events (spec 6.1, 9.6). Set by FCNABehaviorHost. */
	bool bBehaviorLock;

	/** ReconstructNode is running; events of the rebuild are suppressed. */
	bool bReconstructing;

	/** The message on the node comes from ThrowNodeError or ThrowNodeWarning. */
	bool bBehaviorMessageActive;

	/** The node lives in the Live Preview graph. */
	bool bIsPreviewNode;

	/** Expansion already happened for this compiled copy (nested nodes are expanded right away). */
	bool bExpanded;

	/** OnNodePlaced waits for AllocateDefaultPins. */
	bool bPendingPlacedEvent;

	/** Depth of BeginBehaviorChanges / EndBehaviorChanges and whether pins changed in between. */
	int32 BehaviorChangeDepth;
	bool bBehaviorChangedPins;

	/** Validation of this compiled copy already reported its diagnostics (spec 5.1, item 1). */
	mutable bool bCompileDiagnosticsReported;
	mutable bool bCompileHasErrors;
};
