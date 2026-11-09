// Copyright (c) NodeFactory. The asset window: a Blueprint editor for UCustomNodeAsset (spec 7.1, V27).

#include "CNAAssetEditor.h"
#include "CNAEditorMode.h"
#include "CNAEditorCommands.h"
#include "CNAEditorSettings.h"
#include "CNAGraphUtils.h"
#include "CNAJsonExporter.h"
#include "CNAJsonImporter.h"
#include "CNAMessages.h"
#include "CNAUpdateService.h"
#include "CNAValidator.h"
#include "CustomNodeAsset.h"
#include "SCNALivePreview.h"
#include "SCNALocalVariables.h"
#include "SCNANodeCatalog.h"
#include "SCNAPinManager.h"

#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Modules/ModuleManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

void FCNAAssetEditor::InitCNAEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UCustomNodeAsset* Asset)
{
	// Bound first: InitBlueprintEditor builds the toolbar of the mode, which refers to these commands.
	BindNodeFactoryCommands();

	TArray<UBlueprint*> Blueprints;
	Blueprints.Add(Asset);
	InitBlueprintEditor(Mode, InitToolkitHost, Blueprints, /*bShouldOpenInDefaultsMode*/ false);
}

UCustomNodeAsset* FCNAAssetEditor::GetCustomNodeAsset() const
{
	return Cast<UCustomNodeAsset>(GetBlueprintObj());
}

void FCNAAssetEditor::RegisterApplicationModes(const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode, bool bNewlyCreated)
{
	if (InBlueprints.Num() == 1 && Cast<UCustomNodeAsset>(InBlueprints[0]))
	{
		AddApplicationMode(FCNAEditorMode::ModeName, MakeShareable(new FCNAEditorMode(SharedThis(this))));
		SetCurrentMode(FCNAEditorMode::ModeName);
		return;
	}
	// Several assets at once: the plain Blueprint modes.
	FBlueprintEditor::RegisterApplicationModes(InBlueprints, bShouldOpenInDefaultsMode, bNewlyCreated);
}

FGraphAppearanceInfo FCNAAssetEditor::GetGraphAppearance(UEdGraph* InGraph) const
{
	FGraphAppearanceInfo Appearance = FBlueprintEditor::GetGraphAppearance(InGraph);
	const UCustomNodeAsset* Asset = GetCustomNodeAsset();
	if (InGraph && FCNAGraphUtils::IsBehaviorGraph(InGraph))
	{
		Appearance.CornerText = LOCTEXT("AssetEditor.BehaviorCorner", "EDITOR BEHAVIOR");
	}
	else if (InGraph && Asset && FCNAGraphUtils::GetTopLevelGraph(InGraph) == Asset->FindExecuteGraph())
	{
		Appearance.CornerText = LOCTEXT("AssetEditor.ExecuteCorner", "EXECUTION GRAPH");
	}
	return Appearance;
}

FName FCNAAssetEditor::GetToolkitFName() const
{
	return FName(TEXT("CNAAssetEditor"));
}

FText FCNAAssetEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AssetEditor.ToolkitName", "Custom Node Editor");
}

FString FCNAAssetEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("CustomNode ");
}

FLinearColor FCNAAssetEditor::GetWorldCentricTabColorScale() const
{
	// The type color of the asset (spec 7.2).
	return FLinearColor(FColor(0, 184, 169));
}

TSharedRef<SWidget> FCNAAssetEditor::CreateNodeSettingsWidget()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.bAllowSearch = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	// The properties notify the asset themselves (PostEditChangeProperty -> FCNAEditorHooks::OnAssetChanged).
	Args.NotifyHook = nullptr;
	NodeSettingsView = PropertyModule.CreateDetailView(Args);
	NodeSettingsView->SetObject(GetCustomNodeAsset());
	return NodeSettingsView.ToSharedRef();
}

TSharedRef<SWidget> FCNAAssetEditor::CreatePinManagerWidget()
{
	return SAssignNew(PinManager, SCNAPinManager, GetCustomNodeAsset());
}

TSharedRef<SWidget> FCNAAssetEditor::CreateLocalVariablesWidget()
{
	return SAssignNew(LocalVariables, SCNALocalVariables, GetCustomNodeAsset());
}

TSharedRef<SWidget> FCNAAssetEditor::CreateLivePreviewWidget()
{
	return SAssignNew(LivePreview, SCNALivePreview, GetCustomNodeAsset());
}

TSharedRef<SWidget> FCNAAssetEditor::CreateLogWidget()
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
	return MessageLogModule.CreateLogListingWidget(MessageLogModule.GetLogListing(FCNAMessages::LogName));
}

void FCNAAssetEditor::RefreshNodeFactoryPanels()
{
	if (NodeSettingsView.IsValid())
	{
		NodeSettingsView->ForceRefresh();
	}
	if (PinManager.IsValid())
	{
		PinManager->RequestRefresh(true);
	}
	if (LocalVariables.IsValid())
	{
		LocalVariables->RequestRefresh(true);
	}
	if (LivePreview.IsValid())
	{
		LivePreview->RequestRefresh(true);
	}
}

void FCNAAssetEditor::BindNodeFactoryCommands()
{
	// Ctrl+F stays Find in Blueprint here; the panels map FindItem in their own command lists (spec 7.1).
	const FCNAEditorCommands& Commands = FCNAEditorCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = GetToolkitCommands();
	const FCanExecuteAction CanExecute = FCanExecuteAction::CreateSP(this, &FCNAAssetEditor::HasAsset);

	CommandList->MapAction(Commands.ImportJson, FExecuteAction::CreateSP(this, &FCNAAssetEditor::HandleImportJson), CanExecute);
	CommandList->MapAction(Commands.ExportJson, FExecuteAction::CreateSP(this, &FCNAAssetEditor::HandleExportJson), CanExecute);
	CommandList->MapAction(Commands.LivePreview, FExecuteAction::CreateSP(this, &FCNAAssetEditor::HandleToggleLivePreview), FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FCNAAssetEditor::IsLivePreviewEnabled));
	CommandList->MapAction(Commands.Validate, FExecuteAction::CreateSP(this, &FCNAAssetEditor::HandleValidate), CanExecute);
	CommandList->MapAction(Commands.RecompileNode, FExecuteAction::CreateSP(this, &FCNAAssetEditor::HandleRecompileNode), CanExecute);
	CommandList->MapAction(Commands.OpenCatalog, FExecuteAction::CreateLambda([]()
	{
		FGlobalTabmanager::Get()->InvokeTab(SCNANodeCatalog::TabName);
	}));
}

void FCNAAssetEditor::HandleImportJson()
{
	if (FCNAJsonImporter::PromptAndImport(GetCustomNodeAsset()))
	{
		// The importer rewrote the nodes of both graphs inside the existing graph objects.
		RefreshEditors();
		RefreshNodeFactoryPanels();
	}
}

void FCNAAssetEditor::HandleExportJson()
{
	FCNAJsonExporter::PromptAndExport(GetCustomNodeAsset());
}

void FCNAAssetEditor::HandleToggleLivePreview()
{
	UCNAEditorSettings* Settings = GetMutableDefault<UCNAEditorSettings>();
	Settings->SetLivePreviewEnabled(!Settings->bLivePreviewEnabled);
	if (LivePreview.IsValid())
	{
		LivePreview->RequestRefresh(true);
	}
}

bool FCNAAssetEditor::IsLivePreviewEnabled() const
{
	return GetDefault<UCNAEditorSettings>()->bLivePreviewEnabled;
}

void FCNAAssetEditor::HandleValidate()
{
	FCNAValidator::ValidateAssetToLog(GetCustomNodeAsset(), /*bOpenLog*/ true);
}

void FCNAAssetEditor::HandleRecompileNode()
{
	FCNAUpdateService::RecompileNode(GetCustomNodeAsset());
}

#undef LOCTEXT_NAMESPACE
