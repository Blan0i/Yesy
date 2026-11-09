// Copyright (c) NodeFactory. Set Local: writes a local variable of the subgraph (spec 4.8, D9).

#include "K2Node_SetLocal.h"
#include "CNANodeMenu.h"
#include "CNAPinUtils.h"
#include "CustomNodeAsset.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "EdGraphSchema_K2.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

const FName UK2Node_SetLocal::OutputPinName(TEXT("Output_Get"));

UK2Node_SetLocal::UK2Node_SetLocal(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UEdGraphPin* UK2Node_SetLocal::GetExecPin() const
{
	return FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
}

UEdGraphPin* UK2Node_SetLocal::GetThenPin() const
{
	return FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
}

UEdGraphPin* UK2Node_SetLocal::GetValueInputPin() const
{
	return FindPin(ValuePinName, EGPD_Input);
}

UEdGraphPin* UK2Node_SetLocal::GetValueOutputPin() const
{
	return FindPin(OutputPinName, EGPD_Output);
}

void UK2Node_SetLocal::AllocateDefaultPins()
{
	SyncCacheFromVariable();

	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	if (UEdGraphPin* ValuePin = CreateValuePin(EGPD_Input, ValuePinName))
	{
		ValuePin->PinFriendlyName = GetVariableDisplayName();
		if (const FCNALocalVariable* Variable = FindVariable())
		{
			ValuePin->PinToolTip = Variable->Tooltip.ToString();
			// The default of the pin starts as the default of the variable.
			if (!Variable->DefaultValue.IsEmpty())
			{
				GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(*ValuePin, Variable->DefaultValue);
			}
		}
	}

	if (UEdGraphPin* OutputPin = CreateValuePin(EGPD_Output, OutputPinName))
	{
		// Shown without a label, like the output of the Set node of member variables.
		OutputPin->PinFriendlyName = FText::FromString(TEXT(" "));
		OutputPin->PinToolTip = LOCTEXT("SetLocalOutputTooltip", "Value of the variable after the assignment.").ToString();
	}

	Super::AllocateDefaultPins();
}

FText UK2Node_SetLocal::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::FullTitle)
	{
		return FText::Format(LOCTEXT("SetLocalFullTitle", "Set {0}\nLocal Variable"), GetVariableDisplayName());
	}
	return FText::Format(LOCTEXT("SetLocalTitle", "Set {0}"), GetVariableDisplayName());
}

FText UK2Node_SetLocal::GetTooltipText() const
{
	const FCNALocalVariable* Variable = FindVariable();
	if (!Variable)
	{
		return FText::Format(LOCTEXT("SetLocalMissingTooltip", "The local variable {0} does not exist in this asset (CNA017)."), GetVariableDisplayName());
	}
	return FText::Format(LOCTEXT("SetLocalTooltip", "Set {0} ({1})\nThe value lives while this node instance executes; every entry through an exec input resets it to {2}."),
		FText::FromName(Variable->Name), FCNAPinUtils::GetTypeText(Variable->Type),
		Variable->DefaultValue.IsEmpty() ? LOCTEXT("SetLocalTypeDefault", "the default of the type") : FText::FromString(Variable->DefaultValue));
}

void UK2Node_SetLocal::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	FCNANodeMenu::RegisterLocalVariableActions(ActionRegistrar, GetClass());
}

#undef LOCTEXT_NAMESPACE
