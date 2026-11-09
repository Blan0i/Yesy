// Copyright (c) NodeFactory. Export of a Custom Node Asset to one readable JSON file (spec 8.1).

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UCustomNodeAsset;
class UEdGraph;

/**
 * Writes the asset as JSON: UTF-8 without BOM, 2 spaces of indentation, LF line ends, fixed field order, arrays in
 * the order of the asset and no time stamps (spec 8.1). Both graphs are stored in the copy and paste text format
 * of the engine, T3D (spec D8, V18), as arrays of lines. Textures are referenced by path only.
 */
struct NODEFACTORYEDITOR_API FCNAJsonExporter
{
	/** The document of the asset in the order of spec 8.1. */
	static TSharedRef<FJsonObject> BuildJson(const UCustomNodeAsset* Asset);

	/** The text of the document. */
	static FString ExportToString(const UCustomNodeAsset* Asset);

	/** Writes the document to a file. */
	static bool ExportToFile(const UCustomNodeAsset* Asset, const FString& FilePath, FText& OutError);

	/** Asks for a file name and exports (toolbar of the asset window, Content Browser). Returns true when written. */
	static bool PromptAndExport(UCustomNodeAsset* Asset);

	/** Nodes of a graph in the T3D text format of the engine with LF line ends; empty for an empty graph. */
	static FString ExportGraphToT3D(UEdGraph* Graph);

	/** Pins of the signature: name, direction and type, in the order of the Tunnel nodes. */
	static TArray<TSharedPtr<FJsonValue>> BuildSignatureJson(const UCustomNodeAsset* Asset);

	/** "<AssetName>.json". */
	static FString GetDefaultFileName(const UCustomNodeAsset* Asset);

	/** "4.20.3": major, minor and patch only, so files written by different builds of one engine do not differ. */
	static FString GetEngineVersionString();

	/** VersionName of NodeFactory.uplugin. */
	static FString GetPluginVersionString();

	/** Paths of the textures the asset references (icon texture). */
	static TArray<FString> GetTexturePaths(const UCustomNodeAsset* Asset);
};
