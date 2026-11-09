// Copyright (c) NodeFactory. Node Settings: the Details of a Custom Node Asset (spec 3, 7.1).

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

/**
 * Orders the categories of the asset (Node, Appearance, Flags, Placement, Pins, Metadata, Internal), hides the
 * Blueprint options that do not apply to a node and replaces the Icon name by a picker with previews of the brushes
 * of the icon catalog (spec 3.2, 14).
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

	TSharedPtr<IPropertyHandle> IconHandle;
	TSharedPtr<class SComboButton> IconCombo;
};

/** Shows only the settings used by the selected body widget. */
class NODEFACTORYEDITOR_API FCNABodyWidgetDetails : public IPropertyTypeCustomization
{
public:
 static TSharedRef<IPropertyTypeCustomization> MakeInstance();
 virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils) override;
 virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils) override;
};
