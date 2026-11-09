// Copyright (c) NodeFactory. Safe import of a Custom Node Asset from JSON (spec 8.2) and creation of assets from
// presets of the Node Catalog (spec 8.3).

#pragma once

#include "CoreMinimal.h"
#include "CNATypes.h"
#include "EdGraph/EdGraphPin.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/SoftObjectPath.h"

class UCustomNodeAsset;
class UEdGraph;

/** One pin of graph.signature of the file. */
struct NODEFACTORYEDITOR_API FCNAImportedPin
{
	FName Name;
	EEdGraphPinDirection Direction = EGPD_Input;
	FEdGraphPinType Type;
	/** Path of a class, struct or enum that was not found; the type was made Wildcard (CNA010). */
	FString MissingObject;
};

/** Remark of the import written into the Message Log once the asset was changed (CNA010, CNA019, warnings). */
struct NODEFACTORYEDITOR_API FCNAImportNote
{
	EMessageSeverity::Type Severity = EMessageSeverity::Warning;
	FText Text;
};

/**
 * Content of a JSON file after the checks of spec 8.2. Nothing is loaded into an asset yet, so a damaged file never
 * changes anything (A13). Fields missing in the file keep the defaults of a new asset.
 */
struct NODEFACTORYEDITOR_API FCNAImportData
{
	FCNAImportData();

	FString SourceFile;
	int32 SourceSchemaVersion = 0;

	FGuid Id;
	FString Name;
	FText DisplayName;
	FText Subtitle;
	FText Tooltip;
	FText Category;
	FText Keywords;
	FText DefaultComment;

	// appearance
	bool bUseHeaderColor = false;
	FLinearColor HeaderColor;
	FLinearColor HeaderTextColor;
	FLinearColor PinLabelColor;
	FName Icon;
	FLinearColor IconTint;
	/** Empty when the texture is missing: the default icon is used (CNA019, spec 8.2 item 6). */
	FSoftObjectPath IconTexture;
	FLinearColor IconTextureTint;
	ECNACornerIcon CornerIcon = ECNACornerIcon::None;
	FSoftObjectPath CornerIconTexture;
	FLinearColor CornerIconTextureTint;
	float CornerIconTextureSize = 24.0f;
	TArray<FCNABadge> Badges;
	float MinWidth = 0.0f;
	float MinHeight = 0.0f;
	TArray<FCNABodyWidget> BodyWidgets;
	bool bColorWires = false;
	bool bCompact = false;
	FText CompactTitle;

	// flags
	bool bPure = false;
	bool bDevelopmentOnly = false;
	bool bDeprecated = false;
	FText DeprecationMessage;
	int32 AllowedGraphs = 0;
	/** Null when the file names no class or the class was not found (warning). */
	UClass* ContextClass = nullptr;
	bool bHiddenInMenu = false;
	bool bAllowInstanceLabel = false;

	TArray<FCNAPinOverride> PinOverrides;
	TArray<FCNAPinRedirect> PinRedirects;
	TArray<FCNALocalVariable> LocalVariables;

	// graphs (T3D with LF line ends)
	TArray<FCNAImportedPin> Signature;
	FString ExecuteT3D;
	FString BehaviorT3D;
	TArray<FString> Textures;

	// meta
	FString Author;
	FString Description;
	TArray<FString> Tags;
	FString SourceUrl;

	/** Migration remarks, missing types and textures. */
	TArray<FCNAImportNote> Notes;

	/** Signature pin by name and direction. */
	const FCNAImportedPin* FindSignaturePin(FName PinName, EEdGraphPinDirection Direction) const;
};

/**
 * Import of the JSON format of FCNAJsonExporter (spec 8.2):
 *  1. the file is not larger than MaxImportFileSizeKB;
 *  2. a newer schemaVersion is refused, an older one is migrated;
 *  3. classes and textures are only taken from /Script/, /Game/, /Engine/ and the content of enabled plugins;
 *  4. a missing class, struct or enum of a pin type makes the pin Wildcard (CNA010);
 *  5. a missing node class of a T3D refuses both graphs and the asset stays unchanged;
 *  6. a missing icon texture is replaced by the default icon (CNA019);
 *  7. nothing of the file is executed: only data is read and only graph nodes are created;
 *  8. Import and Overwrite shows the changes first and runs in one Undo transaction.
 */
struct NODEFACTORYEDITOR_API FCNAJsonImporter
{
	// Reading

	/**
	 * Reads and checks a file (items 1-7). bHeaderOnly reads texts and metadata without resolving paths or
	 * checking graphs, for the list of the Node Catalog. Returns false with the reasons in OutErrors.
	 */
	static bool LoadFile(const FString& FilePath, FCNAImportData& OutData, TArray<FText>& OutErrors, bool bHeaderOnly = false);

	/** Same as LoadFile for a text in memory (tests, clipboard). */
	static bool LoadString(const FString& Text, FCNAImportData& OutData, TArray<FText>& OutErrors, bool bHeaderOnly = false);

	/**
	 * T3D checks of one graph: Begin/End Object balance, node classes exist (item 5), top level objects are graph
	 * nodes of the K2 schema and nested objects are graphs or nodes (item 7), object paths are allowed (item 3),
	 * and for the macro Execute the Tunnel nodes are present (CNA005). References to missing objects are notes.
	 */
	static bool CheckT3D(const FString& T3D, bool bRequireTunnels, const FText& GraphLabel, const FString& SourceAssetName,
		TArray<FText>& OutErrors, TArray<FCNAImportNote>& OutNotes);

	/** Number of top level objects (nodes) of a T3D text. */
	static int32 CountT3DNodes(const FString& T3D);

	// Changes (item 8)

	/** Pins that are added (+), removed (-) or changed (~), and the properties that differ. */
	static void CollectChanges(const UCustomNodeAsset* Asset, const FCNAImportData& Data, TArray<FText>& OutPinChanges, TArray<FText>& OutPropertyChanges);

	/** Text of the confirmation dialog; empty when the file matches the asset. */
	static FText BuildChangeSummary(const UCustomNodeAsset* Asset, const FCNAImportData& Data, int32 MaxLinesPerSection = 20);

	// Writing

	/**
	 * Writes the data into the asset: properties, local variables, then the nodes of both graphs. An existing asset
	 * changes in one transaction and keeps its identity (AssetGuid); a new asset takes the id of the file and no
	 * transaction is recorded. On failure an existing asset is rolled back. The asset is compiled afterwards.
	 */
	static bool ApplyToAsset(UCustomNodeAsset* Asset, const FCNAImportData& Data, bool bNewAsset, TArray<FText>& OutErrors);

	/** Import and Overwrite: checks, summary dialog (bConfirm), one transaction, report. True when the asset changed. */
	static bool ImportAndOverwrite(UCustomNodeAsset* Asset, const FString& FilePath, bool bConfirm);

	/** Asks for a file and runs ImportAndOverwrite (toolbar, Content Browser). */
	static bool PromptAndImport(UCustomNodeAsset* Asset);

	/** New asset from a file in a content folder such as /Game/Nodes (Node Catalog). A taken name gets _1, _2. */
	static UCustomNodeAsset* CreateAssetFromFile(const FString& FilePath, const FString& PackagePath, TArray<FText>& OutErrors);

	/** Free asset name in a content folder: Name, Name_1, Name_2 (spec 8.3). */
	static FString MakeUniqueAssetName(const FString& PackagePath, const FString& BaseName);

private:
	static void ReadData(const TSharedPtr<class FJsonObject>& Root, FCNAImportData& Data, TArray<FText>& OutErrors, bool bHeaderOnly);
	static bool ReplaceGraphNodes(UCustomNodeAsset* Asset, UEdGraph* Graph, const FString& T3D, TArray<FText>& OutErrors);
	static void FixupMissingPinTypes(UCustomNodeAsset* Asset, const FCNAImportData& Data, TArray<FCNAImportNote>& OutNotes);
	static void ReportFailure(UCustomNodeAsset* Asset, const FString& FilePath, const TArray<FText>& Errors);
};
