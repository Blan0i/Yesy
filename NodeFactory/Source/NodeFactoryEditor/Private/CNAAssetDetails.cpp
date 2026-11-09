// Copyright (c) NodeFactory. Node Settings: the Details of a Custom Node Asset (spec 3, 7.1).

#include "CNAAssetDetails.h"
#include "CNAEditorSettings.h"
#include "CNAStructCustomizations.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryStyle.h"
#include "SCNAPinManager.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNAAssetDetailsLocal
{
	typedef TSharedPtr<FName> FIconItem;

	/** Filterable list of the icon catalog; None clears the icon. */
	class SCNAIconPicker : public SCompoundWidget
	{
	public:
		DECLARE_DELEGATE_OneParam(FOnIconPicked, FName);

		SLATE_BEGIN_ARGS(SCNAIconPicker) {}
			SLATE_EVENT(FOnIconPicked, OnIconPicked)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OnIconPicked = InArgs._OnIconPicked;
			Rebuild(FString());

			ChildSlot
			[
				SNew(SBox)
				.WidthOverride(280.0f)
				.MaxDesiredHeight(420.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f)
					[
						SAssignNew(SearchBox, SSearchBox)
						.HintText(LOCTEXT("Details.IconSearch", "Search icons"))
						.OnTextChanged_Lambda([this](const FText& Text) { Rebuild(Text.ToString()); })
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SAssignNew(List, SListView<FIconItem>)
						.ListItemsSource(&Items)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SCNAIconPicker::GenerateRow)
						.OnSelectionChanged_Lambda([this](FIconItem Item, ESelectInfo::Type SelectInfo)
						{
							// Arrow keys only move the selection; a click or Enter picks the icon.
							if (Item.IsValid() && SelectInfo != ESelectInfo::OnNavigation)
							{
								OnIconPicked.ExecuteIfBound(*Item);
							}
						})
					]
				]
			];
		}

		TSharedPtr<SWidget> GetSearchBox() const { return SearchBox; }

	private:
		void Rebuild(const FString& Filter)
		{
			Items.Reset();
			Items.Add(MakeShareable(new FName(NAME_None)));
			for (const FName& Name : FNodeFactoryStyle::GetIconCatalog())
			{
				if (Filter.IsEmpty() || Name.ToString().Contains(Filter) || FNodeFactoryStyle::GetIconDisplayName(Name).ToString().Contains(Filter))
				{
					Items.Add(MakeShareable(new FName(Name)));
				}
			}
			if (List.IsValid())
			{
				List->RequestListRefresh();
			}
		}

		TSharedRef<ITableRow> GenerateRow(FIconItem Item, const TSharedRef<STableViewBase>& OwnerTable)
		{
			// Rows are generated after MakeIconMenu returned.
			FCNALocalization::FScopedDetailsLanguage Scope;
			const FName Name = Item.IsValid() ? *Item : NAME_None;
			return SNew(STableRow<FIconItem>, OwnerTable)
				.ToolTipText(FText::FromName(Name))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 2.0f)
					[
						SNew(SBox)
						.WidthOverride(16.0f)
						.HeightOverride(16.0f)
						[
							SNew(SImage)
							.Image(Name.IsNone() ? FEditorStyle::GetNoBrush() : FNodeFactoryStyle::FindBrush(Name))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(Name.IsNone() ? LOCTEXT("Details.IconNone", "None") : FNodeFactoryStyle::GetIconDisplayName(Name))
					]
				];
		}

		FOnIconPicked OnIconPicked;
		TSharedPtr<SSearchBox> SearchBox;
		TSharedPtr<SListView<FIconItem>> List;
		TArray<FIconItem> Items;
	};

	/**
	 * Adds a property of the asset to a category in this order: name and tooltip in the plugin language, visibility,
	 * translated enum values in the Russian modes. Other rows keep the engine widgets (inline check boxes included).
	 */
	void AddAssetRow(IDetailLayoutBuilder& DetailBuilder, IDetailCategoryBuilder& Category, FName Name, const CNADetails::FPropertyTextMap& Texts,
		const TAttribute<EVisibility>& Visibility, bool bAdvanced)
	{
		using namespace CNADetails;
		const TSharedPtr<IPropertyHandle> Handle = DetailBuilder.GetProperty(Name);
		if (!Handle.IsValid() || !Handle->IsValidHandle())
		{
			return;
		}
		// V-check: IDetailCategoryBuilder::AddProperty(TSharedPtr<IPropertyHandle>, EPropertyLocation::Type) in 4.20.3.
		IDetailPropertyRow& Row = Category.AddProperty(Handle, bAdvanced ? EPropertyLocation::Advanced : EPropertyLocation::Default);
		LocalizeRow(Row, Texts, Name);
		Row.Visibility(Visibility);
		if (FCNALocalization::IsDetailsRussian() && GetPropertyEnum(Handle) && !Handle->IsEditConst())
		{
			Row.CustomWidget()
				.NameContent()
				[
					Handle->CreatePropertyNameWidget(GetNameOverride(Texts, Name), GetToolTipOverride(Texts, Name))
				]
				.ValueContent()
				.MinDesiredWidth(150.0f)
				[
					MakeEnumValueWidget(Handle.ToSharedRef())
				];
		}
	}
}

TSharedRef<IDetailCustomization> FCNAAssetDetails::MakeInstance()
{
	return MakeShareable(new FCNAAssetDetails);
}

void FCNAAssetDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	using namespace CNADetails;
	using namespace CNAAssetDetailsLocal;

	// Every text built here belongs to the node settings: Russian in the modes Russian and Russian & English.
	FCNALocalization::FScopedDetailsLanguage Scope;
	FPropertyTextMap Texts;
	GetAssetTexts(Texts);

	// V-check: IDetailLayoutBuilder::GetPropertyUtilities and GetObjectsBeingCustomized exist in 4.20.3.
	PropertyUtilities = DetailBuilder.GetPropertyUtilities();
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	Asset = Objects.Num() == 1 ? Cast<UCustomNodeAsset>(Objects[0].Get()) : nullptr;
	const TWeakObjectPtr<UCustomNodeAsset> WeakAsset = Asset;

	// Options of the Blueprint that have no meaning for a node (the asset is never placed or spawned).
	for (const TCHAR* Category : { TEXT("BlueprintOptions"), TEXT("ClassOptions"), TEXT("Thumbnail"), TEXT("Interfaces"), TEXT("Actor"), TEXT("Blueprint") })
	{
		DetailBuilder.HideCategory(Category);
	}

	// Spec 3: the order of the property tables. The names are the categories of the properties, so each one is
	// renamed and ordered here whether the panel shows "Appearance|Icon" as a category of its own or not.
	IDetailCategoryBuilder& NodeCategory = DetailBuilder.EditCategory(TEXT("Node"), LOCTEXT("Details.CategoryNode", "Node"), ECategoryPriority::Important);
	IDetailCategoryBuilder& AppearanceCategory = DetailBuilder.EditCategory(TEXT("Appearance"), LOCTEXT("Details.CategoryAppearance", "Appearance"), ECategoryPriority::TypeSpecific);
	IDetailCategoryBuilder& IconCategory = DetailBuilder.EditCategory(TEXT("Appearance|Icon"), LOCTEXT("Details.CategoryIcon", "Icon"), ECategoryPriority::TypeSpecific);
	IDetailCategoryBuilder& CompactCategory = DetailBuilder.EditCategory(TEXT("Appearance|Compact"), LOCTEXT("Details.CategoryCompact", "Compact"), ECategoryPriority::TypeSpecific);
	IDetailCategoryBuilder& WidgetsCategory = DetailBuilder.EditCategory(TEXT("Appearance|Widgets"), LOCTEXT("Details.CategoryWidgets", "Widgets"), ECategoryPriority::TypeSpecific);
	IDetailCategoryBuilder& StyleCategory = DetailBuilder.EditCategory(TEXT("Appearance|Style"), LOCTEXT("Details.CategoryStyle", "Style"), ECategoryPriority::TypeSpecific);
	IDetailCategoryBuilder& FlagsCategory = DetailBuilder.EditCategory(TEXT("Flags"), LOCTEXT("Details.CategoryFlags", "Flags"), ECategoryPriority::TypeSpecific);
	IDetailCategoryBuilder& PlacementCategory = DetailBuilder.EditCategory(TEXT("Placement"), LOCTEXT("Details.CategoryPlacement", "Placement"), ECategoryPriority::TypeSpecific);
	IDetailCategoryBuilder& PinsCategory = DetailBuilder.EditCategory(TEXT("Pins"), LOCTEXT("Details.CategoryPins", "Pins"), ECategoryPriority::Default);
	IDetailCategoryBuilder& MetadataCategory = DetailBuilder.EditCategory(TEXT("Metadata"), LOCTEXT("Details.CategoryMetadata", "Metadata"), ECategoryPriority::Default);
	IDetailCategoryBuilder& InternalCategory = DetailBuilder.EditCategory(TEXT("Internal"), LOCTEXT("Details.CategoryInternal", "Internal"), ECategoryPriority::Uncommon);

	auto Handle = [&DetailBuilder](FName Name) -> TSharedPtr<IPropertyHandle>
	{
		return DetailBuilder.GetProperty(Name);
	};
	auto Add = [&DetailBuilder, &Texts](IDetailCategoryBuilder& Category, FName Name, const TAttribute<EVisibility>& Visibility)
	{
		AddAssetRow(DetailBuilder, Category, Name, Texts, Visibility, /*bAdvanced*/ false);
	};
	const TAttribute<EVisibility> Always = EVisibility::Visible;

	// Values the visibility depends on. Several selected assets with different values count as "applies".
	const TSharedPtr<IPropertyHandle> PureHandle = Handle(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bPure));
	const TSharedPtr<IPropertyHandle> CompactHandle = Handle(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bCompact));
	const TSharedPtr<IPropertyHandle> DeprecatedHandle = Handle(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bDeprecated));
	const TSharedPtr<IPropertyHandle> NodeStyleHandle = Handle(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, NodeStyle));
	const TSharedPtr<IPropertyHandle> NiagaraStyleHandle = Handle(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, NiagaraStyle));
	const TSharedPtr<IPropertyHandle> IconTextureHandle = Handle(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, IconTexture));
	const TSharedPtr<IPropertyHandle> CornerTextureHandle = Handle(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIconTexture));
	const TSharedPtr<IPropertyHandle> ColorWiresHandle = Handle(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bColorWires));
	IconHandle = Handle(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Icon));

	auto IsTrue = [](const TSharedPtr<IPropertyHandle>& Toggle) -> bool
	{
		bool bValue = false;
		return !ReadBool(Toggle, bValue) || bValue;
	};
	auto IsSet = [](const TSharedPtr<IPropertyHandle>& Texture) -> bool
	{
		bool bValue = false;
		return !ReadObjectSet(Texture, bValue) || bValue;
	};
	auto IsUnset = [](const TSharedPtr<IPropertyHandle>& Texture) -> bool
	{
		bool bValue = false;
		return !ReadObjectSet(Texture, bValue) || !bValue;
	};
	auto IsNodeStyle = [NodeStyleHandle](ECNANodeStyle Expected) -> bool
	{
		uint8 Value = 0;
		return !ReadByte(NodeStyleHandle, Value) || static_cast<ECNANodeStyle>(Value) == Expected;
	};
	auto IsNiagaraStyle = [NiagaraStyleHandle](ECNANiagaraNodeStyle Expected) -> bool
	{
		uint8 Value = 0;
		return !ReadByte(NiagaraStyleHandle, Value) || static_cast<ECNANiagaraNodeStyle>(Value) == Expected;
	};

	// Node
	Add(NodeCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, DisplayName), Always);
	Add(NodeCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Subtitle), Always);
	Add(NodeCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Tooltip), Always);
	Add(NodeCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Category), Always);
	Add(NodeCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Keywords), Always);
	Add(NodeCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bHiddenInMenu), Always);
	Add(NodeCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, DefaultComment), Always);
	Add(NodeCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bAllowInstanceLabel), Always);

	// Appearance. HeaderColor keeps the default row: its check box (bUseHeaderColor, InlineEditConditionToggle)
	// is drawn by that row, so the row stays visible and the color is only editable while the box is checked.
	// The toggle never needs a row of its own; the edit condition reads the value, not the row.
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bUseHeaderColor));
	Add(AppearanceCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, HeaderColor), Always);
	Add(AppearanceCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, HeaderTextColor), Always);
	Add(AppearanceCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinLabelColor), Always);
	Add(AppearanceCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bColorWires), MakeVisibility([WeakAsset, ColorWiresHandle]()
	{
		// Only wires of pins with their own Pin Color change; a checked box stays visible so it can be cleared.
		const UCustomNodeAsset* Target = WeakAsset.Get();
		if (!Target)
		{
			return true;
		}
		bool bOn = false;
		if (ReadBool(ColorWiresHandle, bOn) && bOn)
		{
			return true;
		}
		return Target->PinOverrides.ContainsByPredicate([](const FCNAPinOverride& Override) { return Override.bOverridePinColor; });
	}));

	// Icon: the brush is drawn only without an icon texture, the tints only for what is drawn.
	if (IconHandle.IsValid() && IconHandle->IsValidHandle())
	{
		const FName IconName = GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Icon);
		IDetailPropertyRow& IconRow = IconCategory.AddProperty(IconHandle);
		IconRow.Visibility(MakeVisibility([IconTextureHandle, IsUnset]() { return IsUnset(IconTextureHandle); }));
		IconRow.CustomWidget()
			.NameContent()
			[
				IconHandle->CreatePropertyNameWidget(GetNameOverride(Texts, IconName), GetToolTipOverride(Texts, IconName))
			]
			.ValueContent()
			.MinDesiredWidth(200.0f)
			.MaxDesiredWidth(400.0f)
			[
				SAssignNew(IconCombo, SComboButton)
				.OnGetMenuContent(this, &FCNAAssetDetails::MakeIconMenu)
				.ContentPadding(FMargin(2.0f, 1.0f))
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(16.0f)
						.HeightOverride(16.0f)
						[
							SNew(SImage)
							.Image(this, &FCNAAssetDetails::GetCurrentIconBrush)
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text(this, &FCNAAssetDetails::GetCurrentIconText)
					]
				]
			];
	}
	const TSharedPtr<IPropertyHandle> IconNameHandle = IconHandle;
	Add(IconCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, IconTint), MakeVisibility([IconTextureHandle, IconNameHandle, IsUnset]()
	{
		FName Icon;
		const bool bHasIcon = !ReadName(IconNameHandle, Icon) || !Icon.IsNone();
		return IsUnset(IconTextureHandle) && bHasIcon;
	}));
	Add(IconCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, IconTexture), Always);
	Add(IconCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, IconTextureTint), MakeVisibility([IconTextureHandle, IsSet]() { return IsSet(IconTextureHandle); }));
	Add(IconCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIcon), MakeVisibility([CornerTextureHandle, IsUnset]() { return IsUnset(CornerTextureHandle); }));
	Add(IconCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIconTexture), Always);
	Add(IconCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIconTextureTint), MakeVisibility([CornerTextureHandle, IsSet]() { return IsSet(CornerTextureHandle); }));
	Add(IconCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIconTextureSize), MakeVisibility([CornerTextureHandle, IsSet]() { return IsSet(CornerTextureHandle); }));

	// Compact: only a pure node is drawn compact (CNA warning otherwise); a checked box stays visible.
	Add(CompactCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bCompact), MakeVisibility([PureHandle, CompactHandle, IsTrue]()
	{
		bool bCompactOn = false;
		return IsTrue(PureHandle) || (ReadBool(CompactHandle, bCompactOn) && bCompactOn);
	}));
	Add(CompactCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CompactTitle), MakeVisibility([PureHandle, CompactHandle, IsTrue]()
	{
		return IsTrue(PureHandle) && IsTrue(CompactHandle);
	}));

	// Widgets
	Add(WidgetsCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Badges), Always);
	Add(WidgetsCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, MinWidth), Always);
	Add(WidgetsCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, MinHeight), Always);
	Add(WidgetsCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, BodyWidgets), Always);

	// Style: the settings of the Animation and Niagara families only for that family.
	const TAttribute<EVisibility> ForAnimation = MakeVisibility([IsNodeStyle]() { return IsNodeStyle(ECNANodeStyle::Animation); });
	const TAttribute<EVisibility> ForNiagara = MakeVisibility([IsNodeStyle]() { return IsNodeStyle(ECNANodeStyle::Niagara); });
	Add(StyleCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, NodeStyle), Always);
	Add(StyleCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, AnimStyle), ForAnimation);
	Add(StyleCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bExecPinsAsPosePins), ForAnimation);
	Add(StyleCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bShowFastPathIcon), ForAnimation);
	Add(StyleCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, NiagaraStyle), ForNiagara);
	// The "Add Pin" rows only exist for the dynamic pin groups of the node.
	Add(StyleCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bNiagaraAddPinRows), MakeVisibility([IsNodeStyle, WeakAsset]()
	{
		const UCustomNodeAsset* Target = WeakAsset.Get();
		const bool bHasDynamicGroup = !Target
			|| Target->PinOverrides.ContainsByPredicate([](const FCNAPinOverride& Override) { return Override.bDynamicGroup; });
		return IsNodeStyle(ECNANodeStyle::Niagara) && bHasDynamicGroup;
	}));
	Add(StyleCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bNiagaraNamespaceLabels), ForNiagara);
	Add(StyleCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bExecPinsAsParameterMap), ForNiagara);
	Add(StyleCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, NiagaraBodyText), MakeVisibility([IsNodeStyle, IsNiagaraStyle]()
	{
		return IsNodeStyle(ECNANodeStyle::Niagara) && IsNiagaraStyle(ECNANiagaraNodeStyle::CustomHlsl);
	}));

	// Flags
	Add(FlagsCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bPure), Always);
	Add(FlagsCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bDevelopmentOnly), Always);
	Add(FlagsCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bDeprecated), Always);
	Add(FlagsCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, DeprecationMessage), MakeVisibility([DeprecatedHandle, IsTrue]() { return IsTrue(DeprecatedHandle); }));

	// Placement
	Add(PlacementCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, AllowedGraphs), Always);
	Add(PlacementCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, ContextClass), Always);

	// Pins, then the settings that name a pin the macro Execute does not have.
	Add(PinsCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinOverrides), Always);
	Add(PinsCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinRedirects), Always);
	PinsCategory.AddCustomRow(LOCTEXT("Details.StaleReferences", "Stale References"))
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([WeakAsset]()
		{
			TArray<FCNAStaleReference> References;
			FCNAPinManagerOps::FindStaleReferences(WeakAsset.Get(), References);
			return References.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
		})))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("Details.StaleReferences", "Stale References"))
			.ToolTipText(LOCTEXT("Details.StaleReferencesTip", "Settings that name a pin the macro Execute does not have, for example an override left by an interrupted rename (CNA014, CNA015). Point each one to a pin or remove it; nothing is changed without your choice and every action is one Undo step."))
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SNew(SComboButton)
			.ContentPadding(FMargin(2.0f, 1.0f))
			.OnGetMenuContent(this, &FCNAAssetDetails::MakeStaleReferenceMenu)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Icons.Warning"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([this]()
					{
						FCNALocalization::FScopedDetailsLanguage TextScope;
						return FText::Format(LOCTEXT("Details.StaleCount", "{0} to fix..."), FText::AsNumber(GetStaleReferenceCount()));
					})
				]
			]
		];

	// Metadata
	Add(MetadataCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Author), Always);
	Add(MetadataCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Description), Always);
	Add(MetadataCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Tags), Always);
	Add(MetadataCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, SourceUrl), Always);

	// Internal (advanced, read only)
	AddAssetRow(DetailBuilder, InternalCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, AssetVersion), Texts, Always, /*bAdvanced*/ true);
	AddAssetRow(DetailBuilder, InternalCategory, GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, AssetGuid), Texts, Always, /*bAdvanced*/ true);
}

TSharedRef<SWidget> FCNAAssetDetails::MakeIconMenu()
{
	using namespace CNAAssetDetailsLocal;
	FCNALocalization::FScopedDetailsLanguage Scope;
	TSharedRef<SCNAIconPicker> Picker = SNew(SCNAIconPicker)
		.OnIconPicked(SCNAIconPicker::FOnIconPicked::CreateSP(this, &FCNAAssetDetails::SetIcon));
	if (IconCombo.IsValid())
	{
		IconCombo->SetMenuContentWidgetToFocus(Picker->GetSearchBox());
	}
	return Picker;
}

FName FCNAAssetDetails::GetCurrentIcon() const
{
	FName Value = NAME_None;
	if (IconHandle.IsValid() && IconHandle->GetValue(Value) == FPropertyAccess::Success)
	{
		return Value;
	}
	return NAME_None;
}

const FSlateBrush* FCNAAssetDetails::GetCurrentIconBrush() const
{
	const FName Icon = GetCurrentIcon();
	const FSlateBrush* Brush = Icon.IsNone() ? nullptr : FNodeFactoryStyle::FindBrush(Icon);
	// An unknown name shows the warning icon; the validator reports CNA019.
	return Brush ? Brush : (Icon.IsNone() ? FEditorStyle::GetNoBrush() : FEditorStyle::GetBrush(TEXT("Icons.Warning")));
}

FText FCNAAssetDetails::GetCurrentIconText() const
{
	FCNALocalization::FScopedDetailsLanguage Scope;
	FName Value = NAME_None;
	if (!IconHandle.IsValid() || IconHandle->GetValue(Value) == FPropertyAccess::MultipleValues)
	{
		return LOCTEXT("Details.IconMultiple", "Multiple Values");
	}
	if (Value.IsNone())
	{
		return LOCTEXT("Details.IconNone", "None");
	}
	return FNodeFactoryStyle::DoesBrushExist(Value)
		? FNodeFactoryStyle::GetIconDisplayName(Value)
		: FText::Format(LOCTEXT("Details.IconUnknown", "{0} (unknown brush)"), FText::FromName(Value));
}

void FCNAAssetDetails::SetIcon(FName NewIcon)
{
	if (IconHandle.IsValid())
	{
		IconHandle->SetValue(NewIcon);
	}
	if (IconCombo.IsValid())
	{
		IconCombo->SetIsOpen(false);
	}
}

int32 FCNAAssetDetails::GetStaleReferenceCount() const
{
	TArray<FCNAStaleReference> References;
	FCNAPinManagerOps::FindStaleReferences(Asset.Get(), References);
	return References.Num();
}

TSharedRef<SWidget> FCNAAssetDetails::MakeStaleReferenceMenu()
{
	FCNALocalization::FScopedDetailsLanguage Scope;
	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
	const TWeakObjectPtr<UCustomNodeAsset> WeakAsset = Asset;
	const TWeakPtr<IPropertyUtilities> WeakUtilities = PropertyUtilities;

	// Every action is chosen by the user, runs in its own transaction and refreshes the panel afterwards.
	auto Finish = [WeakUtilities](bool bDone, const FText& Error)
	{
		if (!bDone)
		{
			FMessageDialog::Open(EAppMsgType::Ok, Error);
		}
		if (TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
		{
			// V-check: IPropertyUtilities::RequestRefresh (deferred rebuild of the panel) exists in 4.20.3.
			Utilities->RequestRefresh();
		}
	};

	TArray<FCNAStaleReference> References;
	FCNAPinManagerOps::FindStaleReferences(WeakAsset.Get(), References);
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("Details.StaleSection", "Settings that name a missing pin"));
	if (References.Num() == 0)
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("Details.StaleNone", "Every setting names an existing pin."), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}
	for (const FCNAStaleReference& Reference : References)
	{
		MenuBuilder.AddSubMenu(
			Reference.Describe(),
			LOCTEXT("Details.StaleEntryTip", "Point this setting to an existing pin or remove it."),
			FNewMenuDelegate::CreateLambda([WeakAsset, Reference, Finish](FMenuBuilder& SubMenu)
			{
				FCNALocalization::FScopedDetailsLanguage SubScope;
				UCustomNodeAsset* Target = WeakAsset.Get();
				if (!Target)
				{
					return;
				}
				auto MakeRemap = [WeakAsset, Reference, Finish](FName NewPin)
				{
					return FUIAction(FExecuteAction::CreateLambda([WeakAsset, Reference, Finish, NewPin]()
					{
						FText Error;
						const bool bDone = WeakAsset.IsValid() && FCNAPinManagerOps::RemapStaleReference(WeakAsset.Get(), Reference, NewPin, Error);
						Finish(bDone, Error);
					}));
				};

				const FName Suggested = FCNAPinManagerOps::SuggestRemap(Target, Reference);
				if (!Suggested.IsNone())
				{
					SubMenu.BeginSection(NAME_None, LOCTEXT("Details.StaleSuggestedSection", "Suggested"));
					SubMenu.AddMenuEntry(
						FText::Format(LOCTEXT("Details.StaleUseSuggested", "Use {0}"), FText::FromName(Suggested)),
						FText::Format(LOCTEXT("Details.StaleUseSuggestedTip", "Replace {0} by {1}, the existing pin with the closest name."), FText::FromName(Reference.Name), FText::FromName(Suggested)),
						FSlateIcon(), MakeRemap(Suggested));
					SubMenu.EndSection();
				}

				TArray<FName> Candidates;
				FCNAPinManagerOps::GetRemapCandidates(Target, Reference, Candidates);
				SubMenu.BeginSection(NAME_None, LOCTEXT("Details.StalePinsSection", "Point to pin"));
				if (Candidates.Num() == 0)
				{
					SubMenu.AddMenuEntry(LOCTEXT("Details.StaleNoCandidates", "No pin fits this setting."), FText::GetEmpty(), FSlateIcon(),
						FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
				}
				for (const FName Candidate : Candidates)
				{
					SubMenu.AddMenuEntry(FText::FromName(Candidate), FText::GetEmpty(), FSlateIcon(), MakeRemap(Candidate));
				}
				SubMenu.EndSection();

				SubMenu.BeginSection(NAME_None, LOCTEXT("Details.StaleRemoveSection", "Remove"));
				const bool bPassThrough = Reference.Kind == ECNAStaleReferenceKind::PassThrough;
				SubMenu.AddMenuEntry(
					bPassThrough ? LOCTEXT("Details.StaleClear", "Clear Pass Through Of") : LOCTEXT("Details.StaleRemove", "Remove this entry"),
					bPassThrough
						? LOCTEXT("Details.StaleClearTip", "The override stays, only its Pass Through Of is cleared.")
						: LOCTEXT("Details.StaleRemoveTip", "Removes the pin override, body widget or pin redirect with all of its settings."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "Icons.Delete"),
					FUIAction(FExecuteAction::CreateLambda([WeakAsset, Reference, Finish]()
					{
						FText Error;
						const bool bDone = WeakAsset.IsValid() && FCNAPinManagerOps::RemoveStaleReference(WeakAsset.Get(), Reference, Error);
						Finish(bDone, Error);
					})));
				SubMenu.EndSection();
			}));
	}
	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

// FCNAEditorSettingsDetails

TSharedRef<IDetailCustomization> FCNAEditorSettingsDetails::MakeInstance()
{
	return MakeShareable(new FCNAEditorSettingsDetails);
}

void FCNAEditorSettingsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	using namespace CNADetails;
	// Not node settings: Russian only in the mode Russian, so no details scope here.
	const bool bRussian = FCNALocalization::IsRussianActive();

	FPropertyTextMap Texts;
	Texts.Add(GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, Language), FPropertyText(LOCTEXT("Prop.Settings.Language", "Language"), LOCTEXT("Prop.Settings.Language.Tip", "Language of the NodeFactory UI, independent of the editor language. Russian & English: the node settings in the Details panel of a Custom Node Asset are Russian, everything else stays English. The Details panel of an open asset changes at once, other open windows after reopening them, menu commands after restarting the editor.")));
	Texts.Add(GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, MaxExpansionDepth), FPropertyText(LOCTEXT("Prop.Settings.MaxExpansionDepth", "Max Expansion Depth"), LOCTEXT("Prop.Settings.MaxExpansionDepth.Tip", "Limit of nested custom nodes; deeper nesting fails with CNA002.")));
	Texts.Add(GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, bConfirmBulkUpdate), FPropertyText(LOCTEXT("Prop.Settings.bConfirmBulkUpdate", "Confirm Bulk Update"), LOCTEXT("Prop.Settings.bConfirmBulkUpdate.Tip", "Ask before updating the instances of a changed asset.")));
	Texts.Add(GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, MaxImportFileSizeKB), FPropertyText(LOCTEXT("Prop.Settings.MaxImportFileSizeKB", "Max Import File Size KB"), LOCTEXT("Prop.Settings.MaxImportFileSizeKB.Tip", "Largest JSON file the importer accepts, in kilobytes.")));
	Texts.Add(GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, CatalogFolder), FPropertyText(LOCTEXT("Prop.Settings.CatalogFolder", "Catalog Folder"), LOCTEXT("Prop.Settings.CatalogFolder.Tip", "Folder with the presets of the Node Catalog. Empty: <Project>/NodeFactory/Presets, else the Presets folder of the plugin.")));
	Texts.Add(GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, bVerboseLog), FPropertyText(LOCTEXT("Prop.Settings.bVerboseLog", "Verbose Log"), LOCTEXT("Prop.Settings.bVerboseLog.Tip", "Detailed output of LogNodeFactory: expansion steps, Behavior events, update decisions.")));

	IDetailCategoryBuilder& LanguageCategory = DetailBuilder.EditCategory(TEXT("Language"), LOCTEXT("Settings.CategoryLanguage", "Language"), ECategoryPriority::Important);
	IDetailCategoryBuilder& ExpansionCategory = DetailBuilder.EditCategory(TEXT("Expansion"), LOCTEXT("Settings.CategoryExpansion", "Expansion"));
	IDetailCategoryBuilder& UpdateCategory = DetailBuilder.EditCategory(TEXT("Update"), LOCTEXT("Settings.CategoryUpdate", "Update"));
	IDetailCategoryBuilder& JsonCategory = DetailBuilder.EditCategory(TEXT("JSON"), LOCTEXT("Settings.CategoryJson", "JSON"));
	IDetailCategoryBuilder& CatalogCategory = DetailBuilder.EditCategory(TEXT("Catalog"), LOCTEXT("Settings.CategoryCatalog", "Catalog"));
	IDetailCategoryBuilder& DiagnosticsCategory = DetailBuilder.EditCategory(TEXT("Diagnostics"), LOCTEXT("Settings.CategoryDiagnostics", "Diagnostics"));

	auto Add = [&DetailBuilder, &Texts, bRussian](IDetailCategoryBuilder& Category, FName Name) -> IDetailPropertyRow*
	{
		const TSharedPtr<IPropertyHandle> Handle = DetailBuilder.GetProperty(Name);
		if (!Handle.IsValid() || !Handle->IsValidHandle())
		{
			return nullptr;
		}
		IDetailPropertyRow& Row = Category.AddProperty(Handle);
		const FPropertyText* Text = Texts.Find(Name);
		if (Text && bRussian)
		{
			Row.DisplayName(Text->Name);
			Row.ToolTip(Text->ToolTip);
		}
		return &Row;
	};

	// Language: each mode is named in its own language, whatever the current mode.
	const FName LanguageName = GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, Language);
	const TSharedPtr<IPropertyHandle> LanguageHandle = DetailBuilder.GetProperty(LanguageName);
	if (IDetailPropertyRow* LanguageRow = Add(LanguageCategory, LanguageName))
	{
		const FPropertyText* LanguageTexts = Texts.Find(LanguageName);
		const TWeakObjectPtr<UEnum> LanguageEnum = GetPropertyEnum(LanguageHandle);
		const TWeakPtr<IPropertyUtilities> WeakUtilities = DetailBuilder.GetPropertyUtilities();

		// The names of this panel follow the new language.
		LanguageHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakUtilities]()
		{
			if (TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
			{
				Utilities->RequestRefresh();
			}
		}));

		LanguageRow->CustomWidget()
			.NameContent()
			[
				LanguageHandle->CreatePropertyNameWidget(bRussian && LanguageTexts ? LanguageTexts->Name : FText::GetEmpty(),
					LanguageTexts ? LanguageTexts->ToolTip : FText::GetEmpty())
			]
			.ValueContent()
			.MinDesiredWidth(180.0f)
			[
				SNew(SComboButton)
				.ContentPadding(FMargin(2.0f, 1.0f))
				.OnGetMenuContent_Lambda([LanguageHandle, LanguageEnum]() -> TSharedRef<SWidget>
				{
					FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
					const ECNALanguageMode Modes[] = { ECNALanguageMode::Russian, ECNALanguageMode::English, ECNALanguageMode::RussianAndEnglish };
					for (const ECNALanguageMode Mode : Modes)
					{
						const int64 Value = static_cast<int64>(Mode);
						MenuBuilder.AddMenuEntry(FCNALocalization::GetModeDisplayName(Mode), FText::GetEmpty(), FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([LanguageHandle, LanguageEnum, Value]()
								{
									// The full name (ECNAPluginLanguage::X) is what the enum property imports.
									if (const UEnum* Enum = LanguageEnum.Get())
									{
										LanguageHandle->SetValueFromFormattedString(Enum->GetNameByValue(Value).ToString());
									}
								}),
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([LanguageHandle, Value]()
								{
									uint8 Current = 0;
									return ReadByte(LanguageHandle, Current) && static_cast<int64>(Current) == Value;
								})),
							NAME_None, EUserInterfaceActionType::RadioButton);
					}
					return MenuBuilder.MakeWidget();
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([LanguageHandle]()
					{
						uint8 Current = 0;
						if (!ReadByte(LanguageHandle, Current) || Current > static_cast<uint8>(ECNALanguageMode::RussianAndEnglish))
						{
							return FText::GetEmpty();
						}
						return FCNALocalization::GetModeDisplayName(static_cast<ECNALanguageMode>(Current));
					})
				]
			];
	}

	Add(ExpansionCategory, GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, MaxExpansionDepth));
	Add(UpdateCategory, GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, bConfirmBulkUpdate));
	Add(JsonCategory, GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, MaxImportFileSizeKB));
	Add(CatalogCategory, GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, CatalogFolder));
	Add(DiagnosticsCategory, GET_MEMBER_NAME_CHECKED(UCNAEditorSettings, bVerboseLog));
}

#undef LOCTEXT_NAMESPACE
