// Copyright (c) NodeFactory. Content Browser actions of Custom Node Assets (spec 7.2).

#include "CNAAssetTypeActions.h"
#include "CNAAssetEditor.h"
#include "CNAJsonExporter.h"
#include "CNAJsonImporter.h"
#include "CNAUpdateService.h"
#include "CNAValidator.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryStyle.h"

#include "AssetData.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/AssetEditorManager.h"
#include "Misc/MessageDialog.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

FText FCNAAssetTypeActions::GetName() const
{
	return LOCTEXT("TypeActions.Name", "Custom Node Asset");
}

UClass* FCNAAssetTypeActions::GetSupportedClass() const
{
	return UCustomNodeAsset::StaticClass();
}

uint32 FCNAAssetTypeActions::GetCategories()
{
	return EAssetTypeCategories::Blueprint;
}

void FCNAAssetTypeActions::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	const FAssetList Assets = GetTypedWeakObjectPtrs<UCustomNodeAsset>(InObjects);
	const FName StyleSet = FNodeFactoryStyle::GetStyleSetName();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("TypeActions.Recompile", "Recompile Node"),
		LOCTEXT("TypeActions.RecompileTip", "Compile the asset and update every Blueprint that uses the node."),
		FSlateIcon(StyleSet, FNodeFactoryStyle::RecompileNodeIconName),
		FUIAction(FExecuteAction::CreateSP(this, &FCNAAssetTypeActions::ExecuteRecompile, Assets)));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("TypeActions.Export", "Export to JSON"),
		LOCTEXT("TypeActions.ExportTip", "Write the asset into a readable JSON file."),
		FSlateIcon(StyleSet, FNodeFactoryStyle::ExportJsonIconName),
		FUIAction(FExecuteAction::CreateSP(this, &FCNAAssetTypeActions::ExecuteExport, Assets)));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("TypeActions.Import", "Import and Overwrite from JSON"),
		LOCTEXT("TypeActions.ImportTip", "Replace the content of the asset by a JSON file after the safety checks. One asset at a time."),
		FSlateIcon(StyleSet, FNodeFactoryStyle::ImportJsonIconName),
		FUIAction(FExecuteAction::CreateSP(this, &FCNAAssetTypeActions::ExecuteImport, Assets),
			FCanExecuteAction::CreateSP(this, &FCNAAssetTypeActions::CanExecuteImport, Assets)));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("TypeActions.Validate", "Validate"),
		LOCTEXT("TypeActions.ValidateTip", "Check the asset and write the result into the Message Log NodeFactory."),
		FSlateIcon(StyleSet, FNodeFactoryStyle::ValidateIconName),
		FUIAction(FExecuteAction::CreateSP(this, &FCNAAssetTypeActions::ExecuteValidate, Assets)));
}

void FCNAAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
	for (UObject* Object : InObjects)
	{
		UCustomNodeAsset* Asset = Cast<UCustomNodeAsset>(Object);
		if (!Asset)
		{
			continue;
		}
		// Same guard as the Blueprint asset actions: without generated classes the Blueprint editor cannot open.
		if (!Asset->SkeletonGeneratedClass || !Asset->GeneratedClass)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("TypeActions.CannotOpen",
				"{0} could not be opened: its generated class is missing. Check that the NodeFactory plugin is enabled and recompile the asset."),
				FText::FromString(Asset->GetName())));
			continue;
		}
		TSharedRef<FCNAAssetEditor> Editor(new FCNAAssetEditor());
		Editor->InitCNAEditor(Mode, EditWithinLevelEditor, Asset);
	}
}

FText FCNAAssetTypeActions::GetAssetDescription(const FAssetData& AssetData) const
{
	// The tag is written by GetAssetRegistryTags, so the asset is not loaded for the tooltip (V19).
	const FString Description = AssetData.GetTagValueRef<FString>(CNATags::Description);
	return FText::FromString(Description);
}

void FCNAAssetTypeActions::ExecuteRecompile(FAssetList Assets)
{
	for (const TWeakObjectPtr<UCustomNodeAsset>& Asset : Assets)
	{
		if (Asset.IsValid())
		{
			FCNAUpdateService::RecompileNode(Asset.Get());
		}
	}
}

void FCNAAssetTypeActions::ExecuteExport(FAssetList Assets)
{
	for (const TWeakObjectPtr<UCustomNodeAsset>& Asset : Assets)
	{
		if (Asset.IsValid() && !FCNAJsonExporter::PromptAndExport(Asset.Get()))
		{
			// Cancelled or failed: the exporter already reported why; do not ask for the remaining files.
			break;
		}
	}
}

bool FCNAAssetTypeActions::CanExecuteImport(FAssetList Assets) const
{
	return Assets.Num() == 1 && Assets[0].IsValid();
}

void FCNAAssetTypeActions::ExecuteImport(FAssetList Assets)
{
	if (!CanExecuteImport(Assets))
	{
		return;
	}
	UCustomNodeAsset* Asset = Assets[0].Get();
	if (FCNAJsonImporter::PromptAndImport(Asset))
	{
		// An open asset window shows the new content.
		for (IAssetEditorInstance* Instance : FAssetEditorManager::Get().FindEditorsForAsset(Asset))
		{
			if (Instance && Instance->GetEditorName() == FName(TEXT("CNAAssetEditor")))
			{
				FCNAAssetEditor* Editor = static_cast<FCNAAssetEditor*>(Instance);
				Editor->RefreshEditors();
				Editor->RefreshNodeFactoryPanels();
			}
		}
	}
}

void FCNAAssetTypeActions::ExecuteValidate(FAssetList Assets)
{
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		if (Assets[Index].IsValid())
		{
			FCNAValidator::ValidateAssetToLog(Assets[Index].Get(), /*bOpenLog*/ Index == Assets.Num() - 1);
		}
	}
}

#undef LOCTEXT_NAMESPACE
