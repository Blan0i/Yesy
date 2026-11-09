// Copyright (c) NodeFactory. Local Variables panel of the asset window (spec 4.8, 7.1).

#include "SCNALocalVariables.h"
#include "CustomNodeAsset.h"
#include "CNAEditorCommands.h"
#include "CNAGraphUtils.h"
#include "CNAPinUtils.h"
#include "CNANodeMenu.h"
#include "K2Node_GetLocal.h"

#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "AssetData.h"
#include "SPinTypeSelector.h"
#include "PropertyCustomizationHelpers.h"
#include "Misc/MessageDialog.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNALocalVariablesLocal
{
	const FName ColumnName(TEXT("Name"));
	const FName ColumnType(TEXT("Type"));
	const FName ColumnDefault(TEXT("Default"));
	const FName ColumnTooltip(TEXT("Tooltip"));
	const FName ColumnUses(TEXT("Uses"));
	const FName ColumnRemove(TEXT("Remove"));

	/** Same rules as pin names: the name is written into T3D, JSON and messages. */
	const TCHAR* InvalidNameCharacters = TEXT("\"',.:|/\\\n\r\t");
	const int32 MaxNameLength = 100;

	void ShowFailure(const FText& Text)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 4.0f;
		if (TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}

	bool IsObjectCategory(FName Category)
	{
		return Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_SoftObject
			|| Category == UEdGraphSchema_K2::PC_Interface;
	}

	bool IsClassCategory(FName Category)
	{
		return Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_SoftClass;
	}

	FCNALocalVariable* FindMutable(UCustomNodeAsset* Asset, const FGuid& Guid)
	{
		const int32 Index = FCNALocalVariableOps::FindIndex(Asset, Guid);
		return Index != INDEX_NONE ? &Asset->LocalVariables[Index] : nullptr;
	}

	FText MissingVariableText()
	{
		return LOCTEXT("LocalVars.Missing", "The local variable does not exist any more.");
	}
}

// FCNALocalVariableOps

int32 FCNALocalVariableOps::FindIndex(const UCustomNodeAsset* Asset, const FGuid& Guid)
{
	if (!Asset || !Guid.IsValid())
	{
		return INDEX_NONE;
	}
	return Asset->LocalVariables.IndexOfByPredicate([&Guid](const FCNALocalVariable& Variable) { return Variable.Guid == Guid; });
}

bool FCNALocalVariableOps::VerifyName(const UCustomNodeAsset* Asset, const FGuid& Guid, const FString& NewName, FText& OutError)
{
	using namespace CNALocalVariablesLocal;
	const FString Name = NewName.TrimStartAndEnd();
	if (!Asset)
	{
		OutError = MissingVariableText();
		return false;
	}
	if (Name.IsEmpty() || Name.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		OutError = LOCTEXT("LocalVars.EmptyName", "Enter a name for the variable.");
		return false;
	}
	if (Name.Len() > MaxNameLength)
	{
		OutError = FText::Format(LOCTEXT("LocalVars.LongName", "The name is longer than {0} characters."), FText::AsNumber(MaxNameLength));
		return false;
	}
	for (const TCHAR* Character = InvalidNameCharacters; *Character; ++Character)
	{
		int32 Found = INDEX_NONE;
		if (Name.FindChar(*Character, Found))
		{
			OutError = LOCTEXT("LocalVars.BadCharacter", "The name cannot contain quotes, commas, periods, colons, the characters | / \\ or tabs.");
			return false;
		}
	}
	const FName Candidate(*Name);
	for (const FCNALocalVariable& Variable : Asset->LocalVariables)
	{
		if (Variable.Guid != Guid && Variable.Name == Candidate)
		{
			OutError = FText::Format(LOCTEXT("LocalVars.NameUsed", "A local variable named {0} already exists."), FText::FromString(Name));
			return false;
		}
	}
	return true;
}

FGuid FCNALocalVariableOps::Add(UCustomNodeAsset* Asset, FName BaseName, const FEdGraphPinType& Type)
{
	if (!Asset || !FCNALocalVariable::IsTypeAllowed(Type))
	{
		return FGuid();
	}
	const FScopedTransaction Transaction(LOCTEXT("LocalVars.AddTransaction", "Add Local Variable"));
	Asset->Modify();
	FCNALocalVariable Variable(Asset->MakeUniqueLocalVariableName(BaseName), Type);
	if (!Variable.Guid.IsValid())
	{
		Variable.Guid = FGuid::NewGuid();
	}
	Asset->LocalVariables.Add(Variable);
	Finish(Asset);
	return Variable.Guid;
}

bool FCNALocalVariableOps::Rename(UCustomNodeAsset* Asset, const FGuid& Guid, FName NewName, FText& OutError)
{
	FCNALocalVariable* Variable = CNALocalVariablesLocal::FindMutable(Asset, Guid);
	if (!Variable)
	{
		OutError = CNALocalVariablesLocal::MissingVariableText();
		return false;
	}
	const FString NameString = NewName.ToString().TrimStartAndEnd();
	if (Variable->Name.ToString().Equals(NameString, ESearchCase::CaseSensitive))
	{
		return true;
	}
	if (!VerifyName(Asset, Guid, NameString, OutError))
	{
		return false;
	}

	// Collected before the rename: a node pasted from another asset may only know the old name.
	TArray<UK2Node_CNALocalVariableBase*> Nodes;
	GetVariableNodes(Asset, Guid, Nodes);

	const FScopedTransaction Transaction(LOCTEXT("LocalVars.RenameTransaction", "Rename Local Variable"));
	Asset->Modify();
	Variable->Name = FName(*NameString);
	RefreshVariableNodes(Nodes, *Variable);
	Finish(Asset);
	return true;
}

bool FCNALocalVariableOps::ChangeType(UCustomNodeAsset* Asset, const FGuid& Guid, const FEdGraphPinType& NewType, FText& OutError)
{
	FCNALocalVariable* Variable = CNALocalVariablesLocal::FindMutable(Asset, Guid);
	if (!Variable)
	{
		OutError = CNALocalVariablesLocal::MissingVariableText();
		return false;
	}
	if (!FCNALocalVariable::IsTypeAllowed(NewType))
	{
		OutError = LOCTEXT("LocalVars.TypeNotAllowed", "Exec, Wildcard and delegates cannot be stored in a local variable.");
		return false;
	}
	if (FCNAPinUtils::AreTypesIdentical(Variable->Type, NewType))
	{
		return true;
	}

	TArray<UK2Node_CNALocalVariableBase*> Nodes;
	GetVariableNodes(Asset, Guid, Nodes);

	const FScopedTransaction Transaction(LOCTEXT("LocalVars.TypeTransaction", "Change Local Variable Type"));
	Asset->Modify();
	Variable->Type = NewType;
	Variable->DefaultValue.Empty();
	RefreshVariableNodes(Nodes, *Variable);
	Finish(Asset);
	return true;
}

bool FCNALocalVariableOps::SetDefault(UCustomNodeAsset* Asset, const FGuid& Guid, const FString& NewValue, FText* OutError)
{
	FCNALocalVariable* Variable = CNALocalVariablesLocal::FindMutable(Asset, Guid);
	if (!Variable)
	{
		if (OutError)
		{
			*OutError = CNALocalVariablesLocal::MissingVariableText();
		}
		return false;
	}
	if (!IsDefaultValid(Variable->Type, NewValue, OutError))
	{
		return false;
	}
	if (Variable->DefaultValue.Equals(NewValue, ESearchCase::CaseSensitive))
	{
		return true;
	}
	const FScopedTransaction Transaction(LOCTEXT("LocalVars.DefaultTransaction", "Change Local Variable Default"));
	Asset->Modify();
	Variable->DefaultValue = NewValue;
	Finish(Asset);
	return true;
}

bool FCNALocalVariableOps::SetTooltip(UCustomNodeAsset* Asset, const FGuid& Guid, const FText& NewTooltip)
{
	FCNALocalVariable* Variable = CNALocalVariablesLocal::FindMutable(Asset, Guid);
	if (!Variable)
	{
		return false;
	}
	if (Variable->Tooltip.ToString().Equals(NewTooltip.ToString(), ESearchCase::CaseSensitive))
	{
		return true;
	}
	TArray<UK2Node_CNALocalVariableBase*> Nodes;
	GetVariableNodes(Asset, Guid, Nodes);

	const FScopedTransaction Transaction(LOCTEXT("LocalVars.TooltipTransaction", "Change Local Variable Tooltip"));
	Asset->Modify();
	Variable->Tooltip = NewTooltip;
	// The value pins show the tooltip of the variable.
	RefreshVariableNodes(Nodes, *Variable);
	Finish(Asset);
	return true;
}

bool FCNALocalVariableOps::Remove(UCustomNodeAsset* Asset, const FGuid& Guid, bool bRemoveNodes)
{
	const int32 Index = FindIndex(Asset, Guid);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	TArray<UK2Node_CNALocalVariableBase*> Nodes;
	if (bRemoveNodes)
	{
		GetVariableNodes(Asset, Guid, Nodes);
	}

	const FScopedTransaction Transaction(LOCTEXT("LocalVars.RemoveTransaction", "Remove Local Variable"));
	Asset->Modify();
	for (UK2Node_CNALocalVariableBase* Node : Nodes)
	{
		// Breaks the wires and notifies the graph; the asset is marked modified once below.
		FBlueprintEditorUtils::RemoveNode(Asset, Node, /*bDontRecompile*/ true);
	}
	Asset->LocalVariables.RemoveAt(Index);
	Finish(Asset);
	return true;
}

bool FCNALocalVariableOps::Move(UCustomNodeAsset* Asset, const FGuid& Guid, int32 NewIndex)
{
	const int32 Index = FindIndex(Asset, Guid);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	NewIndex = FMath::Clamp(NewIndex, 0, Asset->LocalVariables.Num() - 1);
	if (NewIndex == Index)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("LocalVars.MoveTransaction", "Move Local Variable"));
	Asset->Modify();
	const FCNALocalVariable Variable = Asset->LocalVariables[Index];
	Asset->LocalVariables.RemoveAt(Index);
	Asset->LocalVariables.Insert(Variable, NewIndex);
	Finish(Asset);
	return true;
}

void FCNALocalVariableOps::GetVariableNodes(const UCustomNodeAsset* Asset, const FGuid& Guid, TArray<UK2Node_CNALocalVariableBase*>& OutNodes)
{
	const int32 Index = FindIndex(Asset, Guid);
	if (Index == INDEX_NONE)
	{
		return;
	}
	const FCNALocalVariable* Variable = &Asset->LocalVariables[Index];
	TArray<UK2Node_CNALocalVariableBase*> Nodes;
	FCNAGraphUtils::GetExecuteNodesOfClass<UK2Node_CNALocalVariableBase>(Asset, Nodes);
	for (UK2Node_CNALocalVariableBase* Node : Nodes)
	{
		// FindVariable also resolves nodes that only know the name (pasted from another asset).
		if (Node && Node->FindVariable() == Variable)
		{
			OutNodes.Add(Node);
		}
	}
}

int32 FCNALocalVariableOps::CountUses(const UCustomNodeAsset* Asset, const FGuid& Guid)
{
	TArray<UK2Node_CNALocalVariableBase*> Nodes;
	GetVariableNodes(Asset, Guid, Nodes);
	return Nodes.Num();
}

bool FCNALocalVariableOps::IsDefaultValid(const FEdGraphPinType& Type, const FString& Value, FText* OutError)
{
	using namespace CNALocalVariablesLocal;
	if (Value.IsEmpty() || Type.IsContainer())
	{
		return true;
	}
	if (IsObjectCategory(Type.PinCategory) || IsClassCategory(Type.PinCategory))
	{
		if (Value == TEXT("None") || FSoftObjectPath(Value).IsValid())
		{
			return true;
		}
		if (OutError)
		{
			*OutError = LOCTEXT("LocalVars.BadPath", "Enter an object path, for example /Game/Folder/Asset.Asset.");
		}
		return false;
	}

	// V-check: the pin name parameter of DefaultValueSimpleValidation is an FName in 4.20.3.
	FString Message;
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (Schema->DefaultValueSimpleValidation(Type, NAME_None, Value, nullptr, FText::FromString(Value), &Message))
	{
		return true;
	}
	if (OutError)
	{
		*OutError = FText::FromString(Message);
	}
	return false;
}

void FCNALocalVariableOps::RefreshVariableNodes(const TArray<UK2Node_CNALocalVariableBase*>& Nodes, const FCNALocalVariable& Variable)
{
	TSet<UEdGraph*> Graphs;
	for (UK2Node_CNALocalVariableBase* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}
		Node->Modify();
		Node->InitVariable(Variable);
		Node->ReconstructNode();
		Graphs.Add(Node->GetGraph());
	}
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph)
		{
			Graph->NotifyGraphChanged();
		}
	}
}

void FCNALocalVariableOps::Finish(UCustomNodeAsset* Asset)
{
	FBlueprintEditorUtils::MarkBlueprintAsModified(Asset);
	Asset->NotifyAssetChanged(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, LocalVariables));
	FCNANodeMenu::RequestRefresh();
}

// Row

class SCNALocalVariableRow : public SMultiColumnTableRow<FCNALocalVariableItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SCNALocalVariableRow) {}
		SLATE_ARGUMENT(FCNALocalVariableItemPtr, Item)
		SLATE_ARGUMENT(TSharedPtr<SCNALocalVariables>, Panel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Item = InArgs._Item;
		Panel = InArgs._Panel;
		FSuperRowType::Construct(FTableRowArgs().Padding(FMargin(0.0f, 1.0f)), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnId) override
	{
		using namespace CNALocalVariablesLocal;
		if (!Item.IsValid() || !Panel.IsValid())
		{
			return SNullWidget::NullWidget;
		}
		TSharedRef<SWidget> Content = SNullWidget::NullWidget;
		if (ColumnId == ColumnName)
		{
			Content = MakeNameWidget();
		}
		else if (ColumnId == ColumnType)
		{
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			// Without Exec and Wildcard (spec 4.8, item 1); containers come from the selector (V-check: arguments in 4.20.3).
			Content = SNew(SPinTypeSelector, FGetPinTypeTree::CreateUObject(Schema, &UEdGraphSchema_K2::GetVariableTypeTree))
				.TargetPinType(this, &SCNALocalVariableRow::GetType)
				.OnPinTypeChanged(this, &SCNALocalVariableRow::HandleTypeChanged)
				.Schema(Schema)
				.TypeTreeFilter(ETypeTreeFilter::None)
				.bAllowArrays(true)
				.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.IsEnabled(this, &SCNALocalVariableRow::IsEditable);
		}
		else if (ColumnId == ColumnDefault)
		{
			Content = MakeDefaultWidget();
		}
		else if (ColumnId == ColumnTooltip)
		{
			Content = SNew(SEditableTextBox)
				.Text(this, &SCNALocalVariableRow::GetTooltipText)
				.HintText(LOCTEXT("LocalVars.TooltipHint", "Tooltip"))
				.OnTextCommitted(this, &SCNALocalVariableRow::HandleTooltipCommitted)
				.IsEnabled(this, &SCNALocalVariableRow::IsEditable);
		}
		else if (ColumnId == ColumnUses)
		{
			Content = SNew(STextBlock)
				.Text(FText::AsNumber(Item->Uses))
				.ColorAndOpacity(Item->Uses > 0 ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground())
				.ToolTipText(LOCTEXT("LocalVars.UsesTip", "Get Local and Set Local nodes of the Execution Graph that use the variable."));
		}
		else if (ColumnId == ColumnRemove)
		{
			Content = PropertyCustomizationHelpers::MakeDeleteButton(
				FSimpleDelegate::CreateSP(this, &SCNALocalVariableRow::HandleRemoveClicked),
				LOCTEXT("LocalVars.RemoveTip", "Remove the variable (Delete)."),
				TAttribute<bool>(this, &SCNALocalVariableRow::IsEditable));
		}
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				Content
			];
	}

private:
	TSharedRef<SWidget> MakeNameWidget()
	{
		TSharedPtr<SInlineEditableTextBlock> NameText;
		TSharedRef<SWidget> Widget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush(TEXT("Kismet.AllClasses.VariableIcon")))
				.ColorAndOpacity(FCNAPinUtils::GetTypeColor(Item->Type))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(NameText, SInlineEditableTextBlock)
				.Text(FText::FromName(Item->Name))
				.ToolTipText(LOCTEXT("LocalVars.NameTip", "Name of the variable. F2 or double click renames it; Get Local and Set Local keep it by its id."))
				.IsSelected(this, &SCNALocalVariableRow::IsSelectedExclusively)
				.IsReadOnly(this, &SCNALocalVariableRow::IsReadOnly)
				.OnVerifyTextChanged(this, &SCNALocalVariableRow::HandleVerifyName)
				.OnTextCommitted(this, &SCNALocalVariableRow::HandleNameCommitted)
			];
		Item->OnRenameRequested.BindSP(NameText.Get(), &SInlineEditableTextBlock::EnterEditingMode);
		return Widget;
	}

	TSharedRef<SWidget> MakeDefaultWidget()
	{
		using namespace CNALocalVariablesLocal;
		const FEdGraphPinType Type = GetType();
		if (Type.IsContainer())
		{
			return SNew(STextBlock)
				.Text(LOCTEXT("LocalVars.ContainerDefault", "Empty"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.ToolTipText(LOCTEXT("LocalVars.ContainerDefaultTip", "Containers start empty on every entry into the node."));
		}
		if (Type.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return SNew(SCheckBox)
				.IsChecked(this, &SCNALocalVariableRow::GetBoolState)
				.OnCheckStateChanged(this, &SCNALocalVariableRow::HandleBoolChanged)
				.IsEnabled(this, &SCNALocalVariableRow::IsEditable);
		}
		if (IsObjectCategory(Type.PinCategory))
		{
			UClass* AllowedClass = Type.PinCategory == UEdGraphSchema_K2::PC_Interface ? nullptr : Cast<UClass>(Type.PinSubCategoryObject.Get());
			// V-check: arguments of SObjectPropertyEntryBox in 4.20.3. Only assets can be defaults, as for pins.
			return SNew(SObjectPropertyEntryBox)
				.AllowedClass(AllowedClass ? AllowedClass : UObject::StaticClass())
				.ObjectPath(this, &SCNALocalVariableRow::GetDefaultString)
				.OnObjectChanged(this, &SCNALocalVariableRow::HandleObjectChanged)
				.DisplayThumbnail(false)
				.IsEnabled(this, &SCNALocalVariableRow::IsEditable);
		}
		if (IsClassCategory(Type.PinCategory))
		{
			const UClass* MetaClass = Cast<UClass>(Type.PinSubCategoryObject.Get());
			// V-check: arguments of SClassPropertyEntryBox in 4.20.3.
			return SNew(SClassPropertyEntryBox)
				.MetaClass(MetaClass ? MetaClass : UObject::StaticClass())
				.AllowNone(true)
				.SelectedClass(this, &SCNALocalVariableRow::GetDefaultClass)
				.OnSetClass(this, &SCNALocalVariableRow::HandleClassChanged)
				.IsEnabled(this, &SCNALocalVariableRow::IsEditable);
		}
		if (Cast<UEnum>(Type.PinSubCategoryObject.Get()))
		{
			return SNew(SComboButton)
				.OnGetMenuContent(this, &SCNALocalVariableRow::MakeEnumMenu)
				.IsEnabled(this, &SCNALocalVariableRow::IsEditable)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(this, &SCNALocalVariableRow::GetEnumText)
					.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				];
		}
		return SAssignNew(ValueBox, SEditableTextBox)
			.Text(this, &SCNALocalVariableRow::GetDefaultText)
			.HintText(LOCTEXT("LocalVars.DefaultHint", "Default of the type"))
			.ToolTipText(LOCTEXT("LocalVars.DefaultTip", "Value assigned on every entry into the node, in the text form of pin values (for example 1.5 or X=1,Y=2,Z=3)."))
			.SelectAllTextWhenFocused(true)
			.OnTextCommitted(this, &SCNALocalVariableRow::HandleDefaultCommitted)
			.IsEnabled(this, &SCNALocalVariableRow::IsEditable);
	}

	const FCNALocalVariable* GetVariable() const
	{
		TSharedPtr<SCNALocalVariables> Owner = Panel.Pin();
		return Owner.IsValid() ? Owner->FindVariable(Item->Guid) : nullptr;
	}

	UCustomNodeAsset* GetAsset() const
	{
		TSharedPtr<SCNALocalVariables> Owner = Panel.Pin();
		return Owner.IsValid() ? Owner->GetAsset() : nullptr;
	}

	bool IsEditable() const
	{
		TSharedPtr<SCNALocalVariables> Owner = Panel.Pin();
		return Owner.IsValid() && Owner->IsEditingEnabled() && GetVariable() != nullptr;
	}

	bool IsReadOnly() const { return !IsEditable(); }

	FEdGraphPinType GetType() const
	{
		const FCNALocalVariable* Variable = GetVariable();
		return Variable ? Variable->Type : Item->Type;
	}

	FString GetDefaultString() const
	{
		const FCNALocalVariable* Variable = GetVariable();
		return Variable ? Variable->DefaultValue : FString();
	}

	FText GetDefaultText() const { return FText::FromString(GetDefaultString()); }

	FText GetTooltipText() const
	{
		const FCNALocalVariable* Variable = GetVariable();
		return Variable ? Variable->Tooltip : FText::GetEmpty();
	}

	ECheckBoxState GetBoolState() const
	{
		return GetDefaultString().ToBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	const UClass* GetDefaultClass() const
	{
		const FString Value = GetDefaultString();
		return (Value.IsEmpty() || Value == TEXT("None")) ? nullptr : FSoftClassPath(Value).ResolveClass();
	}

	FText GetEnumText() const
	{
		const UEnum* Enum = Cast<UEnum>(GetType().PinSubCategoryObject.Get());
		if (!Enum || Enum->NumEnums() == 0)
		{
			return FText::GetEmpty();
		}
		// An empty value is the first enumerator, the zero value of the type.
		const int32 Index = GetDefaultString().IsEmpty() ? 0 : Enum->GetIndexByNameString(GetDefaultString());
		return Index != INDEX_NONE ? Enum->GetDisplayNameTextByIndex(Index) : FText::FromString(GetDefaultString());
	}

	TSharedRef<SWidget> MakeEnumMenu()
	{
		FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);
		if (const UEnum* Enum = Cast<UEnum>(GetType().PinSubCategoryObject.Get()))
		{
			// The last entry is the generated _MAX value.
			for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
			{
				if (Enum->HasMetaData(TEXT("Hidden"), Index))
				{
					continue;
				}
				const FString Value = Enum->GetNameStringByIndex(Index);
				MenuBuilder.AddMenuEntry(Enum->GetDisplayNameTextByIndex(Index), FText::FromString(Value), FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SCNALocalVariableRow::ApplyDefault, Value)));
			}
		}
		return MenuBuilder.MakeWidget();
	}

	void ApplyDefault(FString Value)
	{
		FText Error;
		if (!FCNALocalVariableOps::SetDefault(GetAsset(), Item->Guid, Value, &Error))
		{
			CNALocalVariablesLocal::ShowFailure(Error);
		}
	}

	void HandleBoolChanged(ECheckBoxState NewState)
	{
		ApplyDefault(NewState == ECheckBoxState::Checked ? TEXT("true") : TEXT("false"));
	}

	void HandleObjectChanged(const FAssetData& AssetData)
	{
		ApplyDefault(AssetData.IsValid() ? AssetData.ObjectPath.ToString() : FString());
	}

	void HandleClassChanged(const UClass* NewClass)
	{
		ApplyDefault(NewClass ? NewClass->GetPathName() : FString());
	}

	void HandleDefaultCommitted(const FText& NewText, ETextCommit::Type CommitType)
	{
		FText Error;
		const bool bApplied = FCNALocalVariableOps::SetDefault(GetAsset(), Item->Guid, NewText.ToString().TrimStartAndEnd(), &Error);
		if (ValueBox.IsValid())
		{
			ValueBox->SetError(bApplied ? FText::GetEmpty() : Error);
		}
	}

	void HandleTooltipCommitted(const FText& NewText, ETextCommit::Type CommitType)
	{
		FCNALocalVariableOps::SetTooltip(GetAsset(), Item->Guid, NewText);
	}

	bool HandleVerifyName(const FText& NewText, FText& OutError)
	{
		return FCNALocalVariableOps::VerifyName(GetAsset(), Item->Guid, NewText.ToString(), OutError);
	}

	void HandleNameCommitted(const FText& NewText, ETextCommit::Type CommitType)
	{
		if (CommitType == ETextCommit::OnCleared)
		{
			return;
		}
		FText Error;
		if (!FCNALocalVariableOps::Rename(GetAsset(), Item->Guid, FName(*NewText.ToString().TrimStartAndEnd()), Error))
		{
			CNALocalVariablesLocal::ShowFailure(Error);
		}
	}

	void HandleTypeChanged(const FEdGraphPinType& NewType)
	{
		FText Error;
		if (!FCNALocalVariableOps::ChangeType(GetAsset(), Item->Guid, NewType, Error))
		{
			CNALocalVariablesLocal::ShowFailure(Error);
		}
	}

	void HandleRemoveClicked()
	{
		if (TSharedPtr<SCNALocalVariables> Owner = Panel.Pin())
		{
			Owner->HandleRemove(Item);
		}
	}

	FCNALocalVariableItemPtr Item;
	TWeakPtr<SCNALocalVariables> Panel;
	TSharedPtr<SEditableTextBox> ValueBox;
};

// SCNALocalVariables

void SCNALocalVariables::Construct(const FArguments& InArgs, UCustomNodeAsset* InAsset)
{
	using namespace CNALocalVariablesLocal;
	Asset = InAsset;

	const FCNAEditorCommands& Commands = FCNAEditorCommands::Get();
	const FCanExecuteAction CanEdit = FCanExecuteAction::CreateSP(this, &SCNALocalVariables::IsEditingEnabled);
	const FCanExecuteAction CanEditSelection = FCanExecuteAction::CreateSP(this, &SCNALocalVariables::HasSelection);

	CommandList = MakeShareable(new FUICommandList);
	CommandList->MapAction(FGenericCommands::Get().Rename, FExecuteAction::CreateSP(this, &SCNALocalVariables::RenameSelected), CanEditSelection);
	CommandList->MapAction(FGenericCommands::Get().Delete, FExecuteAction::CreateSP(this, &SCNALocalVariables::RemoveSelected), CanEditSelection);
	CommandList->MapAction(Commands.FindItem, FExecuteAction::CreateSP(this, &SCNALocalVariables::FocusSearch));
	CommandList->MapAction(Commands.AddLocalVariable, FExecuteAction::CreateSP(this, &SCNALocalVariables::AddVariable), CanEdit);
	CommandList->MapAction(Commands.MoveItemUp, FExecuteAction::CreateSP(this, &SCNALocalVariables::MoveSelected, -1), CanEditSelection);
	CommandList->MapAction(Commands.MoveItemDown, FExecuteAction::CreateSP(this, &SCNALocalVariables::MoveSelected, 1), CanEditSelection);

	TSharedRef<SHeaderRow> Header = SNew(SHeaderRow)
		+ SHeaderRow::Column(ColumnName)
		.DefaultLabel(LOCTEXT("LocalVars.ColName", "Name"))
		.FillWidth(0.26f)
		+ SHeaderRow::Column(ColumnType)
		.DefaultLabel(LOCTEXT("LocalVars.ColType", "Type"))
		.FillWidth(0.26f)
		+ SHeaderRow::Column(ColumnDefault)
		.DefaultLabel(LOCTEXT("LocalVars.ColDefault", "Default"))
		.DefaultTooltip(LOCTEXT("LocalVars.ColDefaultTip", "Value of the variable on every entry into the node."))
		.FillWidth(0.26f)
		+ SHeaderRow::Column(ColumnTooltip)
		.DefaultLabel(LOCTEXT("LocalVars.ColTooltip", "Tooltip"))
		.FillWidth(0.22f)
		+ SHeaderRow::Column(ColumnUses)
		.DefaultLabel(LOCTEXT("LocalVars.ColUses", "Uses"))
		.FixedWidth(40.0f)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Center)
		+ SHeaderRow::Column(ColumnRemove)
		.DefaultLabel(FText::GetEmpty())
		.FixedWidth(24.0f);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.HintText(LOCTEXT("LocalVars.SearchHint", "Filter variables by name or type (Ctrl+F)"))
				.OnTextChanged(this, &SCNALocalVariables::HandleFilterChanged)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
				.ForegroundColor(FLinearColor::White)
				.ContentPadding(FMargin(6.0f, 1.0f))
				.IsEnabled(this, &SCNALocalVariables::IsEditingEnabled)
				.ToolTipText(Commands.AddLocalVariable->GetDescription())
				.OnClicked_Lambda([this]()
				{
					AddVariable();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LocalVars.Add", "+ Variable"))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 0.0f, 6.0f, 4.0f)
		[
			SNew(STextBlock)
			.Visibility(this, &SCNALocalVariables::GetPureWarningVisibility)
			.AutoWrapText(true)
			.ColorAndOpacity(FLinearColor(1.0f, 0.45f, 0.35f))
			.Text(LOCTEXT("LocalVars.PureWarning", "The node is pure: local variables are not allowed and compilation fails (CNA018). Clear Pure or remove the variables."))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(ListView, SListView<FCNALocalVariableItemPtr>)
			.ListItemsSource(&Items)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SCNALocalVariables::GenerateRow)
			.OnContextMenuOpening(this, &SCNALocalVariables::MakeContextMenu)
			.HeaderRow(Header)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 4.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(this, &SCNALocalVariables::GetSummaryText)
		]
	];

	if (UCustomNodeAsset* CurrentAsset = Asset.Get())
	{
		// Adding or removing Get Local and Set Local nodes changes the Uses column.
		BlueprintChangedHandle = CurrentAsset->OnChanged().AddSP(this, &SCNALocalVariables::HandleBlueprintChanged);
	}
	AssetChangedHandle = FCNAEditorHooks::OnAssetChanged.AddSP(this, &SCNALocalVariables::HandleAssetChanged);
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
	Refresh();
}

SCNALocalVariables::~SCNALocalVariables()
{
	FCNAEditorHooks::OnAssetChanged.Remove(AssetChangedHandle);
	if (UCustomNodeAsset* CurrentAsset = Asset.Get())
	{
		CurrentAsset->OnChanged().Remove(BlueprintChangedHandle);
	}
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

const FCNALocalVariable* SCNALocalVariables::FindVariable(const FGuid& Guid) const
{
	const int32 Index = FCNALocalVariableOps::FindIndex(Asset.Get(), Guid);
	return Index != INDEX_NONE ? &Asset->LocalVariables[Index] : nullptr;
}

TSharedRef<ITableRow> SCNALocalVariables::GenerateRow(FCNALocalVariableItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SCNALocalVariableRow, OwnerTable)
		.Item(Item)
		.Panel(SharedThis(this));
}

TSharedPtr<SWidget> SCNALocalVariables::MakeContextMenu()
{
	const FCNAEditorCommands& Commands = FCNAEditorCommands::Get();
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/ true, CommandList);
	MenuBuilder.BeginSection("CNALocalVariable", LOCTEXT("LocalVars.MenuVariable", "Local Variable"));
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
		MenuBuilder.AddMenuEntry(Commands.MoveItemUp);
		MenuBuilder.AddMenuEntry(Commands.MoveItemDown);
		MenuBuilder.AddMenuEntry(Commands.AddLocalVariable);
	}
	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

FReply SCNALocalVariables::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList.IsValid() && CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SCNALocalVariables::FocusSearch()
{
	if (SearchBox.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(SearchBox, EFocusCause::SetDirectly);
	}
}

void SCNALocalVariables::HandleFilterChanged(const FText& NewText)
{
	FilterText = NewText.ToString().TrimStartAndEnd();
	RequestRefresh(true);
}

void SCNALocalVariables::HandleAssetChanged(UCustomNodeAsset* ChangedAsset, FName PropertyName)
{
	if (ChangedAsset && ChangedAsset == Asset.Get())
	{
		RequestRefresh(false);
	}
}

void SCNALocalVariables::HandleBlueprintChanged(UBlueprint* Blueprint)
{
	RequestRefresh(false);
}

void SCNALocalVariables::RequestRefresh(bool bForce)
{
	bForceRefresh |= bForce;
	if (!bRefreshPending)
	{
		bRefreshPending = true;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SCNALocalVariables::HandleRefreshTimer));
	}
}

EActiveTimerReturnType SCNALocalVariables::HandleRefreshTimer(double InCurrentTime, float InDeltaTime)
{
	bRefreshPending = false;
	// Rows are only rebuilt when they would change: a rebuild drops the keyboard focus of an open text box.
	if (bForceRefresh || BuildStateKey() != LastStateKey)
	{
		Refresh();
	}
	bForceRefresh = false;
	return EActiveTimerReturnType::Stop;
}

FString SCNALocalVariables::BuildStateKey() const
{
	FString Key = FilterText;
	if (const UCustomNodeAsset* CurrentAsset = Asset.Get())
	{
		Key += CurrentAsset->bPure ? TEXT("|P") : TEXT("|-");
		for (const FCNALocalVariable& Variable : CurrentAsset->LocalVariables)
		{
			// Defaults and tooltips are edited live by their widgets and are not part of the key.
			Key += FString::Printf(TEXT("|%s:%s:%s:%d"), *Variable.Guid.ToString(), *Variable.Name.ToString(),
				*FCNAPinUtils::GetTypeSignatureString(Variable.Type), FCNALocalVariableOps::CountUses(CurrentAsset, Variable.Guid));
		}
	}
	return Key;
}

void SCNALocalVariables::Refresh()
{
	const FCNALocalVariableItemPtr Selected = GetSelectedItem();
	const FGuid SelectedGuid = Selected.IsValid() ? Selected->Guid : FGuid();

	Items.Reset();
	if (const UCustomNodeAsset* CurrentAsset = Asset.Get())
	{
		for (const FCNALocalVariable& Variable : CurrentAsset->LocalVariables)
		{
			const FString TypeText = FCNAPinUtils::GetTypeText(Variable.Type).ToString();
			if (!FilterText.IsEmpty() && !Variable.Name.ToString().Contains(FilterText) && !TypeText.Contains(FilterText))
			{
				continue;
			}
			FCNALocalVariableItemPtr Item = MakeShareable(new FCNALocalVariableItem);
			Item->Guid = Variable.Guid;
			Item->Name = Variable.Name;
			Item->Type = Variable.Type;
			Item->Uses = FCNALocalVariableOps::CountUses(CurrentAsset, Variable.Guid);
			Items.Add(Item);
		}
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
		for (const FCNALocalVariableItemPtr& Item : Items)
		{
			if (SelectedGuid.IsValid() && Item->Guid == SelectedGuid)
			{
				ListView->SetSelection(Item, ESelectInfo::Direct);
			}
		}
	}
	LastStateKey = BuildStateKey();
}

FCNALocalVariableItemPtr SCNALocalVariables::GetSelectedItem() const
{
	if (ListView.IsValid())
	{
		TArray<FCNALocalVariableItemPtr> Selection = ListView->GetSelectedItems();
		if (Selection.Num() > 0)
		{
			return Selection[0];
		}
	}
	return FCNALocalVariableItemPtr();
}

bool SCNALocalVariables::HasSelection() const
{
	return IsEditingEnabled() && GetSelectedItem().IsValid();
}

void SCNALocalVariables::AddVariable()
{
	FEdGraphPinType Type;
	Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	const FGuid NewGuid = FCNALocalVariableOps::Add(Asset.Get(), NAME_None, Type);
	if (!NewGuid.IsValid())
	{
		return;
	}
	// The filter would hide the new row.
	if (!FilterText.IsEmpty() && SearchBox.IsValid())
	{
		SearchBox->SetText(FText::GetEmpty());
		FilterText.Reset();
	}
	Refresh();

	// Select the new variable and start renaming it once its row exists (rows are generated on the next frame).
	for (const FCNALocalVariableItemPtr& Item : Items)
	{
		if (Item->Guid == NewGuid && ListView.IsValid())
		{
			ListView->SetSelection(Item, ESelectInfo::Direct);
			ListView->RequestScrollIntoView(Item);
			const TWeakPtr<FCNALocalVariableItem> WeakItem = Item;
			RegisterActiveTimer(0.05f, FWidgetActiveTimerDelegate::CreateLambda([WeakItem](double, float)
			{
				if (TSharedPtr<FCNALocalVariableItem> Pinned = WeakItem.Pin())
				{
					Pinned->OnRenameRequested.ExecuteIfBound();
				}
				return EActiveTimerReturnType::Stop;
			}));
			break;
		}
	}
}

void SCNALocalVariables::RenameSelected()
{
	if (FCNALocalVariableItemPtr Item = GetSelectedItem())
	{
		Item->OnRenameRequested.ExecuteIfBound();
	}
}

void SCNALocalVariables::RemoveSelected()
{
	HandleRemove(GetSelectedItem());
}

void SCNALocalVariables::HandleRemove(FCNALocalVariableItemPtr Item)
{
	UCustomNodeAsset* CurrentAsset = Asset.Get();
	if (!Item.IsValid() || !CurrentAsset)
	{
		return;
	}
	const int32 Uses = FCNALocalVariableOps::CountUses(CurrentAsset, Item->Guid);
	if (Uses > 0)
	{
		const FText Question = FText::Format(LOCTEXT("LocalVars.RemoveUsed",
			"Local variable {0} is used by {1} Get Local or Set Local node(s). Remove the variable together with these nodes?"),
			FText::FromName(Item->Name), FText::AsNumber(Uses));
		if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
		{
			return;
		}
	}
	FCNALocalVariableOps::Remove(CurrentAsset, Item->Guid, true);
	RequestRefresh(true);
}

void SCNALocalVariables::MoveSelected(int32 Delta)
{
	const FCNALocalVariableItemPtr Item = GetSelectedItem();
	const int32 Index = Item.IsValid() ? FCNALocalVariableOps::FindIndex(Asset.Get(), Item->Guid) : INDEX_NONE;
	if (Index != INDEX_NONE && FCNALocalVariableOps::Move(Asset.Get(), Item->Guid, Index + Delta))
	{
		Refresh();
	}
}

FText SCNALocalVariables::GetSummaryText() const
{
	const UCustomNodeAsset* CurrentAsset = Asset.Get();
	const int32 Count = CurrentAsset ? CurrentAsset->LocalVariables.Num() : 0;
	return FText::Format(LOCTEXT("LocalVars.Summary",
		"{0} variable(s). Each instance of the node has its own copies, reset to the defaults on every entry. F2 renames, Delete removes, Alt+Up and Alt+Down reorder."),
		FText::AsNumber(Count));
}

EVisibility SCNALocalVariables::GetPureWarningVisibility() const
{
	const UCustomNodeAsset* CurrentAsset = Asset.Get();
	return (CurrentAsset && CurrentAsset->bPure && CurrentAsset->LocalVariables.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
