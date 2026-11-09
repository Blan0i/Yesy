// Copyright (c) NodeFactory. The asset window: a Blueprint editor for UCustomNodeAsset (spec 7.1, V27).

#pragma once

#include "CoreMinimal.h"
#include "BlueprintEditor.h"

class UCustomNodeAsset;
class IDetailsView;
class SCNAPinManager;
class SCNALocalVariables;
class SCNALivePreview;

/**
 * The asset opens in one window with both graphs, the settings and the preview of the node (spec 7). The window is the
 * Blueprint editor with one application mode of its own (FCNAEditorMode), as the Widget Blueprint editor does.
 */
class NODEFACTORYEDITOR_API FCNAAssetEditor : public FBlueprintEditor
{
public:
	void InitCNAEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UCustomNodeAsset* Asset);

	UCustomNodeAsset* GetCustomNodeAsset() const;

	// FBlueprintEditor. V-check: signature of RegisterApplicationModes and GetGraphAppearance being virtual in 4.20.3.
	virtual void RegisterApplicationModes(const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode, bool bNewlyCreated = false) override;
	virtual FGraphAppearanceInfo GetGraphAppearance(UEdGraph* InGraph) const override;

	// IToolkit
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	// Bodies of the tabs of FCNAEditorMode
	TSharedRef<SWidget> CreateNodeSettingsWidget();
	TSharedRef<SWidget> CreatePinManagerWidget();
	TSharedRef<SWidget> CreateLocalVariablesWidget();
	TSharedRef<SWidget> CreateLivePreviewWidget();
	TSharedRef<SWidget> CreateLogWidget();

	/** Rebuilds the panels after an import or an undo that replaced many objects. */
	void RefreshNodeFactoryPanels();

private:
	void BindNodeFactoryCommands();
	void HandleImportJson();
	void HandleExportJson();
	void HandleToggleLivePreview();
	bool IsLivePreviewEnabled() const;
	void HandleValidate();
	void HandleRecompileNode();
	bool HasAsset() const { return GetCustomNodeAsset() != nullptr; }

	TSharedPtr<IDetailsView> NodeSettingsView;
	TSharedPtr<SCNAPinManager> PinManager;
	TSharedPtr<SCNALocalVariables> LocalVariables;
	TSharedPtr<SCNALivePreview> LivePreview;
};
