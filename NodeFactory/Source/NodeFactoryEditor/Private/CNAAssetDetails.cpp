// Copyright (c) NodeFactory. Node Settings: the Details of a Custom Node Asset (spec 3, 7.1).

#include "CNAAssetDetails.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryStyle.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "EditorStyleSet.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

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
}

TSharedRef<IDetailCustomization> FCNAAssetDetails::MakeInstance()
{
	return MakeShareable(new FCNAAssetDetails);
}

void FCNAAssetDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Options of the Blueprint that have no meaning for a node (the asset is never placed or spawned).
	for (const TCHAR* Category : { TEXT("BlueprintOptions"), TEXT("ClassOptions"), TEXT("Thumbnail"), TEXT("Interfaces"), TEXT("Actor"), TEXT("Blueprint") })
	{
		DetailBuilder.HideCategory(Category);
	}

	// Spec 3: the order of the property tables.
	DetailBuilder.EditCategory(TEXT("Node"), LOCTEXT("Details.CategoryNode", "Node"), ECategoryPriority::Important);
	IDetailCategoryBuilder& Appearance = DetailBuilder.EditCategory(TEXT("Appearance"), LOCTEXT("Details.CategoryAppearance", "Appearance"), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Flags"), LOCTEXT("Details.CategoryFlags", "Flags"), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Placement"), LOCTEXT("Details.CategoryPlacement", "Placement"), ECategoryPriority::TypeSpecific);
	DetailBuilder.EditCategory(TEXT("Pins"), LOCTEXT("Details.CategoryPins", "Pins"), ECategoryPriority::Default);
	DetailBuilder.EditCategory(TEXT("Metadata"), LOCTEXT("Details.CategoryMetadata", "Metadata"), ECategoryPriority::Default);
	DetailBuilder.EditCategory(TEXT("Internal"), LOCTEXT("Details.CategoryInternal", "Internal"), ECategoryPriority::Uncommon);

	// Icon: a brush name with a picker that previews the brushes (spec 3.2, CNA019 for unknown names).
	IconHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Icon));
	if (!IconHandle.IsValid() || !IconHandle->IsValidHandle())
	{
		return;
	}
	Appearance.AddProperty(IconHandle)
		.CustomWidget()
		.NameContent()
		[
			IconHandle->CreatePropertyNameWidget()
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

TSharedRef<SWidget> FCNAAssetDetails::MakeIconMenu()
{
	using namespace CNAAssetDetailsLocal;
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

TSharedRef<IPropertyTypeCustomization> FCNABodyWidgetDetails::MakeInstance()
{
 return MakeShareable(new FCNABodyWidgetDetails);
}
void FCNABodyWidgetDetails::CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils)
{
 Header.NameContent()[Handle->CreatePropertyNameWidget()].ValueContent()[Handle->CreatePropertyValueWidget()];
}
void FCNABodyWidgetDetails::CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils)
{
 TSharedPtr<IPropertyHandle> TypeHandle = Handle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Type));
 uint8 RawType = 0;
 const bool bKnown = TypeHandle.IsValid() && TypeHandle->GetValue(RawType) == FPropertyAccess::Success;
 if (TypeHandle.IsValid())
 {
  TWeakPtr<IPropertyUtilities> Utilities = Utils.GetPropertyUtilities();
  TypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utilities]() { if (Utilities.IsValid()) Utilities.Pin()->RequestRefresh(); }));
 }
 const ECNABodyWidgetType Type = static_cast<ECNABodyWidgetType>(RawType);
 uint32 Count = 0; Handle->GetNumChildren(Count);
 for (uint32 Index = 0; Index < Count; ++Index)
 {
  TSharedPtr<IPropertyHandle> Child = Handle->GetChildHandle(Index);
  if (!Child.IsValid() || !Child->GetProperty()) continue;
  const FName Name = Child->GetProperty()->GetFName();
  if (bKnown && Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Options) && Type != ECNABodyWidgetType::ComboBox && Type != ECNABodyWidgetType::ScrollList) continue;
  if (bKnown && Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Height) && Type != ECNABodyWidgetType::ScrollList) continue;
  if (bKnown && (Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, SliderMin) || Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, SliderMax)) && Type != ECNABodyWidgetType::Slider) continue;
  Children.AddProperty(Child.ToSharedRef());
 }
}

#undef LOCTEXT_NAMESPACE
