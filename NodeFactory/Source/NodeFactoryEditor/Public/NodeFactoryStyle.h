// Copyright (c) NodeFactory. Slate style set of the plugin (spec 7.2, 9.5).
//
// Registers the class icon and thumbnail of Custom Node Asset, the toolbar icons of the asset window and
// a few helper brushes (badge background, accent strip). The style is created in StartupModule and
// removed in ShutdownModule (spec 9.5), so a module hot reload does not register it twice (A15).

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class NODEFACTORYEDITOR_API FNodeFactoryStyle
{
public:
	/** Creates and registers the style set. Safe to call twice. */
	static void Initialize();

	/** Unregisters and destroys the style set. Safe to call twice. */
	static void Shutdown();

	/** Reloads textures used by Slate after the resources changed on disk. */
	static void ReloadTextures();

	static const ISlateStyle& Get();
	static FName GetStyleSetName();
	static bool IsInitialized() { return StyleInstance.IsValid(); }

	/** Brush by name from this style, then from EditorStyle; nullptr when neither has it (V20). */
	static const FSlateBrush* FindBrush(FName BrushName);

	/** The brush exists in EditorStyle or in this style. Used by the validator for CNA019. */
	static bool DoesBrushExist(FName BrushName);

	/**
	 * Icons offered by the Icon picker of the Details panel (spec 3.1, V20).
	 * The EditorStyle of 4.20 has no public way to enumerate brush names, so the catalog is a fixed list of
	 * names confirmed to exist; names missing in the running engine are filtered out.
	 */
	static const TArray<FName>& GetIconCatalog();

	/** Short display text for an icon brush name: "Kismet.AllClasses.FunctionIcon" -> "Function Icon". */
	static FText GetIconDisplayName(FName BrushName);

	/** Brush names of the plugin, used in code and in the style registration. */
	static const FName ClassIconName;
	static const FName ClassThumbnailName;
	static const FName PluginIconName;
	static const FName BadgeBrushName;
	static const FName AccentStripName;
	static const FName ImportJsonIconName;
	static const FName ExportJsonIconName;
	static const FName LivePreviewIconName;
	static const FName ValidateIconName;
	static const FName RecompileNodeIconName;
	static const FName CatalogTabIconName;
	static const FName PinManagerTabIconName;
	static const FName LocalVariablesIconName;
	static const FName DetailsTabIconName;

private:
	static TSharedRef<class FSlateStyleSet> Create();
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};
