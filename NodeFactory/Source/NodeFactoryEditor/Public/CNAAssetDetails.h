// Copyright (c) NodeFactory. Node Settings: the Details of a Custom Node Asset (spec 3, 7.1).

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IPropertyHandle;
class IPropertyUtilities;
class UCustomNodeAsset;

/**
 * Lays out the categories of the asset (Node, Appearance, Icon, Compact, Widgets, Style, Flags, Placement, Pins,
 * Metadata, Internal), hides the Blueprint options that do not apply to a node, greys out every setting that does nothing
 * for the current choice, names the settings in the plugin language and replaces the Icon name by a picker with
 * previews of the brushes of the icon catalog (spec 3.2, 14). The Pins category lists stale pin references.
 */
class NODEFACTORYEDITOR_API FCNAAssetDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TSharedRef<SWidget> MakeIconMenu();
	const struct FSlateBrush* GetCurrentIconBrush() const;
	FText GetCurrentIconText() const;
	FName GetCurrentIcon() const;
	void SetIcon(FName NewIcon);

	/** Menu of the stale references: point each one to a pin or remove it (one Undo step per action). */
	TSharedRef<SWidget> MakeStaleReferenceMenu();
	int32 GetStaleReferenceCount() const;

	TSharedPtr<IPropertyHandle> IconHandle;
	TSharedPtr<class SComboButton> IconCombo;
	TWeakObjectPtr<UCustomNodeAsset> Asset;
	TWeakPtr<IPropertyUtilities> PropertyUtilities;
};

/** Editor Preferences > Plugins > NodeFactory: language combo with native names, Russian names in Russian. */
class NODEFACTORYEDITOR_API FCNAEditorSettingsDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
