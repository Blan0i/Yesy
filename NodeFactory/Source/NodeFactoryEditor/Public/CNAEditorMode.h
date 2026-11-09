// Copyright (c) NodeFactory. Application mode and tabs of the asset window (spec 7.1, V27).

#pragma once

#include "CoreMinimal.h"
#include "BlueprintEditorModes.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

class FCNAAssetEditor;

/** Identifiers of the tabs that the asset window adds to the Blueprint editor. */
struct NODEFACTORYEDITOR_API FCNAEditorTabs
{
	static const FName NodeSettingsID;
	static const FName PinManagerID;
	static const FName LocalVariablesID;
	static const FName LivePreviewID;
	static const FName LogID;
};

/** One tab of the asset window; the body comes from a Create...Widget method of the editor. */
struct FCNAEditorTabSummoner : public FWorkflowTabFactory
{
	typedef TSharedRef<SWidget> (FCNAAssetEditor::*FCreateBody)();

	FCNAEditorTabSummoner(FName InTabId, TSharedPtr<FCNAAssetEditor> InEditor, FCreateBody InCreateBody,
		const FText& InLabel, const FText& InTooltip, FName InIconName);

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override { return Tooltip; }

private:
	TWeakPtr<FCNAAssetEditor> Editor;
	FCreateBody CreateBody;
	FText Tooltip;
};

/**
 * The only mode of the asset window: the Blueprint editor with its graphs, My Blueprint, Details, Compiler Results
 * and Find Results, plus Node Settings, Pin Manager, Local Variables, Live Preview and the NodeFactory log (spec 7.1).
 * Built like the Graph mode of the Widget Blueprint editor.
 */
class NODEFACTORYEDITOR_API FCNAEditorMode : public FBlueprintEditorApplicationMode
{
public:
	static const FName ModeName;
	static FText GetLocalizedMode(const FName InMode);

	explicit FCNAEditorMode(TSharedPtr<FCNAAssetEditor> InEditor);

	// FApplicationMode
	virtual void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override;
	virtual void PostActivateMode() override;

private:
	TWeakPtr<FCNAAssetEditor> CNAEditor;
	FWorkflowAllowedTabSet NodeFactoryTabFactories;
};
