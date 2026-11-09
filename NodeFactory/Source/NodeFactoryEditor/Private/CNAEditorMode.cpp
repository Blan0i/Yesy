// Copyright (c) NodeFactory. Application mode and tabs of the asset window (spec 7.1, V27).

#include "CNAEditorMode.h"
#include "CNAAssetEditor.h"
#include "CNAEditorCommands.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryStyle.h"

#include "BlueprintEditorTabs.h"
#include "SBlueprintEditorToolbar.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "EditorStyleSet.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

const FName FCNAEditorTabs::NodeSettingsID(TEXT("CNAEditor_NodeSettings"));
const FName FCNAEditorTabs::PinManagerID(TEXT("CNAEditor_PinManager"));
const FName FCNAEditorTabs::LocalVariablesID(TEXT("CNAEditor_LocalVariables"));
const FName FCNAEditorTabs::LivePreviewID(TEXT("CNAEditor_LivePreview"));
const FName FCNAEditorTabs::LogID(TEXT("CNAEditor_Log"));

const FName FCNAEditorMode::ModeName(TEXT("CNAEditorMode"));

// FCNAEditorTabSummoner

FCNAEditorTabSummoner::FCNAEditorTabSummoner(FName InTabId, TSharedPtr<FCNAAssetEditor> InEditor, FCreateBody InCreateBody,
	const FText& InLabel, const FText& InTooltip, FName InIconName)
	: FWorkflowTabFactory(InTabId, InEditor)
	, Editor(InEditor)
	, CreateBody(InCreateBody)
	, Tooltip(InTooltip)
{
	TabLabel = InLabel;
	// V-check: TabIcon is an FSlateIcon in 4.20.3.
	TabIcon = FSlateIcon(FNodeFactoryStyle::GetStyleSetName(), InIconName);
	bIsSingleton = true;
	ViewMenuDescription = InLabel;
	ViewMenuTooltip = InTooltip;
}

TSharedRef<SWidget> FCNAEditorTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FCNAAssetEditor> Pinned = Editor.Pin();
	if (!Pinned.IsValid() || !CreateBody)
	{
		return SNullWidget::NullWidget;
	}
	return (Pinned.Get()->*CreateBody)();
}

// FCNAEditorMode

namespace CNAEditorModeLocal
{
	void FillToolbar(FToolBarBuilder& ToolbarBuilder)
	{
		// Spec 7.1: Save and Compile come from the Blueprint editor, then Import JSON, Export JSON and Live Preview.
		const FCNAEditorCommands& Commands = FCNAEditorCommands::Get();
		ToolbarBuilder.BeginSection(TEXT("NodeFactory"));
		{
			ToolbarBuilder.AddToolBarButton(Commands.ImportJson);
			ToolbarBuilder.AddToolBarButton(Commands.ExportJson);
			ToolbarBuilder.AddSeparator();
			ToolbarBuilder.AddToolBarButton(Commands.LivePreview);
			ToolbarBuilder.AddToolBarButton(Commands.Validate);
			ToolbarBuilder.AddToolBarButton(Commands.RecompileNode);
			// The command name differs from the tab brush; the icon is given explicitly.
			ToolbarBuilder.AddToolBarButton(Commands.OpenCatalog, NAME_None, TAttribute<FText>(), TAttribute<FText>(),
				FSlateIcon(FNodeFactoryStyle::GetStyleSetName(), FNodeFactoryStyle::CatalogTabIconName));
		}
		ToolbarBuilder.EndSection();
	}
}

FText FCNAEditorMode::GetLocalizedMode(const FName InMode)
{
	return LOCTEXT("EditorMode.Name", "Custom Node");
}

FCNAEditorMode::FCNAEditorMode(TSharedPtr<FCNAAssetEditor> InEditor)
	// No SCS viewport and no Class Defaults tab: the asset is not an actor and its defaults are Node Settings.
	: FBlueprintEditorApplicationMode(InEditor, FCNAEditorMode::ModeName, &FCNAEditorMode::GetLocalizedMode, false, false)
	, CNAEditor(InEditor)
{
	typedef FCNAEditorTabSummoner FSummoner;
	NodeFactoryTabFactories.RegisterFactory(MakeShareable(new FSummoner(FCNAEditorTabs::NodeSettingsID, InEditor, &FCNAAssetEditor::CreateNodeSettingsWidget,
		LOCTEXT("EditorMode.NodeSettings", "Node Settings"),
		LOCTEXT("EditorMode.NodeSettingsTip", "Name, category, keywords, tooltip, colors, icons and flags of the node."),
		FNodeFactoryStyle::DetailsTabIconName)));
	NodeFactoryTabFactories.RegisterFactory(MakeShareable(new FSummoner(FCNAEditorTabs::PinManagerID, InEditor, &FCNAAssetEditor::CreatePinManagerWidget,
		LOCTEXT("EditorMode.PinManager", "Pin Manager"),
		LOCTEXT("EditorMode.PinManagerTip", "Pins of the node: order, types, containers, default values and visibility flags."),
		FNodeFactoryStyle::PinManagerTabIconName)));
	NodeFactoryTabFactories.RegisterFactory(MakeShareable(new FSummoner(FCNAEditorTabs::LocalVariablesID, InEditor, &FCNAAssetEditor::CreateLocalVariablesWidget,
		LOCTEXT("EditorMode.LocalVariables", "Local Variables"),
		LOCTEXT("EditorMode.LocalVariablesTip", "Variables of one node instance, used by Get Local and Set Local in the Execution Graph."),
		FNodeFactoryStyle::LocalVariablesIconName)));
	NodeFactoryTabFactories.RegisterFactory(MakeShareable(new FSummoner(FCNAEditorTabs::LivePreviewID, InEditor, &FCNAAssetEditor::CreateLivePreviewWidget,
		LOCTEXT("EditorMode.LivePreview", "Live Preview"),
		LOCTEXT("EditorMode.LivePreviewTip", "The node as it appears in a Blueprint, updated on every change."),
		FNodeFactoryStyle::LivePreviewIconName)));
	NodeFactoryTabFactories.RegisterFactory(MakeShareable(new FSummoner(FCNAEditorTabs::LogID, InEditor, &FCNAAssetEditor::CreateLogWidget,
		LOCTEXT("EditorMode.Log", "NodeFactory Log"),
		LOCTEXT("EditorMode.LogTip", "Messages of validation, import, export and updates of the nodes (Message Log NodeFactory)."),
		FNodeFactoryStyle::ValidateIconName)));

	// Spec 7.1: left Node Settings and My Blueprint over Live Preview; center the graphs; right Pin Manager and Details
	// over Local Variables over Compiler Results and the log.
	TabLayout = FTabManager::NewLayout(TEXT("Standalone_CNAEditor_Layout_v1"))
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewStack()
			->SetSizeCoefficient(0.1f)
			->SetHideTabWell(true)
			->AddTab(InEditor->GetToolbarTabId(), ETabState::OpenedTab)
		)
		->Split
		(
			FTabManager::NewSplitter()
			->SetOrientation(Orient_Horizontal)
			->SetSizeCoefficient(0.9f)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.22f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.6f)
					->AddTab(FCNAEditorTabs::NodeSettingsID, ETabState::OpenedTab)
					->AddTab(FBlueprintEditorTabs::MyBlueprintID, ETabState::OpenedTab)
					->SetForegroundTab(FCNAEditorTabs::NodeSettingsID)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.4f)
					->AddTab(FCNAEditorTabs::LivePreviewID, ETabState::OpenedTab)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.52f)
				->AddTab(TEXT("Document"), ETabState::ClosedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.26f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.4f)
					->AddTab(FCNAEditorTabs::PinManagerID, ETabState::OpenedTab)
					->AddTab(FBlueprintEditorTabs::DetailsID, ETabState::OpenedTab)
					->SetForegroundTab(FCNAEditorTabs::PinManagerID)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->AddTab(FCNAEditorTabs::LocalVariablesID, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->AddTab(FBlueprintEditorTabs::CompilerResultsID, ETabState::OpenedTab)
					->AddTab(FCNAEditorTabs::LogID, ETabState::OpenedTab)
					->AddTab(FBlueprintEditorTabs::FindResultsID, ETabState::ClosedTab)
					->SetForegroundTab(FBlueprintEditorTabs::CompilerResultsID)
				)
			)
		)
	);

	// Replaces the toolbar of the Blueprint mode (as the Widget Blueprint editor does): Compile with its status, then
	// the NodeFactory buttons. Save and Browse come from the asset editor itself.
	ToolbarExtender = MakeShareable(new FExtender);
	InEditor->GetToolbarBuilder()->AddCompileToolbar(ToolbarExtender);
	ToolbarExtender->AddToolBarExtension(TEXT("Asset"), EExtensionHook::After, InEditor->GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateStatic(&CNAEditorModeLocal::FillToolbar));
}

void FCNAEditorMode::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	TSharedPtr<FBlueprintEditor> Editor = MyBlueprintEditor.Pin();
	if (!Editor.IsValid())
	{
		return;
	}
	Editor->RegisterToolbarTab(InTabManager.ToSharedRef());
	Editor->PushTabFactories(CoreTabFactories);
	Editor->PushTabFactories(BlueprintEditorOnlyTabFactories);
	Editor->PushTabFactories(BlueprintEditorTabFactories);
	Editor->PushTabFactories(NodeFactoryTabFactories);
}

void FCNAEditorMode::PostActivateMode()
{
	// Both graphs open as tabs of the center (spec 7.1); the Execution Graph comes last so that it is in front.
	TSharedPtr<FCNAAssetEditor> Editor = CNAEditor.Pin();
	if (UCustomNodeAsset* Asset = Editor.IsValid() ? Editor->GetCustomNodeAsset() : nullptr)
	{
		for (UEdGraph* Graph : { Asset->FindBehaviorGraph(), Asset->FindExecuteGraph() })
		{
			const bool bListed = Graph && Asset->LastEditedDocuments.ContainsByPredicate([Graph](const FEditedDocumentInfo& Info)
			{
				return Info.EditedObjectPath.ResolveObject() == Graph;
			});
			if (Graph && !bListed)
			{
				Asset->LastEditedDocuments.Add(FEditedDocumentInfo(Graph));
			}
		}
	}
	FBlueprintEditorApplicationMode::PostActivateMode();
}

#undef LOCTEXT_NAMESPACE
