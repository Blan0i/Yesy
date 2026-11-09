// Copyright (c) NodeFactory. Live Preview of the asset window (spec 7.1, D10).

#include "SCNALivePreview.h"
#include "CustomNodeAsset.h"
#include "K2Node_CustomNode.h"
#include "CNABehaviorHost.h"
#include "CNAEditorSettings.h"
#include "CNAGraphUtils.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "GraphEditor.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

void SCNALivePreview::Construct(const FArguments& InArgs, UCustomNodeAsset* InAsset)
{
	Asset = InAsset;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SCNALivePreview::GetStatusText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("LivePreview.Rebuild", "Rebuild"))
				.ToolTipText(LOCTEXT("LivePreview.RebuildTip", "Place the node again: Behavior receives OnNodePlaced and the pins start with their default values."))
				.IsEnabled(this, &SCNALivePreview::CanShowNode)
				.OnClicked_Lambda([this]()
				{
					RequestRefresh(true);
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex(this, &SCNALivePreview::GetActiveIndex)
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(GraphHost, SBox)
			]
			+ SWidgetSwitcher::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(12.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Justification(ETextJustify::Center)
					.Text(this, &SCNALivePreview::GetMessageText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SButton)
					.Visibility_Lambda([this]() { return IsPreviewEnabled() ? EVisibility::Collapsed : EVisibility::Visible; })
					.Text(LOCTEXT("LivePreview.TurnOn", "Turn On Live Preview"))
					.OnClicked_Lambda([this]()
					{
						GetMutableDefault<UCNAEditorSettings>()->SetLivePreviewEnabled(true);
						RequestRefresh(true);
						return FReply::Handled();
					})
				]
			]
		]
	];

	if (UCustomNodeAsset* CurrentAsset = Asset.Get())
	{
		// Edits of the Execution Graph change the signature without a property change of the asset.
		BlueprintChangedHandle = CurrentAsset->OnChanged().AddSP(this, &SCNALivePreview::HandleBlueprintChanged);
		BlueprintCompiledHandle = CurrentAsset->OnCompiled().AddSP(this, &SCNALivePreview::HandleBlueprintChanged);
	}
	AssetChangedHandle = FCNAEditorHooks::OnAssetChanged.AddSP(this, &SCNALivePreview::HandleAssetChanged);
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
	RequestRefresh(true);
}

SCNALivePreview::~SCNALivePreview()
{
	FCNAEditorHooks::OnAssetChanged.Remove(AssetChangedHandle);
	if (UCustomNodeAsset* CurrentAsset = Asset.Get())
	{
		CurrentAsset->OnChanged().Remove(BlueprintChangedHandle);
		CurrentAsset->OnCompiled().Remove(BlueprintCompiledHandle);
	}
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
	// No graph edits here: the graph editor, a child of this widget, still listens to the graph.
	if (PreviewNode && FCNABehaviorHost::IsAvailable())
	{
		FCNABehaviorHost::Get().ForgetNode(PreviewNode);
	}
	if (PreviewBlueprint)
	{
		PreviewBlueprint->MarkPendingKill();
	}
	PreviewNode = nullptr;
	PreviewGraph = nullptr;
	PreviewBlueprint = nullptr;
}

void SCNALivePreview::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PreviewBlueprint);
	Collector.AddReferencedObject(PreviewGraph);
	Collector.AddReferencedObject(PreviewNode);
	Collector.AddReferencedObject(PreviewParentClass);
}

void SCNALivePreview::RequestRefresh(bool bRebuild)
{
	bRebuildPending |= bRebuild;
	if (!bRefreshPending)
	{
		bRefreshPending = true;
		// Coalesces the bursts of change notifications of one edit. The timer only runs while the tab is visible.
		RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SCNALivePreview::HandleRefreshTimer));
	}
}

bool SCNALivePreview::IsPreviewEnabled() const
{
	return GetDefault<UCNAEditorSettings>()->bLivePreviewEnabled;
}

bool SCNALivePreview::CanShowNode() const
{
	const UCustomNodeAsset* CurrentAsset = Asset.Get();
	return IsPreviewEnabled() && CurrentAsset && FCNAGraphUtils::HasValidExecuteGraph(CurrentAsset);
}

EActiveTimerReturnType SCNALivePreview::HandleRefreshTimer(double InCurrentTime, float InDeltaTime)
{
	bRefreshPending = false;
	const bool bRebuild = bRebuildPending;
	bRebuildPending = false;

	UCustomNodeAsset* CurrentAsset = Asset.Get();
	if (!CanShowNode())
	{
		DestroyNode();
		return EActiveTimerReturnType::Stop;
	}

	// The parent class of the preview Blueprint is the context of the node (Get Caller, World Context, CNA012).
	UClass* ParentClass = CurrentAsset->ContextClass.Get();
	if (!ParentClass)
	{
		ParentClass = UObject::StaticClass();
	}
	if (!PreviewGraph || PreviewParentClass != ParentClass)
	{
		DestroyGraph();
		CreateGraph(ParentClass);
	}

	if (!PreviewNode || bRebuild)
	{
		DestroyNode();
		CreateNode();
	}
	else
	{
		PreviewNode->SetCustomNodeAsset(CurrentAsset);
		PreviewNode->ReconstructNode();
		PreviewGraph->NotifyGraphChanged();
	}

	if (GraphEditor.IsValid())
	{
		// Never above 1:1: the zoom of SGraphPanel stops at the default level when it fits.
		GraphEditor->ZoomToFit(/*bOnlySelection*/ false);
	}
	return EActiveTimerReturnType::Stop;
}

void SCNALivePreview::CreateGraph(UClass* ParentClass)
{
	// V-check: a UBlueprint without generated classes is enough for SGraphEditor and the K2 schema in 4.20.3; it is
	// never compiled and never registered with the asset registry (spec D10).
	UPackage* Package = GetTransientPackage();
	PreviewBlueprint = NewObject<UBlueprint>(Package, MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("CNA_LivePreview")), RF_Transient);
	PreviewBlueprint->ParentClass = ParentClass;
	PreviewBlueprint->BlueprintType = BPTYPE_Normal;
	PreviewParentClass = ParentClass;

	PreviewGraph = NewObject<UEdGraph>(PreviewBlueprint, TEXT("LivePreview"), RF_Transient);
	PreviewGraph->Schema = UEdGraphSchema_K2::StaticClass();
	PreviewGraph->bAllowDeletion = false;
	PreviewBlueprint->UbergraphPages.Add(PreviewGraph);

	FGraphAppearanceInfo Appearance;
	Appearance.CornerText = LOCTEXT("LivePreview.Corner", "PREVIEW");

	GraphEditor = SNew(SGraphEditor)
		.GraphToEdit(PreviewGraph)
		.IsEditable(false)
		.Appearance(Appearance);
	if (GraphHost.IsValid())
	{
		GraphHost->SetContent(GraphEditor.ToSharedRef());
	}
}

void SCNALivePreview::DestroyGraph()
{
	DestroyNode();
	if (GraphHost.IsValid())
	{
		GraphHost->SetContent(SNullWidget::NullWidget);
	}
	GraphEditor.Reset();
	if (PreviewBlueprint)
	{
		PreviewBlueprint->UbergraphPages.Reset();
		// Keeps TObjectIterator<UBlueprint> users from seeing the transient Blueprint until GC.
		PreviewBlueprint->MarkPendingKill();
	}
	if (PreviewGraph)
	{
		PreviewGraph->MarkPendingKill();
	}
	PreviewBlueprint = nullptr;
	PreviewGraph = nullptr;
	PreviewParentClass = nullptr;
}

void SCNALivePreview::CreateNode()
{
	UCustomNodeAsset* CurrentAsset = Asset.Get();
	if (!CurrentAsset || !PreviewGraph)
	{
		return;
	}
	// Same order as FGraphNodeCreator: add, guid, placed, pins. The preview flag comes first so that Behavior runs
	// without transactions and the update service skips the node.
	PreviewNode = NewObject<UK2Node_CustomNode>(PreviewGraph, NAME_None, RF_Transient);
	PreviewNode->SetPreviewNode(true);
	PreviewNode->SetCustomNodeAsset(CurrentAsset);
	PreviewGraph->AddNode(PreviewNode, /*bFromUI*/ false, /*bSelectNewNode*/ false);
	PreviewNode->CreateNewGuid();
	PreviewNode->PostPlacedNewNode();
	if (PreviewNode->Pins.Num() == 0)
	{
		PreviewNode->AllocateDefaultPins();
	}
	PreviewNode->NodePosX = 0;
	PreviewNode->NodePosY = 0;
	PreviewGraph->NotifyGraphChanged();
}

void SCNALivePreview::DestroyNode()
{
	if (!PreviewNode)
	{
		return;
	}
	if (FCNABehaviorHost::IsAvailable())
	{
		FCNABehaviorHost::Get().ForgetNode(PreviewNode);
	}
	if (PreviewGraph)
	{
		PreviewGraph->RemoveNode(PreviewNode);
	}
	PreviewNode->MarkPendingKill();
	PreviewNode = nullptr;
}

void SCNALivePreview::HandleAssetChanged(UCustomNodeAsset* ChangedAsset, FName PropertyName)
{
	if (ChangedAsset && ChangedAsset == Asset.Get())
	{
		RequestRefresh(false);
	}
}

void SCNALivePreview::HandleBlueprintChanged(UBlueprint* Blueprint)
{
	RequestRefresh(false);
}

int32 SCNALivePreview::GetActiveIndex() const
{
	return (CanShowNode() && GraphEditor.IsValid()) ? 0 : 1;
}

FText SCNALivePreview::GetStatusText() const
{
	if (!PreviewNode)
	{
		return LOCTEXT("LivePreview.NoNode", "No preview");
	}
	int32 Inputs = 0;
	int32 Outputs = 0;
	for (const UEdGraphPin* Pin : PreviewNode->Pins)
	{
		if (Pin && !Pin->ParentPin && !Pin->bHidden)
		{
			++(Pin->Direction == EGPD_Input ? Inputs : Outputs);
		}
	}
	return FText::Format(LOCTEXT("LivePreview.Status", "{0} input(s), {1} output(s). Read-only: the node as it appears in a Blueprint."),
		FText::AsNumber(Inputs), FText::AsNumber(Outputs));
}

FText SCNALivePreview::GetMessageText() const
{
	if (!IsPreviewEnabled())
	{
		return LOCTEXT("LivePreview.Off", "Live Preview is off. The toolbar button or the button below turns it on.");
	}
	if (!Asset.IsValid() || !FCNAGraphUtils::HasValidExecuteGraph(Asset.Get()))
	{
		return LOCTEXT("LivePreview.NoGraph", "The Execution Graph has no Tunnel Input or Tunnel Output (CNA005), so there is no node to show.");
	}
	return LOCTEXT("LivePreview.Building", "Building the preview...");
}

#undef LOCTEXT_NAMESPACE
