// Copyright (c) NodeFactory. Get Caller: the object that executes the custom node (spec 4.9, D9).

#include "K2Node_GetCaller.h"
#include "CNAGraphUtils.h"
#include "CustomNodeAsset.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "EditorStyleSet.h"
#include "GraphEditorSettings.h"
#include "KismetCompiler.h"
#include "Textures/SlateIcon.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

const FName UK2Node_GetCaller::CallerPinName(TEXT("Caller"));

UK2Node_GetCaller::UK2Node_GetCaller(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UEdGraphPin* UK2Node_GetCaller::GetCallerPin() const
{
	return FindPin(CallerPinName, EGPD_Output);
}

UCustomNodeAsset* UK2Node_GetCaller::GetOwningAsset() const
{
	return FCNAGraphUtils::GetOwningAsset(this);
}

UClass* UK2Node_GetCaller::GetCallerClass() const
{
	const UCustomNodeAsset* Asset = GetOwningAsset();
	UClass* ContextClass = Asset ? Asset->ContextClass.Get() : nullptr;
	return ContextClass ? ContextClass : UObject::StaticClass();
}

void UK2Node_GetCaller::AllocateDefaultPins()
{
	FEdGraphPinType Type;
	Type.PinCategory = UEdGraphSchema_K2::PC_Object;
	Type.PinSubCategoryObject = GetCallerClass();
	UEdGraphPin* Pin = CreatePin(EGPD_Output, Type, CallerPinName);
	Pin->PinFriendlyName = GetCallerClass()->GetDisplayNameText();
	Pin->PinToolTip = LOCTEXT("CallerPinTooltip", "The object whose Blueprint executes the custom node (Self of the caller).").ToString();
	Super::AllocateDefaultPins();
}

FText UK2Node_GetCaller::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("GetCallerTitle", "Get Caller");
}

FText UK2Node_GetCaller::GetTooltipText() const
{
	return FText::Format(
		LOCTEXT("GetCallerTooltip", "Returns the object that executes this custom node ({0}).\nConnect it to Target pins of its functions and to WorldContext pins."),
		GetCallerClass()->GetDisplayNameText());
}

FLinearColor UK2Node_GetCaller::GetNodeTitleColor() const
{
	return GetDefault<UGraphEditorSettings>()->PureFunctionCallNodeTitleColor;
}

FSlateIcon UK2Node_GetCaller::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	return FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("Kismet.AllClasses.FunctionIcon"));
}

bool UK2Node_GetCaller::IsCompatibleWithGraph(const UEdGraph* TargetGraph) const
{
	// Execution Graph of a Custom Node Asset only; allowed in pure nodes (spec 4.9).
	return Super::IsCompatibleWithGraph(TargetGraph) && FCNAGraphUtils::GetExecuteGraphOwner(TargetGraph) != nullptr;
}

FText UK2Node_GetCaller::GetKeywords() const
{
	return LOCTEXT("GetCallerKeywords", "self owner caller world context this");
}

FText UK2Node_GetCaller::GetMenuCategory() const
{
	return LOCTEXT("GetCallerMenuCategory", "Custom Node");
}

void UK2Node_GetCaller::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		check(Spawner != nullptr);
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

void UK2Node_GetCaller::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);
	// FCNAExpander replaces Get Caller by Self and leaves it without wires. A wired node here sits in a graph that is
	// not an Execution Graph; an unwired one has no effect and is pruned by the compiler.
	const UEdGraphPin* CallerPin = GetCallerPin();
	if (CallerPin && CallerPin->LinkedTo.Num() > 0)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("GetCallerOutsideAsset", "@@ can only be used inside the Execution Graph of a Custom Node Asset.").ToString(), this);
		BreakAllNodeLinks();
	}
}

#undef LOCTEXT_NAMESPACE
