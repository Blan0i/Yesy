// Copyright (c) NodeFactory. The Custom Node Asset: a Blueprint subclass that describes one node (spec 2, 3).

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "UObject/SoftObjectPtr.h"
#include "CNATypes.h"
#include "CustomNodeAsset.generated.h"

class UEdGraph;
class UTexture2D;
class UCustomNodeAsset;

/**
 * UObject::IsDataValid(TArray<FText>&) and EDataValidationResult are expected in 4.20.3 for the Data Validation
 * plugin (V-check). Set to 0 if the engine does not declare them; Validate in the asset editor still works.
 */
#ifndef CNA_WITH_DATA_VALIDATION
#define CNA_WITH_DATA_VALIDATION 1
#endif

/** Asset registry tags of a Custom Node Asset (spec 7.2, V19). Visible tags show up in the Content Browser tooltip. */
namespace CNATags
{
	/** Visible: menu category. */
	NODEFACTORY_API extern const FName Category;
	/** Visible: "Pure" or "Impure". */
	NODEFACTORY_API extern const FName Purity;
	/** Visible: "None" or "Array of T". */
	NODEFACTORY_API extern const FName DynamicPins;
	/** Visible: description of the metadata. */
	NODEFACTORY_API extern const FName Description;
	/** Visible: AssetVersion. */
	NODEFACTORY_API extern const FName Version;

	/** Hidden tags read by the node menu without loading the asset (spec 9.8). */
	NODEFACTORY_API extern const FName DisplayName;
	NODEFACTORY_API extern const FName Keywords;
	NODEFACTORY_API extern const FName Tooltip;
	NODEFACTORY_API extern const FName Hidden;
	NODEFACTORY_API extern const FName AllowedGraphs;
	NODEFACTORY_API extern const FName ContextClass;
	NODEFACTORY_API extern const FName Latent;
	NODEFACTORY_API extern const FName Author;
	NODEFACTORY_API extern const FName Tags;
	NODEFACTORY_API extern const FName Deprecated;

	/** "True" or "False" as stored in the tags. */
	NODEFACTORY_API FString FromBool(bool bValue);
	NODEFACTORY_API bool ToBool(const FString& Value);
}

#if WITH_EDITOR
/**
 * Hooks the editor module installs on startup. The runtime module cannot depend on editor code, so validation,
 * change notifications and the parent class of the Behavior Graph are reached through these (spec 2.1).
 */
struct NODEFACTORY_API FCNAEditorHooks
{
	/** Full validation of the asset (spec 10.2). Returns false when an error was found; texts are appended. */
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FValidateAsset, UCustomNodeAsset* /*Asset*/, TArray<FText>& /*OutErrors*/);
	static FValidateAsset ValidateAsset;

	/** A property of the asset changed (Details, Undo, JSON import). Live Preview and the menu listen. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAssetChanged, UCustomNodeAsset* /*Asset*/, FName /*PropertyName*/);
	static FOnAssetChanged OnAssetChanged;

	/** The asset is saved by the editor (not cooked): signature hash, version and registry cache are refreshed. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssetPreSave, UCustomNodeAsset* /*Asset*/);
	static FOnAssetPreSave OnAssetPreSave;

	/** UCNABehaviorBase of the editor module: the only allowed parent class (spec D4). */
	static TWeakObjectPtr<UClass> BehaviorBaseClass;
};
#endif

/**
 * Custom Node Asset (CNA). One asset describes one Blueprint node (spec D2):
 *  - the macro graph "Execute" holds the game logic between Tunnel Input and Tunnel Output (D1);
 *  - the Event Graph is the Editor Behavior Graph, its parent class is UCNABehaviorBase (D4);
 *  - the properties below describe appearance, flags, pin overrides, redirects and local variables (spec 3).
 *
 * Nothing of the asset is needed at runtime: the node is expanded into standard nodes when a Blueprint is
 * compiled (spec 2.2). The asset is therefore editor-only and never cooked.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Custom Node Asset"))
class NODEFACTORY_API UCustomNodeAsset : public UBlueprint
{
	GENERATED_BODY()

public:
	UCustomNodeAsset(const FObjectInitializer& ObjectInitializer);

	/** Name of the macro graph with the subgraph (spec 2.5). */
	static const FName ExecuteGraphName;
	/** Name of the Event Graph page that is the Editor Behavior Graph. */
	static const FName BehaviorGraphName;

#if WITH_EDITORONLY_DATA
	// Node (spec 3.1)

	/** Title of the node. Empty: the asset name without the CNA_ prefix. */
	UPROPERTY(EditAnywhere, Category = "Node")
	FText DisplayName;

	/** Second line of the title in a smaller font (V8). */
	UPROPERTY(EditAnywhere, Category = "Node")
	FText Subtitle;

	UPROPERTY(EditAnywhere, Category = "Node", meta = (MultiLine = true))
	FText Tooltip;

	/** Menu category; nesting with a vertical bar: "Math|Vector". */
	UPROPERTY(EditAnywhere, Category = "Node")
	FText Category;

	/** Search words of the action menu. */
	UPROPERTY(EditAnywhere, Category = "Node")
	FText Keywords;

	/** The node is not offered by the menu but keeps working in existing graphs (V9). */
	UPROPERTY(EditAnywhere, Category = "Node")
	bool bHiddenInMenu;

	/** Comment that appears when the node is placed. */
	UPROPERTY(EditAnywhere, Category = "Node", meta = (MultiLine = true))
	FText DefaultComment;

	/** Allows renaming the instance: the label is shown as the second line (V8). */
	UPROPERTY(EditAnywhere, Category = "Node")
	bool bAllowInstanceLabel;

	// Appearance (spec 3.1, 3.2)

	UPROPERTY(EditAnywhere, Category = "Appearance", meta = (InlineEditConditionToggle))
	bool bUseHeaderColor;

	/** Color of the title bar. Without it the colors of function calls of the Graph Editor settings are used. */
	UPROPERTY(EditAnywhere, Category = "Appearance", meta = (EditCondition = "bUseHeaderColor"))
	FLinearColor HeaderColor;

	/** Color of the title and subtitle text (own widget, V24). */
	UPROPERTY(EditAnywhere, Category = "Appearance")
	FLinearColor HeaderTextColor;

	/** Default color of all pin labels; per pin through LabelColor of the override (V25). */
	UPROPERTY(EditAnywhere, Category = "Appearance")
	FLinearColor PinLabelColor;

	/** Brush of the EditorStyle in front of the title, like Branch or Function (V20). */
	UPROPERTY(EditAnywhere, Category = "Appearance|Icon")
	FName Icon;

	UPROPERTY(EditAnywhere, Category = "Appearance|Icon")
	FLinearColor IconTint;

	/** Texture of the Content Browser shown instead of Icon; UI texture group without mips is recommended (V24). */
	UPROPERTY(EditAnywhere, Category = "Appearance|Icon")
	TSoftObjectPtr<UTexture2D> IconTexture;

	UPROPERTY(EditAnywhere, Category = "Appearance|Icon")
	FLinearColor IconTextureTint;

	/** Optional texture replacing the corner icon, including the latent icon. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Icon")
	TSoftObjectPtr<UTexture2D> CornerIconTexture;
	UPROPERTY(EditAnywhere, Category = "Appearance|Icon")
	FLinearColor CornerIconTextureTint;
	UPROPERTY(EditAnywhere, Category = "Appearance|Icon", meta = (ClampMin = "8.0", ClampMax = "128.0"))
	float CornerIconTextureSize;

	/** Default corner icon; a latent subgraph shows Latent unless a custom texture is set. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Icon")
	ECNACornerIcon CornerIcon;

	/** Compact look without a title bar, like the math operators. Only for a pure node. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Compact")
	bool bCompact;

	/** Large text of the compact node, for example "+" or "SUM". */
	UPROPERTY(EditAnywhere, Category = "Appearance|Compact", meta = (EditCondition = "bCompact"))
	FText CompactTitle;

	/** Text badges in the corners of the node (spec 3.2). */
	UPROPERTY(EditAnywhere, Category = "Appearance|Widgets")
	TArray<FCNABadge> Badges;

	// Style families (Animation Blueprint and Niagara looks for ordinary Blueprint nodes)

	/** Visual family of the node. The pins keep working as Blueprint pins in every family. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Style")
	ECNANodeStyle NodeStyle;

	/** Kind of Animation Blueprint node to imitate (NodeStyle == Animation). */
	UPROPERTY(EditAnywhere, Category = "Appearance|Style")
	ECNAAnimNodeStyle AnimStyle;

	/** Animation: exec pins are drawn as pose pins, like the pose links of an AnimGraph. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Style")
	bool bExecPinsAsPosePins;

	/** Animation: lightning "fast path" mark in the corner, like anim nodes that use the fast path. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Style")
	bool bShowFastPathIcon;

	/** Kind of Niagara script node to imitate (NodeStyle == Niagara). */
	UPROPERTY(EditAnywhere, Category = "Appearance|Style")
	ECNANiagaraNodeStyle NiagaraStyle;

	/** Niagara: "Add Pin" rows of Map Get / Map Set for the dynamic pins of the node. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Style")
	bool bNiagaraAddPinRows;

	/** Niagara: pin names like "Module.Speed" show the namespace dimmed, as in Niagara. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Style")
	bool bNiagaraNamespaceLabels;

	/** Niagara: exec pins are drawn as Parameter Map pins. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Style")
	bool bExecPinsAsParameterMap;

	/** Niagara Custom HLSL: text shown in the text area of the body (read-only on instances, a note). */
	UPROPERTY(EditAnywhere, Category = "Appearance|Style", meta = (MultiLine = true))
	FText NiagaraBodyText;

	/** Minimum width of the node in slate units, 0 for automatic. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Widgets", meta = (ClampMin = "0", ClampMax = "1200", UIMin = "0", UIMax = "600"))
	float MinWidth;

	/** Minimum node height in slate units; 0 uses the automatic height. */
	UPROPERTY(EditAnywhere, Category = "Appearance|Widgets", meta = (ClampMin = "0", ClampMax = "1200", UIMin = "0", UIMax = "600"))
	float MinHeight;

	/** Widgets on the body of the node that replace the value fields of input pins (spec 3.2). */
	UPROPERTY(EditAnywhere, Category = "Appearance|Widgets")
	TArray<FCNABodyWidget> BodyWidgets;

	/** Apply a pin override color to connected wires; without an override, normal type colors remain. */
	UPROPERTY(EditAnywhere, Category = "Appearance")
	bool bColorWires;

	// Flags and placement (spec 3.1)

	/** Node without exec pins; values are computed on every access. The subgraph must not execute nodes (CNA004). */
	UPROPERTY(EditAnywhere, Category = "Flags")
	bool bPure;

	/** The node gets the standard Development Only state (V5). */
	UPROPERTY(EditAnywhere, Category = "Flags")
	bool bDevelopmentOnly;

	/** Compiling a Blueprint that uses the node produces the warning CNA009. */
	UPROPERTY(EditAnywhere, Category = "Flags")
	bool bDeprecated;

	UPROPERTY(EditAnywhere, Category = "Flags", meta = (EditCondition = "bDeprecated", MultiLine = true))
	FText DeprecationMessage;

	/** Graphs where the node can be placed. */
	UPROPERTY(EditAnywhere, Category = "Placement", meta = (Bitmask, BitmaskEnum = "ECNAGraphKind"))
	int32 AllowedGraphs;

	/** Class whose Blueprints may use the node (CNA012); also the output type of Get Caller (spec 4.9). */
	UPROPERTY(EditAnywhere, Category = "Placement", meta = (AllowAbstract = "true"))
	TSubclassOf<UObject> ContextClass;

	// Pins (spec 4)

	/** Overrides of pins of the macro Execute, matched by name (spec 4.2, 4.7). */
	UPROPERTY(EditAnywhere, Category = "Pins")
	TArray<FCNAPinOverride> PinOverrides;

	/** Old pin name -> new pin name, keeps wires after renaming a pin of the macro (V11). */
	UPROPERTY(EditAnywhere, Category = "Pins")
	TArray<FCNAPinRedirect> PinRedirects;

	/** Local variables of the subgraph (spec 4.8). Edited in the Local Variables panel of the asset window. */
	UPROPERTY()
	TArray<FCNALocalVariable> LocalVariables;

	// Metadata (spec 3.1, 8.3)

	UPROPERTY(EditAnywhere, Category = "Metadata")
	FString Author;

	UPROPERTY(EditAnywhere, Category = "Metadata", meta = (MultiLine = true))
	FString Description;

	UPROPERTY(EditAnywhere, Category = "Metadata")
	TArray<FString> Tags;

	/** Address of the documentation, shown as text in the tooltip (spec 3.3). */
	UPROPERTY(EditAnywhere, Category = "Metadata")
	FString SourceUrl;

	// Internal

	/** Grows when the pin structure changes; compared with NodeVersion of the instances (CNA006). */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Internal")
	int32 AssetVersion;

	/** Hash of the pin structure at the last bump of AssetVersion. */
	UPROPERTY()
	uint32 SignatureHash;

	/** Identity of the asset, exported as "id" in JSON (spec 8.1). */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Internal")
	FGuid AssetGuid;

	/** Cached for the menu and the registry tags: the subgraph contains a latent node. */
	UPROPERTY()
	bool bContainsLatent;

	/** Cached "None" or "Array of T" for the CNA.DynamicPins tag. */
	UPROPERTY()
	FString CachedDynamicPinsDescription;

	/** The macro Execute (also reachable through MacroGraphs). */
	UPROPERTY()
	UEdGraph* ExecuteGraph;

	/** The Event Graph page used as Editor Behavior Graph (also in UbergraphPages). */
	UPROPERTY()
	UEdGraph* BehaviorGraph;
#endif // WITH_EDITORONLY_DATA

	/** The asset is editor data only and is never cooked (spec 2.2, V-check: cooker honours IsEditorOnly). */
	virtual bool IsEditorOnly() const override { return true; }

	virtual void Serialize(FArchive& Ar) override;
	virtual void PostInitProperties() override;

#if WITH_EDITOR
	// UObject
	virtual void PostLoad() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
#if CNA_WITH_DATA_VALIDATION
	virtual EDataValidationResult IsDataValid(TArray<FText>& ValidationErrors) override;
#endif

	// UBlueprint
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
	virtual void GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses, TSet<const UClass*>& DisallowedChildrenOfClasses) const override;

	/** The macro Execute or nullptr (CNA005). */
	UEdGraph* FindExecuteGraph() const;

	/** The Editor Behavior Graph or nullptr. */
	UEdGraph* FindBehaviorGraph() const;

	/** Re-reads ExecuteGraph and BehaviorGraph from MacroGraphs and UbergraphPages. */
	void RefreshGraphPointers();

	const FCNAPinOverride* FindPinOverride(FName PinName) const;
	FCNAPinOverride* FindPinOverrideMutable(FName PinName);
	/** Existing override or a new one appended to PinOverrides. Call Modify() first. */
	FCNAPinOverride& FindOrAddPinOverride(FName PinName);

	/** Variable by Guid; when no Guid matches and FallbackName is set, by name (older nodes). */
	const FCNALocalVariable* FindLocalVariable(const FGuid& Guid, FName FallbackName = NAME_None) const;
	const FCNALocalVariable* FindLocalVariableByName(FName Name) const;

	/** Unique variable name based on BaseName: Total, Total_1, Total_2. */
	FName MakeUniqueLocalVariableName(FName BaseName) const;

	/** Follows PinRedirects from an old name to the current one, chains included. Returns OldName without match. */
	FName ResolvePinRedirect(FName OldName) const;

	/** At least one override declares a dynamic group. */
	bool HasDynamicGroup() const;

	/** CornerIcon, or Latent when the subgraph contains a latent node (spec 3.1). */
	ECNACornerIcon GetEffectiveCornerIcon() const;

	/** DisplayName, or the asset name without the CNA_ prefix. */
	FText GetEffectiveDisplayName() const;

	/** Compact look is only applied to pure nodes (spec 3.1). */
	bool IsCompactInEffect() const { return bCompact && bPure; }

	/** Marks the pin structure as changed: AssetVersion + 1 (spec 9.1). */
	void BumpAssetVersion();

	/** Broadcasts FCNAEditorHooks::OnAssetChanged. */
	void NotifyAssetChanged(FName PropertyName = NAME_None);

	/** Regenerates invalid or duplicated Guids of local variables. Returns true if anything changed. */
	bool EnsureUniqueLocalVariableGuids();

	/** Gives every body widget a unique WidgetGuid (copies of array elements share one). Returns true on change. */
	bool EnsureUniqueBodyWidgetGuids();

	/** Curve body widget by Guid; when no Guid matches and FallbackName is set, by PinName. */
	const FCNABodyWidget* FindCurveWidget(const FGuid& WidgetGuid, FName FallbackName = NAME_None) const;

	/** All Curve body widgets in order. */
	void GetCurveWidgets(TArray<const FCNABodyWidget*>& OutWidgets) const;

	/** Title color the node shows: HeaderColor when set, else the color of the style family (false: engine default). */
	bool GetStyleHeaderColor(FLinearColor& OutColor) const;
#endif // WITH_EDITOR
};
