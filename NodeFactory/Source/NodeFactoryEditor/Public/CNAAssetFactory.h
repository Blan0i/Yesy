// Copyright (c) NodeFactory. Factory of Custom Node Assets: Content Browser > Add New > Blueprints (spec 7.2).

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "CNAAssetFactory.generated.h"

class UCustomNodeAsset;

/**
 * Creates a Custom Node Asset named CNA_NewNode: a Blueprint whose parent class is UCNABehaviorBase (D4) with the
 * macro Execute (Tunnel Input and Tunnel Output, D1) and the Event Graph page used as Editor Behavior Graph (V23).
 * The new node is impure and passes execution through: execute -> then.
 */
UCLASS(hidecategories = Object)
class NODEFACTORYEDITOR_API UCNAAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UCNAAssetFactory(const FObjectInitializer& ObjectInitializer);

	// UFactory
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual uint32 GetMenuCategories() const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetToolTip() const override;
	virtual FString GetDefaultNewAssetName() const override;

	/** New asset in an outer (package or transient package for tests), compiled and ready to use. */
	static UCustomNodeAsset* CreateCustomNodeAsset(UObject* Outer, FName Name);

	/**
	 * Adds the graphs an asset lacks: the macro Execute with both Tunnel nodes and the Behavior Graph page. With
	 * bWithDefaultPins a new Execute gets the exec pins execute and then, wired through.
	 */
	static void CreateDefaultGraphs(UCustomNodeAsset* Asset, bool bWithDefaultPins);
};
