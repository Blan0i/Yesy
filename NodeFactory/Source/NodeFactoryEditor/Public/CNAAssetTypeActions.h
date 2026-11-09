// Copyright (c) NodeFactory. Content Browser actions of Custom Node Assets (spec 7.2).

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

class UCustomNodeAsset;

/**
 * Custom Node Asset in the Content Browser (spec 7.2): turquoise accent (0, 184, 169), group Blueprints, the asset
 * window on double click and the context menu Recompile Node, Export to JSON, Import and Overwrite from JSON, Validate
 * (Edit is added by the Content Browser).
 */
class NODEFACTORYEDITOR_API FCNAAssetTypeActions : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor(0, 184, 169); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override { return true; }
	virtual void GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder) override;
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
	virtual FText GetAssetDescription(const FAssetData& AssetData) const override;

private:
	typedef TArray<TWeakObjectPtr<UCustomNodeAsset>> FAssetList;

	void ExecuteRecompile(FAssetList Assets);
	void ExecuteExport(FAssetList Assets);
	void ExecuteImport(FAssetList Assets);
	bool CanExecuteImport(FAssetList Assets) const;
	void ExecuteValidate(FAssetList Assets);
};
