// Copyright (c) NodeFactory. Details of the structs of a Custom Node Asset: only the settings that do something for
// the current choice are shown, names and tooltips follow the plugin language (Russian in the Russian modes).

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"

class IDetailPropertyRow;
class IDetailChildrenBuilder;
class UCustomNodeAsset;
class UEnum;
struct FCNASignaturePin;

/** Helpers shared by the class and struct customizations of the plugin. */
namespace CNADetails
{
	/** Name and tooltip of one property in the plugin language. */
	struct FPropertyText
	{
		FText Name;
		FText ToolTip;

		FPropertyText() {}
		FPropertyText(const FText& InName, const FText& InToolTip) : Name(InName), ToolTip(InToolTip) {}
	};

	typedef TMap<FName, FPropertyText> FPropertyTextMap;

	/** Texts of the properties of the asset (UCustomNodeAsset). Call inside FCNALocalization::FScopedDetailsLanguage. */
	void GetAssetTexts(FPropertyTextMap& Out);
	void GetPinOverrideTexts(FPropertyTextMap& Out);
	void GetPinRedirectTexts(FPropertyTextMap& Out);
	void GetBadgeTexts(FPropertyTextMap& Out);
	void GetBodyWidgetTexts(FPropertyTextMap& Out);
	void GetCurveSettingsTexts(FPropertyTextMap& Out);

	/** Applies name and tooltip to a row when the node settings are Russian; English keeps the reflected texts. */
	void LocalizeRow(IDetailPropertyRow& Row, const FPropertyTextMap& Texts, FName PropertyName);

	/** Name and tooltip overrides for CreatePropertyNameWidget (empty in English). */
	FText GetNameOverride(const FPropertyTextMap& Texts, FName PropertyName);
	FText GetToolTipOverride(const FPropertyTextMap& Texts, FName PropertyName);

	/** Asset the handle edits (first outer object). */
	UCustomNodeAsset* GetAsset(const TSharedPtr<IPropertyHandle>& Handle);

	/** Value readers; false for an invalid handle or several different values (then everything stays visible). */
	bool ReadBool(const TSharedPtr<IPropertyHandle>& Handle, bool& OutValue);
	bool ReadByte(const TSharedPtr<IPropertyHandle>& Handle, uint8& OutValue);
	bool ReadName(const TSharedPtr<IPropertyHandle>& Handle, FName& OutValue);
	/** An object or soft object reference is set. */
	bool ReadObjectSet(const TSharedPtr<IPropertyHandle>& Handle, bool& OutValue);

	/** Visible while the predicate is true. */
	TAttribute<EVisibility> MakeVisibility(TFunction<bool()> Predicate);

	/** Native child handles of a struct property by member name. */
	TSharedPtr<IPropertyHandle> Child(const TSharedRef<IPropertyHandle>& StructHandle, FName MemberName);

	/** Display name of a value of an enum of the plugin in the plugin language; reflection for other enums. */
	FText GetEnumValueText(const UEnum* Enum, int64 Value);

	/** Enum of an enum or byte property. */
	UEnum* GetPropertyEnum(const TSharedPtr<IPropertyHandle>& Handle);

	/** Combo button with the translated values of an enum property (used in the Russian modes). */
	TSharedRef<SWidget> MakeEnumValueWidget(const TSharedRef<IPropertyHandle>& Handle);

	/** Which pins the pin picker offers. */
	enum class EPinFilter : uint8
	{
		Any,
		Inputs,
		Outputs
	};

	/**
	 * Value widget of an FName property that names a pin of the macro Execute: the text field, a button with the
	 * list of the pins and a warning when the name does not exist (CNA014). bIsPinReference = false turns the list
	 * and the warning off (the name of a Curve widget is not a pin).
	 */
	TSharedRef<SWidget> MakePinNameValueWidget(const TSharedRef<IPropertyHandle>& Handle, EPinFilter Filter, TAttribute<bool> bIsPinReference);

	/** Signature pin of the asset by name, or false. */
	bool FindSignaturePin(const UCustomNodeAsset* Asset, FName PinName, FCNASignaturePin& OutPin);

	/** Adds a child row: localized texts, visibility, translated enum combo in the Russian modes. */
	IDetailPropertyRow& AddChildRow(IDetailChildrenBuilder& Children, const TSharedRef<IPropertyHandle>& Handle,
		const FPropertyTextMap& Texts, const TAttribute<EVisibility>& Visibility);

	/** Properties marked InlineEditConditionToggle are drawn by the row of the property they enable. */
	bool IsInlineToggle(const TSharedPtr<IPropertyHandle>& Handle);
}

/** Pin Override: Dynamic fields with Dynamic Group, Class Filter with its rule, input/output-only settings by pin. */
class NODEFACTORYEDITOR_API FCNAPinOverrideDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils) override;
};

/** Pin Redirect: names in the plugin language, pin list for the new name. */
class NODEFACTORYEDITOR_API FCNAPinRedirectDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils) override;
};

/** Badge: names in the plugin language. */
class NODEFACTORYEDITOR_API FCNABadgeDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils) override;
};

/** Body Widget: only the settings used by the selected widget type. */
class NODEFACTORYEDITOR_API FCNABodyWidgetDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils) override;
};

/** Curve settings: asset or own curve, the channels of the value type, the fields and pin names of the mode. */
class NODEFACTORYEDITOR_API FCNACurveSettingsDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils) override;
};
