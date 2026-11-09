// Copyright (c) NodeFactory. Pin Manager of the asset window (spec 7.1, 4.1, 4.2).

#include "SCNAPinManager.h"
#include "CustomNodeAsset.h"
#include "CNAEditorCommands.h"
#include "CNAGraphUtils.h"
#include "CNAPinUtils.h"
#include "CNAValidator.h"
#include "CNAMessages.h"

#include "K2Node_Tunnel.h"
#include "K2Node_EditablePinBase.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "NodeFactory.h"
#include "SGraphPin.h"
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
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Input/DragAndDrop.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNAPinManagerLocal
{
	const FName ColumnName(TEXT("Name"));
	const FName ColumnType(TEXT("Type"));
	const FName ColumnDefault(TEXT("Default"));
	const FName ColumnAdvanced(TEXT("Advanced"));
	const FName ColumnHidden(TEXT("Hidden"));
	const FName ColumnRequired(TEXT("Required"));
	const FName ColumnStatus(TEXT("Status"));
	const FName ColumnRemove(TEXT("Remove"));

	/** Characters that break T3D, JSON paths or the names of dynamic pins. */
	const TCHAR* InvalidPinNameCharacters = TEXT("\"',.:|/\\\n\r\t");

	/** Direction of the tunnel pin that carries a signature pin of the given side. */
	EEdGraphPinDirection GetTunnelPinDirection(EEdGraphPinDirection Side)
	{
		return Side == EGPD_Input ? EGPD_Output : EGPD_Input;
	}

	void ShowNotification(const FText& Text, bool bSuccess)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		FNotificationInfo Info(Text);
		Info.ExpireDuration = bSuccess ? 2.5f : 5.0f;
		Info.bUseLargeFont = false;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	FString JoinTexts(const TArray<FText>& Texts)
	{
		FString Joined;
		for (const FText& Text : Texts)
		{
			if (!Joined.IsEmpty())
			{
				Joined += TEXT("\n");
			}
			Joined += Text.ToString();
		}
		return Joined;
	}
}

// FCNAPinManagerOps

UK2Node_Tunnel* FCNAPinManagerOps::GetTunnel(const UCustomNodeAsset* Asset, EEdGraphPinDirection Side)
{
	const UEdGraph* Execute = Asset ? Asset->FindExecuteGraph() : nullptr;
	return Side == EGPD_Input ? FCNAGraphUtils::FindEntryTunnel(Execute) : FCNAGraphUtils::FindExitTunnel(Execute);
}

TSharedPtr<FUserPinInfo> FCNAPinManagerOps::FindUserPin(const UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName)
{
	if (UK2Node_Tunnel* Tunnel = GetTunnel(Asset, Side))
	{
		for (const TSharedPtr<FUserPinInfo>& Info : Tunnel->UserDefinedPins)
		{
			if (Info.IsValid() && Info->PinName == PinName)
			{
				return Info;
			}
		}
	}
	return TSharedPtr<FUserPinInfo>();
}

UEdGraphPin* FCNAPinManagerOps::FindTunnelPin(const UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName)
{
	UK2Node_Tunnel* Tunnel = GetTunnel(Asset, Side);
	return Tunnel ? Tunnel->FindPin(PinName, CNAPinManagerLocal::GetTunnelPinDirection(Side)) : nullptr;
}

bool FCNAPinManagerOps::IsPinNameUsed(const UCustomNodeAsset* Asset, FName PinName)
{
	for (const EEdGraphPinDirection Side : { EGPD_Input, EGPD_Output })
	{
		const UK2Node_Tunnel* Tunnel = GetTunnel(Asset, Side);
		if (!Tunnel)
		{
			continue;
		}
		for (const TSharedPtr<FUserPinInfo>& Info : Tunnel->UserDefinedPins)
		{
			if (Info.IsValid() && Info->PinName == PinName)
			{
				return true;
			}
		}
		for (const UEdGraphPin* Pin : Tunnel->Pins)
		{
			if (Pin && Pin->PinName == PinName)
			{
				return true;
			}
		}
	}
	return false;
}

FName FCNAPinManagerOps::MakeUniquePinName(const UCustomNodeAsset* Asset, FName BaseName)
{
	if (BaseName.IsNone())
	{
		BaseName = FName(TEXT("NewParam"));
	}
	if (!IsPinNameUsed(Asset, BaseName))
	{
		return BaseName;
	}

	const FString Base = BaseName.ToString();
	for (int32 Suffix = 1; Suffix < 10000; ++Suffix)
	{
		const FName Candidate(*FString::Printf(TEXT("%s%d"), *Base, Suffix));
		if (!IsPinNameUsed(Asset, Candidate))
		{
			return Candidate;
		}
	}
	return FName(*FString::Printf(TEXT("%s_%s"), *Base, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
}

bool FCNAPinManagerOps::VerifyPinName(const UCustomNodeAsset* Asset, FName OldName, const FString& NewName, FText& OutError)
{
	const FString Trimmed = NewName.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		OutError = LOCTEXT("PinManager.NameEmpty", "The name cannot be empty.");
		return false;
	}
	if (Trimmed.Len() > 100)
	{
		OutError = LOCTEXT("PinManager.NameTooLong", "The name is longer than 100 characters.");
		return false;
	}
	for (const TCHAR Character : Trimmed)
	{
		if (FCString::Strchr(CNAPinManagerLocal::InvalidPinNameCharacters, Character) != nullptr)
		{
			OutError = FText::Format(LOCTEXT("PinManager.NameInvalidChar", "The character '{0}' is not allowed in a pin name."),
				FText::FromString(FString::Chr(Character)));
			return false;
		}
	}
	if (Trimmed.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		OutError = LOCTEXT("PinManager.NameNone", "None is not a valid pin name.");
		return false;
	}

	const FName Candidate(*Trimmed);
	// FName comparison ignores case: a change of the case only is not a rename.
	if (!OldName.IsNone() && Candidate == OldName)
	{
		return true;
	}
	if (IsPinNameUsed(Asset, Candidate))
	{
		OutError = FText::Format(LOCTEXT("PinManager.NameUsed", "A pin named {0} already exists. Names are unique across inputs and outputs."),
			FText::FromString(Trimmed));
		return false;
	}
	return true;
}

FName FCNAPinManagerOps::AddPin(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, const FEdGraphPinType& Type, FName BaseName, FText* OutError)
{
	UK2Node_Tunnel* Tunnel = GetTunnel(Asset, Side);
	if (!Tunnel)
	{
		if (OutError)
		{
			*OutError = LOCTEXT("PinManager.NoTunnel", "The Execution Graph has no Tunnel Input or Tunnel Output (CNA005).");
		}
		return NAME_None;
	}

	const EEdGraphPinDirection Desired = CNAPinManagerLocal::GetTunnelPinDirection(Side);
	FText Error;
	// V-check: UK2Node_EditablePinBase::CanCreateUserDefinedPin is public in 4.20.3.
	if (!Tunnel->CanCreateUserDefinedPin(Type, Desired, Error))
	{
		if (OutError)
		{
			*OutError = Error;
		}
		return NAME_None;
	}

	const FName NewName = MakeUniquePinName(Asset, BaseName.IsNone() ? FName(Side == EGPD_Input ? TEXT("NewInput") : TEXT("NewOutput")) : BaseName);

	const FScopedTransaction Transaction(LOCTEXT("PinManager.AddTransaction", "Add Pin"));
	Asset->Modify();
	Tunnel->Modify();
	// The name is already unique; the default argument bUseUniqueName (where present) keeps it.
	Tunnel->CreateUserDefinedPin(NewName, Type, Desired);
	FinishStructuralChange(Asset, Tunnel);
	return NewName;
}

bool FCNAPinManagerOps::RenamePin(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName OldName, FName NewName, FText& OutError)
{
	UK2Node_Tunnel* Tunnel = GetTunnel(Asset, Side);
	if (!Tunnel || OldName.IsNone() || NewName.IsNone())
	{
		return false;
	}
	if (OldName == NewName)
	{
		return true;
	}
	if (!VerifyPinName(Asset, OldName, NewName.ToString(), OutError))
	{
		return false;
	}

	TSharedPtr<FUserPinInfo> PinInfo = FindUserPin(Asset, Side, OldName);
	if (!PinInfo.IsValid()) { OutError = LOCTEXT("PinManager.RenameMissing", "Pin description not found."); return false; }
	FScopedTransaction Transaction(LOCTEXT("PinManager.RenameTransaction", "Rename Pin"));
	Asset->Modify();
	Tunnel->Modify();
	if (Tunnel->RenameUserDefinedPin(OldName, NewName) != ERenamePinResult::ERenamePinResult_Success)
	{
		Transaction.Cancel();
		OutError = FText::Format(LOCTEXT("PinManager.RenameFailed", "Pin {0} could not be renamed."), FText::FromName(OldName));
		return false;
	}

	// RenameUserDefinedPin changes the graph pin, but not UserDefinedPins on this engine.
	PinInfo->PinName = NewName;
	// Instances keep their wires through the redirect (V11); overrides and references follow the new name.
	AddRedirect(*Asset, OldName, NewName);
	RenameReferences(*Asset, OldName, NewName);
	FinishStructuralChange(Asset, Tunnel);
	Asset->NotifyAssetChanged(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinRedirects));
	return true;
}

bool FCNAPinManagerOps::ChangePinType(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName, const FEdGraphPinType& NewType, FText& OutError)
{
	UK2Node_Tunnel* Tunnel = GetTunnel(Asset, Side);
	TSharedPtr<FUserPinInfo> Info = FindUserPin(Asset, Side, PinName);
	if (!Tunnel || !Info.IsValid())
	{
		return false;
	}
	if (FCNAPinUtils::AreTypesIdentical(Info->PinType, NewType))
	{
		return true;
	}
	if (!Tunnel->CanCreateUserDefinedPin(NewType, Info->DesiredPinDirection, OutError))
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("PinManager.TypeTransaction", "Change Pin Type"));
	Asset->Modify();
	Tunnel->Modify();
	Info->PinType = NewType;
	Info->PinDefaultValue.Reset();
	// A value of the old type must not move to the new pin: ReconstructNode moves values that differ from the
	// autogenerated default, so the old pin is reset to it first.
	if (UEdGraphPin* Pin = FindTunnelPin(Asset, Side, PinName))
	{
		Pin->DefaultValue = Pin->AutogeneratedDefaultValue;
		Pin->DefaultObject = nullptr;
		Pin->DefaultTextValue = FText::GetEmpty();
	}
	FinishStructuralChange(Asset, Tunnel);
	return true;
}
bool FCNAPinManagerOps::RemovePin(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName)
{
	UK2Node_Tunnel* Tunnel = GetTunnel(Asset, Side);
	if (!Tunnel || !FindUserPin(Asset, Side, PinName).IsValid())
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("PinManager.RemoveTransaction", "Remove Pin"));
	Asset->Modify();
	Tunnel->Modify();
	// RemoveUserDefinedPinByName does not break the wires on the other side in every engine version.
	if (UEdGraphPin* Pin = FindTunnelPin(Asset, Side, PinName))
	{
		FCNAPinUtils::BreakAllLinksRecursive(Pin);
	}
	Tunnel->RemoveUserDefinedPinByName(PinName);
	// Settings of the pin go with it; pass-through references to it are cleared (CNA015 otherwise).
	Asset->PinOverrides.RemoveAll([PinName](const FCNAPinOverride& Override) { return Override.PinName == PinName; });
	for (FCNAPinOverride& Override : Asset->PinOverrides)
	{
		if (Override.PassThroughOf == PinName)
		{
			Override.PassThroughOf = NAME_None;
		}
	}
	Asset->BodyWidgets.RemoveAll([PinName](const FCNABodyWidget& Widget) { return Widget.PinName == PinName; });
	FinishStructuralChange(Asset, Tunnel);
	Asset->NotifyAssetChanged(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinOverrides));
	return true;
}
bool FCNAPinManagerOps::MovePin(UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName, int32 NewIndex)
{
	UK2Node_Tunnel* Tunnel = GetTunnel(Asset, Side);
	if (!Tunnel)
	{
		return false;
	}
	const int32 OldIndex = Tunnel->UserDefinedPins.IndexOfByPredicate([PinName](const TSharedPtr<FUserPinInfo>& Info)
	{
		return Info.IsValid() && Info->PinName == PinName;
	});
	if (OldIndex == INDEX_NONE)
	{
		return false;
	}
	NewIndex = FMath::Clamp(NewIndex, 0, Tunnel->UserDefinedPins.Num() - 1);
	if (NewIndex == OldIndex)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("PinManager.MoveTransaction", "Move Pin"));
	Asset->Modify();
	Tunnel->Modify();
	TSharedPtr<FUserPinInfo> Moved = Tunnel->UserDefinedPins[OldIndex];
	Tunnel->UserDefinedPins.RemoveAt(OldIndex);
	Tunnel->UserDefinedPins.Insert(Moved, NewIndex);
	// ReconstructNode creates the pins in the order of UserDefinedPins; wires are kept by name.
	FinishStructuralChange(Asset, Tunnel);
	return true;
}
bool FCNAPinManagerOps::SetPinDefault(UCustomNodeAsset* Asset, FName PinName, const FString& NewValue)
{
	UK2Node_Tunnel* Entry = GetTunnel(Asset, EGPD_Input);
	TSharedPtr<FUserPinInfo> Info = FindUserPin(Asset, EGPD_Input, PinName);
	UEdGraphPin* Pin = FindTunnelPin(Asset, EGPD_Input, PinName);
	if (!Entry || !Info.IsValid() || !Pin)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("PinManager.DefaultTransaction", "Change Pin Default Value"));
	Asset->Modify();
	Entry->Modify();
	// The schema checks the value; an invalid value leaves the pin unchanged.
	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text) GetDefault<UEdGraphSchema_K2>()->TrySetDefaultText(*Pin, FText::FromString(NewValue));
	else GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(*Pin, NewValue);
	Info->PinDefaultValue = Pin->GetDefaultAsString();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Asset);
	return Pin->GetDefaultAsString() == NewValue;
}
bool FCNAPinManagerOps::SetPinFlag(UCustomNodeAsset* Asset, FName PinName, ECNAPinFlag Flag, bool bValue)
{
	if (!Asset || PinName.IsNone() || GetPinFlag(Asset, PinName, Flag) == bValue)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("PinManager.FlagTransaction", "Change Pin Visibility"));
	Asset->Modify();
	FCNAPinOverride& Override = Asset->FindOrAddPinOverride(PinName);
	switch (Flag)
	{
	case ECNAPinFlag::Advanced:
		Override.bAdvanced = bValue;
		break;
	case ECNAPinFlag::Hidden:
		Override.bHiddenByDefault = bValue;
		break;
	case ECNAPinFlag::Required:
		Override.bRequired = bValue;
		break;
	}
	// An override without any setting is noise in Details and JSON.
	if (Override.IsEmpty())
	{
		Asset->PinOverrides.RemoveAll([PinName](const FCNAPinOverride& Item) { return Item.PinName == PinName; });
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(Asset);
	Asset->NotifyAssetChanged(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinOverrides));
	return true;
}
bool FCNAPinManagerOps::GetPinFlag(const UCustomNodeAsset* Asset, FName PinName, ECNAPinFlag Flag)
{
	const FCNAPinOverride* Override = Asset ? Asset->FindPinOverride(PinName) : nullptr;
	if (!Override)
	{
		return false;
	}
	switch (Flag)
	{
	case ECNAPinFlag::Advanced:
		return Override->bAdvanced;
	case ECNAPinFlag::Hidden:
		return Override->bHiddenByDefault;
	case ECNAPinFlag::Required:
		return Override->bRequired;
	}
	return false;
}
bool FCNAPinManagerOps::SyncTunnelDefaults(UCustomNodeAsset* Asset)
{
	UK2Node_Tunnel* Entry = GetTunnel(Asset, EGPD_Input);
	if (!Entry || GIsTransacting)
	{
		return false;
	}
	bool bChanged = false;
	for (const TSharedPtr<FUserPinInfo>& Info : Entry->UserDefinedPins)
	{
		if (!Info.IsValid())
		{
			continue;
		}
		const UEdGraphPin* Pin = Entry->FindPin(Info->PinName, EGPD_Output);
		// Text values live in DefaultTextValue of the pin and are read from there (CNAGraphUtils).
		if (!Pin || FCNAPinUtils::IsExecType(Pin->PinType) || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
		{
			continue;
		}
		const FString Current = Pin->GetDefaultAsString();
		if (Current != Info->PinDefaultValue)
		{
			if (!bChanged)
			{
				Entry->Modify();
				bChanged = true;
			}
			Info->PinDefaultValue = Current;
		}
	}
	return bChanged;
}
bool FCNAPinManagerOps::GetPinProblem(const UCustomNodeAsset* Asset, EEdGraphPinDirection Side, FName PinName, FText& OutProblem)
{
	TArray<FText> Problems;
	const TSharedPtr<FUserPinInfo> Info = FindUserPin(Asset, Side, PinName);
	if (Info.IsValid() && FCNAPinUtils::IsTypeObjectMissing(Info->PinType))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Type"), FCNAPinUtils::GetTypeText(Info->PinType));
		Args.Add(TEXT("Pin"), FText::FromName(PinName));
		Problems.Add(FCNAMessages::Format(ECNAMessage::CNA010, Args));
	}
	FText OverrideProblem;
	if (FCNAValidator::GetPinOverrideProblem(Asset, PinName, OverrideProblem) && !OverrideProblem.IsEmpty())
	{
		Problems.Add(OverrideProblem);
	}
	if (Problems.Num() == 0)
	{
		return false;
	}
	OutProblem = FText::FromString(CNAPinManagerLocal::JoinTexts(Problems));
	return true;
}
void FCNAPinManagerOps::AddRedirect(UCustomNodeAsset& Asset, FName OldName, FName NewName)
{
	// Chains A -> Old become A -> New, so instances older than the last rename are found as well.
	for (FCNAPinRedirect& Redirect : Asset.PinRedirects)
	{
		if (Redirect.NewName == OldName)
		{
			Redirect.NewName = NewName;
		}
	}
	// The new name exists again (renamed back, for example): a redirect away from it would steal its wires.
	Asset.PinRedirects.RemoveAll([NewName](const FCNAPinRedirect& Redirect)
	{
		return Redirect.OldName == NewName || Redirect.OldName == Redirect.NewName;
	});
	if (FCNAPinRedirect* Existing = Asset.PinRedirects.FindByPredicate([OldName](const FCNAPinRedirect& Redirect) { return Redirect.OldName == OldName; }))
	{
		Existing->NewName = NewName;
	}
	else
	{
		Asset.PinRedirects.Add(FCNAPinRedirect(OldName, NewName));
	}
}
void FCNAPinManagerOps::RenameReferences(UCustomNodeAsset& Asset, FName OldName, FName NewName)
{
	for (FCNAPinOverride& Override : Asset.PinOverrides)
	{
		if (Override.PinName == OldName)
		{
			Override.PinName = NewName;
		}
		if (Override.PassThroughOf == OldName)
		{
			Override.PassThroughOf = NewName;
		}
	}
	for (FCNABodyWidget& Widget : Asset.BodyWidgets)
	{
		if (Widget.PinName == OldName)
		{
			Widget.PinName = NewName;
		}
	}
}
void FCNAPinManagerOps::FinishStructuralChange(UCustomNodeAsset* Asset, UK2Node_Tunnel* Tunnel)
{
	if (Tunnel)
	{
		Tunnel->ReconstructNode();
		if (UEdGraph* Graph = Tunnel->GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}
	// Recompiles the skeleton of the asset and notifies the panels (PostEditChangeProperty -> NotifyAssetChanged).
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Asset);
}
// Drag and drop of rows
/** A row of the Pin Manager dragged to another position of its list. */
class FCNAPinDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FCNAPinDragDropOp, FDragDropOperation)
	FName PinName;
	EEdGraphPinDirection Side = EGPD_Input;
	static TSharedRef<FCNAPinDragDropOp> New(const FCNAPinManagerItem& Item)
	{
		TSharedRef<FCNAPinDragDropOp> Operation = MakeShareable(new FCNAPinDragDropOp);
		Operation->PinName = Item.PinName;
		Operation->Side = Item.Side;
		Operation->Construct();
		return Operation;
	}
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("Graph.ConnectorFeedback.Border"))
			.Padding(FMargin(6.0f, 3.0f))
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("PinManager.DragLabel", "Move {0}"), FText::FromName(PinName)))
			];
	}
};
// Row
/** One pin: name, type, default value, flags, status and remove button. */
class SCNAPinRow : public SMultiColumnTableRow<FCNAPinManagerItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SCNAPinRow) {}
		SLATE_ARGUMENT(FCNAPinManagerItemPtr, Item)
		SLATE_ARGUMENT(TSharedPtr<SCNAPinManager>, Manager)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Item = InArgs._Item;
		Manager = InArgs._Manager;
		if (TSharedPtr<SCNAPinManager> Owner = Manager.Pin())
		{
			bHasProblem = FCNAPinManagerOps::GetPinProblem(Owner->GetAsset(), Item->Side, Item->PinName, Problem);
		}
		// V-check: OnDragDetected, OnCanAcceptDrop and OnAcceptDrop of STableRow in 4.20.3.
		FSuperRowType::Construct(FTableRowArgs()
			.Padding(FMargin(0.0f, 1.0f))
			.OnDragDetected(this, &SCNAPinRow::HandleDragDetected)
			.OnCanAcceptDrop(this, &SCNAPinRow::HandleCanAcceptDrop)
			.OnAcceptDrop(this, &SCNAPinRow::HandleAcceptDrop),
			OwnerTable);
	}
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnId) override
	{
		using namespace CNAPinManagerLocal;
		if (!Item.IsValid() || !Manager.IsValid())
		{
			return SNullWidget::NullWidget;
		}
		if (ColumnId == ColumnName)
		{
			return MakeNameWidget();
		}
		if (ColumnId == ColumnType)
		{
			return MakeTypeWidget();
		}
		if (ColumnId == ColumnDefault)
		{
			return MakeDefaultWidget();
		}
		if (ColumnId == ColumnAdvanced)
		{
			return MakeFlagWidget(ECNAPinFlag::Advanced, LOCTEXT("PinManager.AdvancedTip", "Advanced: the pin is behind the arrow at the bottom of the node."));
		}
		if (ColumnId == ColumnHidden)
		{
			return MakeFlagWidget(ECNAPinFlag::Hidden, LOCTEXT("PinManager.HiddenTip", "Hidden: the pin is created hidden; Behavior can show it."));
		}
		if (ColumnId == ColumnRequired)
		{
			return MakeFlagWidget(ECNAPinFlag::Required, LOCTEXT("PinManager.RequiredTip", "Required: compilation fails when the pin has neither a wire nor a value (CNA008)."));
		}
		if (ColumnId == ColumnStatus)
		{
			return SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Icons.Warning"))
					.ToolTipText(Problem)
					.Visibility(bHasProblem ? EVisibility::Visible : EVisibility::Hidden)
				];
		}
		if (ColumnId == ColumnRemove)
		{
			return SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					PropertyCustomizationHelpers::MakeDeleteButton(
						FSimpleDelegate::CreateSP(this, &SCNAPinRow::HandleRemoveClicked),
						LOCTEXT("PinManager.RemoveTip", "Remove the pin (Delete)."),
						TAttribute<bool>(this, &SCNAPinRow::IsEditable))
				];
		}
		return SNullWidget::NullWidget;
	}

private:
	TSharedRef<SWidget> MakeNameWidget()
	{
		TSharedPtr<SInlineEditableTextBlock> NameText;
		TSharedRef<SWidget> Widget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 3.0f, 0.0f)
			[
				SNew(SImage)
				.Image(this, &SCNAPinRow::GetPinIcon)
				.ColorAndOpacity(this, &SCNAPinRow::GetPinColor)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			[
				SAssignNew(NameText, SInlineEditableTextBlock)
				.Text(FText::FromName(Item->PinName))
				.ToolTipText(LOCTEXT("PinManager.NameTip", "Name of the pin. F2 or double click renames it; the old name is kept in Pin Redirects so wires survive."))
				.IsSelected(this, &SCNAPinRow::IsSelectedExclusively)
				.IsReadOnly(this, &SCNAPinRow::IsReadOnly)
				.OnVerifyTextChanged(this, &SCNAPinRow::HandleVerifyName)
				.OnTextCommitted(this, &SCNAPinRow::HandleNameCommitted)
			];

		Item->OnRenameRequested.BindSP(NameText.Get(), &SInlineEditableTextBlock::EnterEditingMode);
		return Widget;
	}

	TSharedRef<SWidget> MakeTypeWidget()
	{
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		// Exec and Wildcard are valid macro pins (spec 4.3, 4.5); containers come from the selector (V-check: arguments in 4.20.3).
		return SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.0f, 0.0f))
			[
				SNew(SPinTypeSelector, FGetPinTypeTree::CreateUObject(Schema, &UEdGraphSchema_K2::GetVariableTypeTree))
				.TargetPinType(this, &SCNAPinRow::GetPinType)
				.OnPinTypeChanged(this, &SCNAPinRow::HandleTypeChanged)
				.Schema(Schema)
				.TypeTreeFilter(ETypeTreeFilter::AllowExec | ETypeTreeFilter::AllowWildcard)
				.bAllowArrays(true)
				.Font(FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.IsEnabled(this, &SCNAPinRow::IsEditable)
			];
	}

	TSharedRef<SWidget> MakeDefaultWidget()
	{
		TSharedPtr<SCNAPinManager> Owner = Manager.Pin();
		UEdGraphPin* Pin = Owner.IsValid() ? FCNAPinManagerOps::FindTunnelPin(Owner->GetAsset(), Item->Side, Item->PinName) : nullptr;
		if (!Pin || Item->Side != EGPD_Input || Pin->PinType.IsContainer()) return SNew(STextBlock).Text(FText::FromString(TEXT("-")));
		const FName Category = Pin->PinType.PinCategory;
		TWeakPtr<SCNAPinManager> WeakOwner = Manager;
		const FName Name = Item->PinName;
		if (Category == UEdGraphSchema_K2::PC_Boolean)
		{
			return SNew(SCheckBox).IsEnabled(this, &SCNAPinRow::IsEditable)
				.IsChecked_Lambda([WeakOwner, Name]() { TSharedPtr<SCNAPinManager> O = WeakOwner.Pin(); UEdGraphPin* P = O.IsValid() ? FCNAPinManagerOps::FindTunnelPin(O->GetAsset(), EGPD_Input, Name) : nullptr; return P && P->GetDefaultAsString().ToBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([WeakOwner, Name](ECheckBoxState State) { if (TSharedPtr<SCNAPinManager> O = WeakOwner.Pin()) FCNAPinManagerOps::SetPinDefault(O->GetAsset(), Name, State == ECheckBoxState::Checked ? TEXT("true") : TEXT("false")); });
		}
		if (Category == UEdGraphSchema_K2::PC_String || Category == UEdGraphSchema_K2::PC_Name || Category == UEdGraphSchema_K2::PC_Text || Category == UEdGraphSchema_K2::PC_Int || Category == UEdGraphSchema_K2::PC_Float)
		{
			return SNew(SEditableTextBox).IsEnabled(this, &SCNAPinRow::IsEditable)
				.Text_Lambda([WeakOwner, Name]() { TSharedPtr<SCNAPinManager> O = WeakOwner.Pin(); UEdGraphPin* P = O.IsValid() ? FCNAPinManagerOps::FindTunnelPin(O->GetAsset(), EGPD_Input, Name) : nullptr; return P ? (P->PinType.PinCategory == UEdGraphSchema_K2::PC_Text ? P->DefaultTextValue : FText::FromString(P->GetDefaultAsString())) : FText::GetEmpty(); })
				.OnTextCommitted_Lambda([WeakOwner, Name](const FText& Text, ETextCommit::Type Commit) { if (Commit != ETextCommit::OnCleared) if (TSharedPtr<SCNAPinManager> O = WeakOwner.Pin()) FCNAPinManagerOps::SetPinDefault(O->GetAsset(), Name, Text.ToString()); });
		}
		return SNew(STextBlock).Text(FText::FromString(TEXT("-")));
	}
	TSharedRef<SWidget> MakeFlagWidget(ECNAPinFlag Flag, const FText& ToolTip)
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.ToolTipText(ToolTip)
				.IsEnabled(this, &SCNAPinRow::IsEditable)
				.IsChecked(this, &SCNAPinRow::GetFlagState, Flag)
				.OnCheckStateChanged(this, &SCNAPinRow::HandleFlagChanged, Flag)
			];
	}
	const FSlateBrush* GetPinIcon() const
	{
		return FBlueprintEditorUtils::GetIconFromPin(GetPinType(), false);
	}
	FSlateColor GetPinColor() const
	{
		return GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(GetPinType());
	}
	FEdGraphPinType GetPinType() const
	{
		TSharedPtr<SCNAPinManager> Owner = Manager.Pin();
		return Owner.IsValid() ? Owner->GetItemType(Item) : Item->PinType;
	}
	bool IsEditable() const
	{
		TSharedPtr<SCNAPinManager> Owner = Manager.Pin();
		return Owner.IsValid() && Owner->IsEditingEnabled();
	}
	bool IsReadOnly() const
	{
		return !IsEditable();
	}
	ECheckBoxState GetFlagState(ECNAPinFlag Flag) const
	{
		TSharedPtr<SCNAPinManager> Owner = Manager.Pin();
		const bool bSet = Owner.IsValid() && FCNAPinManagerOps::GetPinFlag(Owner->GetAsset(), Item->PinName, Flag);
		return bSet ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	void HandleFlagChanged(ECheckBoxState NewState, ECNAPinFlag Flag)
	{
		if (TSharedPtr<SCNAPinManager> Owner = Manager.Pin())
		{
			FCNAPinManagerOps::SetPinFlag(Owner->GetAsset(), Item->PinName, Flag, NewState == ECheckBoxState::Checked);
		}
	}
	bool HandleVerifyName(const FText& NewText, FText& OutError)
	{
		TSharedPtr<SCNAPinManager> Owner = Manager.Pin();
		return Owner.IsValid() && Owner->HandleVerifyName(NewText, OutError, Item);
	}
	void HandleNameCommitted(const FText& NewText, ETextCommit::Type CommitType)
	{
		if (TSharedPtr<SCNAPinManager> Owner = Manager.Pin())
		{
			Owner->HandleNameCommitted(NewText, CommitType, Item);
		}
	}
	void HandleTypeChanged(const FEdGraphPinType& NewType)
	{
		if (TSharedPtr<SCNAPinManager> Owner = Manager.Pin())
		{
			Owner->HandleTypeChanged(NewType, Item);
		}
	}
	void HandleRemoveClicked()
	{
		if (TSharedPtr<SCNAPinManager> Owner = Manager.Pin())
		{
			Owner->HandleRemove(Item);
		}
	}
	FReply HandleDragDetected(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
	{
		if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && IsEditable())
		{
			return FReply::Handled().BeginDragDrop(FCNAPinDragDropOp::New(*Item));
		}
		return FReply::Unhandled();
	}
	TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FCNAPinManagerItemPtr TargetItem)
	{
		TSharedPtr<FCNAPinDragDropOp> Operation = DragDropEvent.GetOperationAs<FCNAPinDragDropOp>();
		if (!Operation.IsValid() || !TargetItem.IsValid() || Operation->Side != TargetItem->Side || Operation->PinName == TargetItem->PinName)
		{
			return TOptional<EItemDropZone>();
		}
		// Pins are only placed between rows.
		return DropZone == EItemDropZone::OntoItem ? EItemDropZone::BelowItem : DropZone;
	}
	FReply HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FCNAPinManagerItemPtr TargetItem)
	{
		TSharedPtr<FCNAPinDragDropOp> Operation = DragDropEvent.GetOperationAs<FCNAPinDragDropOp>();
		TSharedPtr<SCNAPinManager> Owner = Manager.Pin();
		if (!Operation.IsValid() || !Owner.IsValid() || !TargetItem.IsValid())
		{
			return FReply::Unhandled();
		}
		FCNAPinManagerItemPtr Dragged = MakeShareable(new FCNAPinManagerItem);
		Dragged->PinName = Operation->PinName;
		Dragged->Side = Operation->Side;
		Owner->HandleMove(Dragged, TargetItem, DropZone != EItemDropZone::AboveItem);
		return FReply::Handled();
	}
	FCNAPinManagerItemPtr Item;
	TWeakPtr<SCNAPinManager> Manager;
	/** Keeps the K2 pin widget alive: the value widget is bound to it. */
	FText Problem;
	bool bHasProblem = false;
};

// SCNAPinManager

void SCNAPinManager::Construct(const FArguments& InArgs, UCustomNodeAsset* InAsset)
{
	Asset = InAsset;

	const FCNAEditorCommands& Commands = FCNAEditorCommands::Get();
	const FCanExecuteAction CanEdit = FCanExecuteAction::CreateSP(this, &SCNAPinManager::IsEditingEnabled);
	const FCanExecuteAction CanEditSelection = FCanExecuteAction::CreateSP(this, &SCNAPinManager::HasSelection);

	CommandList = MakeShareable(new FUICommandList);
	CommandList->MapAction(FGenericCommands::Get().Rename, FExecuteAction::CreateSP(this, &SCNAPinManager::RenameSelected), CanEditSelection);
	CommandList->MapAction(FGenericCommands::Get().Delete, FExecuteAction::CreateSP(this, &SCNAPinManager::RemoveSelected), CanEditSelection);
	CommandList->MapAction(Commands.FindItem, FExecuteAction::CreateSP(this, &SCNAPinManager::FocusSearch));
	CommandList->MapAction(Commands.AddInputPin, FExecuteAction::CreateSP(this, &SCNAPinManager::AddPin, EGPD_Input), CanEdit);
	CommandList->MapAction(Commands.AddOutputPin, FExecuteAction::CreateSP(this, &SCNAPinManager::AddPin, EGPD_Output), CanEdit);
	CommandList->MapAction(Commands.MoveItemUp, FExecuteAction::CreateSP(this, &SCNAPinManager::MoveSelected, -1), CanEditSelection);
	CommandList->MapAction(Commands.MoveItemDown, FExecuteAction::CreateSP(this, &SCNAPinManager::MoveSelected, 1), CanEditSelection);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SAssignNew(SearchBox, SSearchBox)
			.HintText(LOCTEXT("PinManager.SearchHint", "Filter pins by name or type (Ctrl+F)"))
			.OnTextChanged(this, &SCNAPinManager::HandleFilterChanged)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 0.0f, 6.0f, 4.0f)
		[
			SNew(STextBlock)
			.Visibility(this, &SCNAPinManager::GetProblemVisibility)
			.AutoWrapText(true)
			.ColorAndOpacity(FLinearColor(1.0f, 0.45f, 0.35f))
			.Text(LOCTEXT("PinManager.NoTunnels", "The Execution Graph has no Tunnel Input or Tunnel Output (CNA005). Import the node from JSON or create a new asset."))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+ SSplitter::Slot()
			.Value(0.55f)
			[
				BuildSection(EGPD_Input)
			]
			+ SSplitter::Slot()
			.Value(0.45f)
			[
				BuildSection(EGPD_Output)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 4.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("PinManager.Hint", "Drag rows to change the order. F2 renames, Delete removes, Ctrl+F filters. Other pin settings are in Node Settings, Pins."))
		]
	];

	if (UCustomNodeAsset* CurrentAsset = Asset.Get())
	{
		BlueprintChangedHandle = CurrentAsset->OnChanged().AddSP(this, &SCNAPinManager::HandleBlueprintChanged);
		BlueprintCompiledHandle = CurrentAsset->OnCompiled().AddSP(this, &SCNAPinManager::HandleBlueprintChanged);
	}
	AssetChangedHandle = FCNAEditorHooks::OnAssetChanged.AddSP(this, &SCNAPinManager::HandleAssetChanged);
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
	Refresh();
}
SCNAPinManager::~SCNAPinManager()
{
	FCNAEditorHooks::OnAssetChanged.Remove(AssetChangedHandle);
	if (UCustomNodeAsset* CurrentAsset = Asset.Get())
	{
		CurrentAsset->OnChanged().Remove(BlueprintChangedHandle);
		CurrentAsset->OnCompiled().Remove(BlueprintCompiledHandle);
	}
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}
TSharedRef<SWidget> SCNAPinManager::BuildSection(EEdGraphPinDirection Side)
{
	using namespace CNAPinManagerLocal;
	const bool bInputs = Side == EGPD_Input;

	TSharedRef<SHeaderRow> Header = SNew(SHeaderRow)
		+ SHeaderRow::Column(ColumnName)
		.DefaultLabel(LOCTEXT("PinManager.ColName", "Name"))
		.FillWidth(0.34f)
		+ SHeaderRow::Column(ColumnType)
		.DefaultLabel(LOCTEXT("PinManager.ColType", "Type"))
		.FillWidth(0.33f);

	if (bInputs)
	{
		Header->AddColumn(SHeaderRow::Column(ColumnDefault)
			.DefaultLabel(LOCTEXT("PinManager.ColDefault", "Default"))
			.DefaultTooltip(LOCTEXT("PinManager.ColDefaultTip", "Value of the pin when nothing is connected."))
			.FillWidth(0.33f));
	}
	Header->AddColumn(SHeaderRow::Column(ColumnAdvanced)
		.DefaultLabel(LOCTEXT("PinManager.ColAdvanced", "Adv"))
		.DefaultTooltip(LOCTEXT("PinManager.ColAdvancedTip", "Advanced pin"))
		.FixedWidth(34.0f)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Center));
	Header->AddColumn(SHeaderRow::Column(ColumnHidden)
		.DefaultLabel(LOCTEXT("PinManager.ColHidden", "Hid"))
		.DefaultTooltip(LOCTEXT("PinManager.ColHiddenTip", "Hidden by default"))
		.FixedWidth(34.0f)
		.HAlignHeader(HAlign_Center)
		.HAlignCell(HAlign_Center));
	if (bInputs)
	{
		Header->AddColumn(SHeaderRow::Column(ColumnRequired)
			.DefaultLabel(LOCTEXT("PinManager.ColRequired", "Req"))
			.DefaultTooltip(LOCTEXT("PinManager.ColRequiredTip", "Required pin (CNA008)"))
			.FixedWidth(34.0f)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Center));
	}
	Header->AddColumn(SHeaderRow::Column(ColumnStatus)
		.DefaultLabel(FText::GetEmpty())
		.FixedWidth(22.0f));
	Header->AddColumn(SHeaderRow::Column(ColumnRemove)
		.DefaultLabel(FText::GetEmpty())
		.FixedWidth(24.0f));

	TSharedPtr<SListView<FCNAPinManagerItemPtr>>& List = bInputs ? InputList : OutputList;
	SAssignNew(List, SListView<FCNAPinManagerItemPtr>)
		.ListItemsSource(bInputs ? &InputItems : &OutputItems)
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &SCNAPinManager::GenerateRow)
		.OnSelectionChanged(this, &SCNAPinManager::HandleSelectionChanged, Side)
		.OnContextMenuOpening(this, &SCNAPinManager::MakeContextMenu)
		.HeaderRow(Header);

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 4.0f, 4.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FEditorStyle::GetFontStyle(TEXT("DetailsView.CategoryFontStyle")))
				.Text(this, &SCNAPinManager::GetSectionTitle, Side)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
				.ForegroundColor(FLinearColor::White)
				.ContentPadding(FMargin(6.0f, 1.0f))
				.IsEnabled(this, &SCNAPinManager::IsEditingEnabled)
				.ToolTipText(bInputs ? FCNAEditorCommands::Get().AddInputPin->GetDescription() : FCNAEditorCommands::Get().AddOutputPin->GetDescription())
				.OnClicked_Lambda([this, Side]()
				{
					AddPin(Side);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(bInputs ? LOCTEXT("PinManager.AddInput", "+ Input") : LOCTEXT("PinManager.AddOutput", "+ Output"))
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			List.ToSharedRef()
		];
}
TSharedRef<ITableRow> SCNAPinManager::GenerateRow(FCNAPinManagerItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SCNAPinRow, OwnerTable)
		.Item(Item)
		.Manager(SharedThis(this));
}
TSharedPtr<SWidget> SCNAPinManager::MakeContextMenu()
{
	const FCNAEditorCommands& Commands = FCNAEditorCommands::Get();
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/ true, CommandList);
	MenuBuilder.BeginSection("CNAPin", LOCTEXT("PinManager.MenuPin", "Pin"));
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
		MenuBuilder.AddMenuEntry(Commands.MoveItemUp);
		MenuBuilder.AddMenuEntry(Commands.MoveItemDown);
	}
	MenuBuilder.EndSection();
	MenuBuilder.BeginSection("CNAPinAdd", LOCTEXT("PinManager.MenuAdd", "Add"));
	{
		MenuBuilder.AddMenuEntry(Commands.AddInputPin);
		MenuBuilder.AddMenuEntry(Commands.AddOutputPin);
	}
	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}
void SCNAPinManager::HandleSelectionChanged(FCNAPinManagerItemPtr Item, ESelectInfo::Type SelectInfo, EEdGraphPinDirection Side)
{
	// One selection across both lists: F2 and Delete act on it.
	if (Item.IsValid())
	{
		TSharedPtr<SListView<FCNAPinManagerItemPtr>> Other = Side == EGPD_Input ? OutputList : InputList;
		if (Other.IsValid() && Other->GetNumItemsSelected() > 0)
		{
			Other->ClearSelection();
		}
	}
}
void SCNAPinManager::HandleFilterChanged(const FText& NewText)
{
	FilterText = NewText.ToString().TrimStartAndEnd();
	RequestRefresh(true);
}
void SCNAPinManager::RequestRefresh(bool bForce)
{
	bForceRefresh |= bForce;
	if (!bRefreshPending)
	{
		bRefreshPending = true;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SCNAPinManager::HandleRefreshTimer));
	}
}
EActiveTimerReturnType SCNAPinManager::HandleRefreshTimer(double InCurrentTime, float InDeltaTime)
{
	bRefreshPending = false;
	// Every edit of the graphs marks the asset as modified; the rows are only rebuilt when they would change.
	if (bForceRefresh || BuildStateKey() != LastStateKey)
	{
		Refresh();
	}
	bForceRefresh = false;
	return EActiveTimerReturnType::Stop;
}
void SCNAPinManager::Refresh()
{
	const FCNAPinManagerItemPtr Selected = GetSelectedItem();
	const FName SelectedName = Selected.IsValid() ? Selected->PinName : NAME_None;

	InputItems.Reset();
	OutputItems.Reset();
	NumInputs = 0;
	NumOutputs = 0;

	UCustomNodeAsset* CurrentAsset = Asset.Get();
	UK2Node_Tunnel* Entry = FCNAPinManagerOps::GetTunnel(CurrentAsset, EGPD_Input);
	UK2Node_Tunnel* Exit = FCNAPinManagerOps::GetTunnel(CurrentAsset, EGPD_Output);
	bHasTunnels = Entry != nullptr && Exit != nullptr;

	auto Collect = [this](UK2Node_Tunnel* Tunnel, EEdGraphPinDirection Side, TArray<FCNAPinManagerItemPtr>& OutItems, int32& OutCount)
	{
		if (!Tunnel)
		{
			return;
		}
		for (int32 Index = 0; Index < Tunnel->UserDefinedPins.Num(); ++Index)
		{
			const TSharedPtr<FUserPinInfo>& Info = Tunnel->UserDefinedPins[Index];
			if (!Info.IsValid())
			{
				continue;
			}
			++OutCount;
			FCNAPinManagerItemPtr Item = MakeShareable(new FCNAPinManagerItem);
			Item->PinName = Info->PinName;
			Item->Side = Side;
			Item->PinType = Info->PinType;
			Item->Index = Index;
			if (PassesFilter(*Item))
			{
				OutItems.Add(Item);
			}
		}
	};
	Collect(Entry, EGPD_Input, InputItems, NumInputs);
	Collect(Exit, EGPD_Output, OutputItems, NumOutputs);

	for (TSharedPtr<SListView<FCNAPinManagerItemPtr>> List : { InputList, OutputList })
	{
		if (List.IsValid())
		{
			List->RequestListRefresh();
		}
	}
	if (!SelectedName.IsNone())
	{
		for (const FCNAPinManagerItemPtr& Item : InputItems)
		{
			if (Item->PinName == SelectedName)
			{
				InputList->SetSelection(Item, ESelectInfo::Direct);
			}
		}
		for (const FCNAPinManagerItemPtr& Item : OutputItems)
		{
			if (Item->PinName == SelectedName)
			{
				OutputList->SetSelection(Item, ESelectInfo::Direct);
			}
		}
	}
	LastStateKey = BuildStateKey();
}
FString SCNAPinManager::BuildStateKey() const
{
	const UCustomNodeAsset* CurrentAsset = Asset.Get();
	FString Key = FilterText;
	for (const EEdGraphPinDirection Side : { EGPD_Input, EGPD_Output })
	{
		const UK2Node_Tunnel* Tunnel = FCNAPinManagerOps::GetTunnel(CurrentAsset, Side);
		Key += Tunnel ? TEXT("|T") : TEXT("|-");
		if (!Tunnel)
		{
			continue;
		}
		for (const TSharedPtr<FUserPinInfo>& Info : Tunnel->UserDefinedPins)
		{
			if (!Info.IsValid())
			{
				continue;
			}
			// Default values are edited live by their widgets and are not part of the key.
			Key += FString::Printf(TEXT("|%s:%s:%d%d%d"),
				*Info->PinName.ToString(),
				*FCNAPinUtils::GetTypeSignatureString(Info->PinType),
				FCNAPinManagerOps::GetPinFlag(CurrentAsset, Info->PinName, ECNAPinFlag::Advanced) ? 1 : 0,
				FCNAPinManagerOps::GetPinFlag(CurrentAsset, Info->PinName, ECNAPinFlag::Hidden) ? 1 : 0,
				FCNAPinManagerOps::GetPinFlag(CurrentAsset, Info->PinName, ECNAPinFlag::Required) ? 1 : 0);
			FText Problem;
			if (FCNAPinManagerOps::GetPinProblem(CurrentAsset, Side, Info->PinName, Problem))
			{
				Key += Problem.ToString();
			}
		}
	}
	return Key;
}
bool SCNAPinManager::PassesFilter(const FCNAPinManagerItem& Item) const
{
	if (FilterText.IsEmpty())
	{
		return true;
	}
	return Item.PinName.ToString().Contains(FilterText, ESearchCase::IgnoreCase)
		|| FCNAPinUtils::GetTypeText(Item.PinType).ToString().Contains(FilterText, ESearchCase::IgnoreCase);
}
void SCNAPinManager::HandleAssetChanged(UCustomNodeAsset* ChangedAsset, FName PropertyName)
{
	if (!ChangedAsset || ChangedAsset != Asset.Get())
	{
		return;
	}
	// Runs inside the transaction of the edit (the default value widgets mark the asset as modified).
	FCNAPinManagerOps::SyncTunnelDefaults(ChangedAsset);
	RequestRefresh(false);
}
void SCNAPinManager::HandleBlueprintChanged(UBlueprint* Blueprint)
{
	RequestRefresh(false);
}
void SCNAPinManager::PostUndo(bool bSuccess)
{
	RequestRefresh(true);
}
void SCNAPinManager::PostRedo(bool bSuccess)
{
	RequestRefresh(true);
}
FReply SCNAPinManager::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList.IsValid() && CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}
void SCNAPinManager::FocusSearch()
{
	if (SearchBox.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(SearchBox, EFocusCause::SetDirectly);
	}
}
bool SCNAPinManager::IsEditingEnabled() const
{
	return Asset.IsValid() && bHasTunnels;
}
bool SCNAPinManager::HandleVerifyName(const FText& NewText, FText& OutError, FCNAPinManagerItemPtr Item) const
{
	return Item.IsValid() && FCNAPinManagerOps::VerifyPinName(Asset.Get(), Item->PinName, NewText.ToString(), OutError);
}
void SCNAPinManager::HandleNameCommitted(const FText& NewText, ETextCommit::Type CommitType, FCNAPinManagerItemPtr Item)
{
	if (!Item.IsValid() || CommitType == ETextCommit::OnCleared)
	{
		return;
	}
	const FName NewName(*NewText.ToString().TrimStartAndEnd());
	if (NewName == Item->PinName || NewName.IsNone())
	{
		return;
	}
	FText Error;
	if (!FCNAPinManagerOps::RenamePin(Asset.Get(), Item->Side, Item->PinName, NewName, Error))
	{
		CNAPinManagerLocal::ShowNotification(Error, false);
	}
	RequestRefresh(true);
}
FEdGraphPinType SCNAPinManager::GetItemType(FCNAPinManagerItemPtr Item) const
{
	if (!Item.IsValid())
	{
		return FEdGraphPinType();
	}
	const TSharedPtr<FUserPinInfo> Info = FCNAPinManagerOps::FindUserPin(Asset.Get(), Item->Side, Item->PinName);
	return Info.IsValid() ? Info->PinType : Item->PinType;
}
void SCNAPinManager::HandleTypeChanged(const FEdGraphPinType& NewType, FCNAPinManagerItemPtr Item)
{
	if (!Item.IsValid())
	{
		return;
	}
	FText Error;
	if (!FCNAPinManagerOps::ChangePinType(Asset.Get(), Item->Side, Item->PinName, NewType, Error))
	{
		CNAPinManagerLocal::ShowNotification(Error, false);
	}
	RequestRefresh(true);
}
void SCNAPinManager::HandleRemove(FCNAPinManagerItemPtr Item)
{
	UCustomNodeAsset* CurrentAsset = Asset.Get();
	if (!Item.IsValid() || !CurrentAsset)
	{
		return;
	}
	// Wires inside the Execution Graph are lost; the instances report lost wires on update (CNA016).
	const UEdGraphPin* Pin = FCNAPinManagerOps::FindTunnelPin(CurrentAsset, Item->Side, Item->PinName);
	if (Pin && FCNAPinUtils::IsPinOrSubPinLinked(Pin))
	{
		const FText Question = FText::Format(
			LOCTEXT("PinManager.RemoveLinkedQuestion", "Pin {0} is connected inside the Execution Graph. Remove it together with its wires?"),
			FText::FromName(Item->PinName));
		if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
		{
			return;
		}
	}
	FCNAPinManagerOps::RemovePin(CurrentAsset, Item->Side, Item->PinName);
	RequestRefresh(true);
}
void SCNAPinManager::HandleMove(FCNAPinManagerItemPtr Item, FCNAPinManagerItemPtr Target, bool bBelow)
{
	UCustomNodeAsset* CurrentAsset = Asset.Get();
	UK2Node_Tunnel* Tunnel = Item.IsValid() ? FCNAPinManagerOps::GetTunnel(CurrentAsset, Item->Side) : nullptr;
	if (!Tunnel || !Target.IsValid() || Item->Side != Target->Side)
	{
		return;
	}
	auto IndexOf = [Tunnel](FName PinName)
	{
		return Tunnel->UserDefinedPins.IndexOfByPredicate([PinName](const TSharedPtr<FUserPinInfo>& Info)
		{
			return Info.IsValid() && Info->PinName == PinName;
		});
	};
	const int32 From = IndexOf(Item->PinName);
	int32 To = IndexOf(Target->PinName);
	if (From == INDEX_NONE || To == INDEX_NONE)
	{
		return;
	}
	if (bBelow)
	{
		++To;
	}
	if (From < To)
	{
		--To;
	}
	FCNAPinManagerOps::MovePin(CurrentAsset, Item->Side, Item->PinName, To);
	RequestRefresh(true);
}
FCNAPinManagerItemPtr SCNAPinManager::GetSelectedItem() const
{
	for (const TSharedPtr<SListView<FCNAPinManagerItemPtr>>& List : { InputList, OutputList })
	{
		if (List.IsValid())
		{
			const TArray<FCNAPinManagerItemPtr> Selected = List->GetSelectedItems();
			if (Selected.Num() > 0)
			{
				return Selected[0];
			}
		}
	}
	return FCNAPinManagerItemPtr();
}
bool SCNAPinManager::HasSelection() const
{
	return IsEditingEnabled() && GetSelectedItem().IsValid();
}
void SCNAPinManager::RenameSelected()
{
	FCNAPinManagerItemPtr Item = GetSelectedItem();
	if (!Item.IsValid())
	{
		return;
	}
	TSharedPtr<SListView<FCNAPinManagerItemPtr>> List = Item->Side == EGPD_Input ? InputList : OutputList;
	if (List.IsValid())
	{
		List->RequestScrollIntoView(Item);
	}
	Item->OnRenameRequested.ExecuteIfBound();
}
void SCNAPinManager::RemoveSelected()
{
	HandleRemove(GetSelectedItem());
}
void SCNAPinManager::MoveSelected(int32 Delta)
{
	FCNAPinManagerItemPtr Item = GetSelectedItem();
	UK2Node_Tunnel* Tunnel = Item.IsValid() ? FCNAPinManagerOps::GetTunnel(Asset.Get(), Item->Side) : nullptr;
	if (!Tunnel)
	{
		return;
	}
	const FName PinName = Item->PinName;
	const int32 Index = Tunnel->UserDefinedPins.IndexOfByPredicate([PinName](const TSharedPtr<FUserPinInfo>& Info)
	{
		return Info.IsValid() && Info->PinName == PinName;
	});
	if (Index != INDEX_NONE)
	{
		FCNAPinManagerOps::MovePin(Asset.Get(), Item->Side, PinName, Index + Delta);
		RequestRefresh(true);
	}
}
void SCNAPinManager::AddPin(EEdGraphPinDirection Side)
{
	FEdGraphPinType Type;
	Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;

	FText Error;
	const FName NewName = FCNAPinManagerOps::AddPin(Asset.Get(), Side, Type, NAME_None, &Error);
	if (NewName.IsNone())
	{
		CNAPinManagerLocal::ShowNotification(Error, false);
		return;
	}
	// The filter would hide the new row.
	if (!FilterText.IsEmpty() && SearchBox.IsValid())
	{
		SearchBox->SetText(FText::GetEmpty());
		FilterText.Reset();
	}
	Refresh();

	// Select the new pin and start renaming it once its row exists (the list generates rows on the next frame).
	TArray<FCNAPinManagerItemPtr>& Items = Side == EGPD_Input ? InputItems : OutputItems;
	TSharedPtr<SListView<FCNAPinManagerItemPtr>> List = Side == EGPD_Input ? InputList : OutputList;
	for (const FCNAPinManagerItemPtr& Item : Items)
	{
		if (Item->PinName == NewName && List.IsValid())
		{
			List->SetSelection(Item, ESelectInfo::Direct);
			List->RequestScrollIntoView(Item);
			const TWeakPtr<FCNAPinManagerItem> WeakItem = Item;
			RegisterActiveTimer(0.05f, FWidgetActiveTimerDelegate::CreateLambda([WeakItem](double, float)
			{
				if (TSharedPtr<FCNAPinManagerItem> Pinned = WeakItem.Pin())
				{
					Pinned->OnRenameRequested.ExecuteIfBound();
				}
				return EActiveTimerReturnType::Stop;
			}));
			break;
		}
	}
}
FText SCNAPinManager::GetSectionTitle(EEdGraphPinDirection Side) const
{
	return Side == EGPD_Input
		? FText::Format(LOCTEXT("PinManager.InputsTitle", "Inputs (Tunnel Input): {0}"), FText::AsNumber(NumInputs))
		: FText::Format(LOCTEXT("PinManager.OutputsTitle", "Outputs (Tunnel Output): {0}"), FText::AsNumber(NumOutputs));
}
EVisibility SCNAPinManager::GetProblemVisibility() const
{
	return (Asset.IsValid() && !bHasTunnels) ? EVisibility::Visible : EVisibility::Collapsed;
}
#undef LOCTEXT_NAMESPACE
