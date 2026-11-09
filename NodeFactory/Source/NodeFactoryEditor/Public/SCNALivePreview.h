// Copyright (c) NodeFactory. Live Preview of the asset window (spec 7.1, D10).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"
#include "EditorUndoClient.h"

class UCustomNodeAsset;
class UBlueprint;
class UEdGraph;
class UK2Node_CustomNode;
class SGraphEditor;
class SBox;

/**
 * Live Preview (spec 7.1, D10): the node 1:1 as it appears in a Blueprint, in a read-only SGraphEditor. The node lives
 * in a transient graph of a transient Blueprint whose parent class is the ContextClass of the asset, so Behavior and
 * the widgets of the node run as in a real graph. The node follows every change of the asset; Rebuild places it again.
 */
class NODEFACTORYEDITOR_API SCNALivePreview : public SCompoundWidget, public FGCObject, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SCNALivePreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UCustomNodeAsset* InAsset);
	virtual ~SCNALivePreview();

	/** Updates the node on the next frame; bRebuild places a new node (Behavior receives OnNodePlaced again). */
	void RequestRefresh(bool bRebuild = false);

	UK2Node_CustomNode* GetPreviewNode() const { return PreviewNode; }

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override { RequestRefresh(); }
	virtual void PostRedo(bool bSuccess) override { RequestRefresh(); }

private:
	bool IsPreviewEnabled() const;
	bool CanShowNode() const;
	void CreateGraph(UClass* ParentClass);
	void DestroyGraph();
	void CreateNode();
	void DestroyNode();
	EActiveTimerReturnType HandleRefreshTimer(double InCurrentTime, float InDeltaTime);
	void HandleAssetChanged(UCustomNodeAsset* ChangedAsset, FName PropertyName);
	void HandleBlueprintChanged(UBlueprint* Blueprint);
	int32 GetActiveIndex() const;
	FText GetStatusText() const;
	FText GetMessageText() const;

	TWeakObjectPtr<UCustomNodeAsset> Asset;
	UBlueprint* PreviewBlueprint = nullptr;
	UEdGraph* PreviewGraph = nullptr;
	UK2Node_CustomNode* PreviewNode = nullptr;
	UClass* PreviewParentClass = nullptr;
	TSharedPtr<SBox> GraphHost;
	TSharedPtr<SGraphEditor> GraphEditor;
	bool bRefreshPending = false;
	bool bRebuildPending = false;
	FDelegateHandle AssetChangedHandle;
	FDelegateHandle BlueprintChangedHandle;
	FDelegateHandle BlueprintCompiledHandle;
};
