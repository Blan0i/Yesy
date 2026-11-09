// Copyright (c) NodeFactory. Editor Preferences > Plugins > NodeFactory (spec 9.7, D7).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "CNAEditorSettings.generated.h"

/** Language of the NodeFactory UI; the same values as ECNALanguageMode of CNALocalization.h. */
UENUM()
enum class ECNAPluginLanguage : uint8
{
	/** The whole NodeFactory UI in Russian. */
	Russian,
	/** The whole NodeFactory UI in English. */
	English,
	/** Node settings in the Details panel of a Custom Node Asset in Russian, everything else in English. */
	RussianAndEnglish UMETA(DisplayName = "Russian & English")
};

/**
 * User settings of the plugin. They live in the per-user editor config, so the plugin never writes into the
 * configuration of the project (spec D7).
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "NodeFactory"))
class NODEFACTORYEDITOR_API UCNAEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	UCNAEditorSettings();

	/**
	 * Language of the NodeFactory UI, independent of the editor language. The Details panel of an open asset
	 * changes at once; other open windows after reopening them; menu commands after restarting the editor.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Language")
	ECNAPluginLanguage Language;

	/** Limit of nested custom nodes; deeper nesting fails with CNA002 (spec 5.2). */
	UPROPERTY(config, EditAnywhere, Category = "Expansion", meta = (ClampMin = "1", ClampMax = "128", UIMin = "1", UIMax = "64"))
	int32 MaxExpansionDepth;

	/** Ask before updating the instances of a changed asset (spec 9.1, item 3). */
	UPROPERTY(config, EditAnywhere, Category = "Update")
	bool bConfirmBulkUpdate;

	/** Largest JSON file the importer accepts (spec 8.2). */
	UPROPERTY(config, EditAnywhere, Category = "JSON", meta = (ClampMin = "1", ClampMax = "65536", UIMin = "16", UIMax = "16384"))
	int32 MaxImportFileSizeKB;

	/** Folder with the presets of the Node Catalog. Empty: <Project>/NodeFactory/Presets, else the plugin Presets (spec 8.3). */
	UPROPERTY(config, EditAnywhere, Category = "Catalog")
	FDirectoryPath CatalogFolder;

	/** Detailed output of LogNodeFactory (expansion steps, Behavior events, update decisions). */
	UPROPERTY(config, EditAnywhere, Category = "Diagnostics")
	bool bVerboseLog;

	/** State of the Live Preview toggle of the asset window, remembered between sessions. */
	UPROPERTY(config)
	bool bLivePreviewEnabled;

	/** Absolute path of the catalog folder with the default applied. */
	FString GetCatalogFolderAbsolute() const;

	/** MaxImportFileSizeKB in bytes. */
	int64 GetMaxImportFileSizeBytes() const;

	/** Registration in ISettingsModule (spec 9.7). */
	static const FName ContainerName;
	static const FName CategoryName;
	static const FName SectionName;

	/** Sets the Live Preview toggle and saves the config. */
	void SetLivePreviewEnabled(bool bEnabled);

	/** Pushes Language to FCNALocalization (startup and every change). */
	void ApplyLanguage() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
