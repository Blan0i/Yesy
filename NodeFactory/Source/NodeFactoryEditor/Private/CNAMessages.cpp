// Copyright (c) NodeFactory. Message codes CNA001-CNA019, their texts and the Message Log "NodeFactory" (spec 10.1).

#include "CNAMessages.h"
#include "CNAEditorSettings.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryModule.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/MessageLog.h"
#include "MessageLogInitializationOptions.h"
#include "MessageLogModule.h"
#include "Misc/UObjectToken.h"
#include "Modules/ModuleManager.h"
#include "Toolkits/AssetEditorManager.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

const FName FCNAMessages::LogName(TEXT("NodeFactory"));

namespace CNAMessagesLocal
{
	/** Clicking a link of the Message Log opens the Blueprint at the node or the asset editor (spec 10.1). */
	void HandleTokenActivated(const TSharedRef<IMessageToken>& Token)
	{
		if (Token->GetType() != EMessageToken::Object)
		{
			return;
		}
		const TSharedRef<FUObjectToken> ObjectToken = StaticCastSharedRef<FUObjectToken>(Token);
		UObject* Object = ObjectToken->GetObject().Get();
		if (!Object)
		{
			return;
		}
		if (Object->IsA<UEdGraphNode>())
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Object);
		}
		else if (Object->IsAsset())
		{
			FAssetEditorManager::Get().OpenEditorForAsset(Object);
		}
	}

	TSharedRef<FTokenizedMessage> MakeMessage(EMessageSeverity::Type Severity, const FText& Text, const UObject* Context)
	{
		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(Severity);
		if (Context)
		{
			FText Label;
			if (const UEdGraphNode* Node = Cast<UEdGraphNode>(Context))
			{
				Label = Node->GetNodeTitle(ENodeTitleType::ListView);
			}
			else
			{
				Label = FText::FromString(Context->GetName());
			}
			Message->AddToken(FUObjectToken::Create(Context, Label)
				->OnMessageTokenActivated(FOnMessageTokenActivated::CreateStatic(&HandleTokenActivated)));
		}
		Message->AddToken(FTextToken::Create(Text));
		return Message;
	}

	void WriteToOutputLog(EMessageSeverity::Type Severity, const FText& Text, const UObject* Context)
	{
		const FString ContextName = Context ? Context->GetPathName() : FString(TEXT("-"));
		switch (Severity)
		{
		case EMessageSeverity::CriticalError:
		case EMessageSeverity::Error:
			UE_LOG(LogNodeFactory, Error, TEXT("%s [%s]"), *Text.ToString(), *ContextName);
			break;
		case EMessageSeverity::PerformanceWarning:
		case EMessageSeverity::Warning:
			UE_LOG(LogNodeFactory, Warning, TEXT("%s [%s]"), *Text.ToString(), *ContextName);
			break;
		default:
			UE_LOG(LogNodeFactory, Log, TEXT("%s [%s]"), *Text.ToString(), *ContextName);
			break;
		}
	}
}

// FCNADiagnostic / FCNADiagnosticList

bool FCNADiagnostic::IsError() const
{
	return !bDowngradedToWarning && FCNAMessages::IsError(Code);
}

EMessageSeverity::Type FCNADiagnostic::GetSeverity() const
{
	const EMessageSeverity::Type Severity = FCNAMessages::GetSeverity(Code);
	return bDowngradedToWarning && Severity == EMessageSeverity::Error ? EMessageSeverity::Warning : Severity;
}

void FCNADiagnosticList::Add(ECNAMessage Code, const FFormatNamedArguments& Arguments, const UObject* Context)
{
	FCNADiagnostic Diagnostic;
	Diagnostic.Code = Code;
	Diagnostic.Text = FCNAMessages::Format(Code, Arguments);
	Diagnostic.Context = Context;
	Items.Add(Diagnostic);
}

void FCNADiagnosticList::AddText(ECNAMessage Code, const FText& Details, const UObject* Context, bool bAsWarning)
{
	FCNADiagnostic Diagnostic;
	Diagnostic.Code = Code;
	Diagnostic.Text = FText::Format(LOCTEXT("Msg.WithCode", "{0}: {1}"), FText::FromString(FCNAMessages::GetCode(Code)), Details);
	Diagnostic.Context = Context;
	Diagnostic.bDowngradedToWarning = bAsWarning;
	Items.Add(Diagnostic);
}

bool FCNADiagnosticList::HasErrors() const
{
	return Items.ContainsByPredicate([](const FCNADiagnostic& Item) { return Item.IsError(); });
}

bool FCNADiagnosticList::Contains(ECNAMessage Code) const
{
	return Items.ContainsByPredicate([Code](const FCNADiagnostic& Item) { return Item.Code == Code; });
}

int32 FCNADiagnosticList::NumErrors() const
{
	int32 Count = 0;
	for (const FCNADiagnostic& Item : Items)
	{
		Count += Item.IsError() ? 1 : 0;
	}
	return Count;
}

int32 FCNADiagnosticList::NumWarnings() const
{
	return Items.Num() - NumErrors();
}

void FCNADiagnosticList::FlushToMessageLog() const
{
	for (const FCNADiagnostic& Item : Items)
	{
		FCNAMessages::ReportTextToLogWithSeverity(Item.GetSeverity(), Item.Text, Item.Context.Get());
	}
}

void FCNADiagnosticList::FlushToCompilerLog(FCompilerResultsLog& Log, const UEdGraphNode* Node) const
{
	for (const FCNADiagnostic& Item : Items)
	{
		FCNAMessages::ReportTextToCompilerWithSeverity(Log, Node, Item.IsError(), Item.Text);
	}
}

void FCNADiagnosticList::GetTexts(TArray<FText>& OutTexts) const
{
	for (const FCNADiagnostic& Item : Items)
	{
		OutTexts.Add(Item.Text);
	}
}

// Codes

FString FCNAMessages::GetCode(ECNAMessage Code)
{
	return FString::Printf(TEXT("CNA%03d"), static_cast<int32>(Code));
}

ECNAMessage FCNAMessages::ParseCode(const FString& Text)
{
	if (Text.Len() >= 6 && Text.StartsWith(TEXT("CNA")))
	{
		const int32 Number = FCString::Atoi(*Text.Mid(3, 3));
		if (Number >= 1 && Number < static_cast<int32>(ECNAMessage::Count))
		{
			return static_cast<ECNAMessage>(Number);
		}
	}
	return ECNAMessage::Count;
}

EMessageSeverity::Type FCNAMessages::GetSeverity(ECNAMessage Code)
{
	switch (Code)
	{
	case ECNAMessage::CNA006:
	case ECNAMessage::CNA009:
	case ECNAMessage::CNA011:
	case ECNAMessage::CNA014:
	case ECNAMessage::CNA016:
	case ECNAMessage::CNA019:
		return EMessageSeverity::Warning;
	default:
		return EMessageSeverity::Error;
	}
}

bool FCNAMessages::IsError(ECNAMessage Code)
{
	return GetSeverity(Code) == EMessageSeverity::Error;
}

FText FCNAMessages::GetTemplate(ECNAMessage Code)
{
	// Keys Msg.CNAxxx in the namespace NodeFactory (spec 10.1). NSLOCTEXT keeps the namespace explicit.
	switch (Code)
	{
	case ECNAMessage::CNA001: return NSLOCTEXT("NodeFactory", "Msg.CNA001", "Cycle of nested custom nodes: {Chain}. Expansion stopped.");
	case ECNAMessage::CNA002: return NSLOCTEXT("NodeFactory", "Msg.CNA002", "Expansion depth {Depth} of {Asset} exceeds the limit {Limit} (Editor Preferences, NodeFactory, Max Expansion Depth).");
	case ECNAMessage::CNA003: return NSLOCTEXT("NodeFactory", "Msg.CNA003", "{Asset} contains latent nodes ({Nodes}) and cannot be used in this graph. Latent nodes are only allowed in an Event Graph.");
	case ECNAMessage::CNA004: return NSLOCTEXT("NodeFactory", "Msg.CNA004", "{Asset} is pure, but its subgraph contains execution nodes: {Nodes}.");
	case ECNAMessage::CNA005: return NSLOCTEXT("NodeFactory", "Msg.CNA005", "Custom Node Asset {Asset}, its macro Execute or its Tunnel nodes were not found.");
	case ECNAMessage::CNA006: return NSLOCTEXT("NodeFactory", "Msg.CNA006", "The node is outdated: NodeVersion {NodeVersion} differs from AssetVersion {AssetVersion} of {Asset}. Use Recompile Node to update it.");
	case ECNAMessage::CNA007: return NSLOCTEXT("NodeFactory", "Msg.CNA007", "The Wildcard type of {Asset} is not resolved. Connect a wire to one of its Wildcard pins.");
	case ECNAMessage::CNA008: return NSLOCTEXT("NodeFactory", "Msg.CNA008", "Required pin {Pin} of {Asset} has no wire and no value.");
	case ECNAMessage::CNA009: return NSLOCTEXT("NodeFactory", "Msg.CNA009", "{Asset} is deprecated: {Message}");
	case ECNAMessage::CNA010: return NSLOCTEXT("NodeFactory", "Msg.CNA010", "Type {Type} of pin {Pin} was not found: the class, structure or enumeration is missing.");
	case ECNAMessage::CNA011: return NSLOCTEXT("NodeFactory", "Msg.CNA011", "Behavior of {Asset}: {Details}");
	case ECNAMessage::CNA012: return NSLOCTEXT("NodeFactory", "Msg.CNA012", "Blueprint {Blueprint} does not derive from {ContextClass} required by {Asset} or does not provide a world context.");
	case ECNAMessage::CNA013: return NSLOCTEXT("NodeFactory", "Msg.CNA013", "{Count} dynamic pins of {Pin} are outside the range {Min}..{Max}.");
	case ECNAMessage::CNA014: return NSLOCTEXT("NodeFactory", "Msg.CNA014", "Pin override {Pin} of {Asset} does not match any pin of the macro Execute.");
	case ECNAMessage::CNA015: return NSLOCTEXT("NodeFactory", "Msg.CNA015", "PassThroughOf of pin {Pin} points to {Target}, which does not exist or has an incompatible type.");
	case ECNAMessage::CNA016: return NSLOCTEXT("NodeFactory", "Msg.CNA016", "Updating {Asset} in {Blueprint} lost the wire of pin {Pin}.");
	case ECNAMessage::CNA017: return NSLOCTEXT("NodeFactory", "Msg.CNA017", "{Node} references the local variable {Variable}, which does not exist in {Asset}.");
	case ECNAMessage::CNA018: return NSLOCTEXT("NodeFactory", "Msg.CNA018", "{Asset} is pure and must not use local variables.");
	case ECNAMessage::CNA019: return NSLOCTEXT("NodeFactory", "Msg.CNA019", "Icon texture {Texture} was not found or cannot be loaded; the default icon is used.");
	case ECNAMessage::CNA020: return NSLOCTEXT("NodeFactory", "Msg.CNA020", "Curve {Curve} of {Asset} uses Playback, which only works on an impure node placed in an Event Graph.");
	case ECNAMessage::CNA021: return NSLOCTEXT("NodeFactory", "Msg.CNA021", "{Asset} has more than one curve in Playback mode ({Curves}); only one is allowed per node.");
	case ECNAMessage::CNA022: return NSLOCTEXT("NodeFactory", "Msg.CNA022", "Curve {Curve} of {Asset}: {Details}");
	case ECNAMessage::CNA023: return NSLOCTEXT("NodeFactory", "Msg.CNA023", "{Node} references the curve {Curve}, which does not exist in {Asset}.");
	default:                  return NSLOCTEXT("NodeFactory", "Msg.Unknown", "Unknown NodeFactory message.");
	}
}

FText FCNAMessages::Format(ECNAMessage Code, const FFormatNamedArguments& Arguments)
{
	return FText::Format(LOCTEXT("Msg.WithCode", "{0}: {1}"), FText::FromString(GetCode(Code)), FText::Format(GetTemplate(Code), Arguments));
}

FFormatNamedArguments FCNAMessages::AssetArgs(const UObject* Asset)
{
	FFormatNamedArguments Arguments;
	if (const UCustomNodeAsset* CustomAsset = Cast<UCustomNodeAsset>(Asset))
	{
		const FText Display = CustomAsset->GetEffectiveDisplayName();
		const FString AssetName = CustomAsset->GetName();
		Arguments.Add(TEXT("Asset"), Display.ToString() == AssetName
			? Display
			: FText::Format(LOCTEXT("Msg.AssetWithName", "{0} ({1})"), Display, FText::FromString(AssetName)));
	}
	else if (Asset)
	{
		Arguments.Add(TEXT("Asset"), FText::FromString(Asset->GetName()));
	}
	else
	{
		Arguments.Add(TEXT("Asset"), LOCTEXT("Msg.NoAsset", "<missing asset>"));
	}
	return Arguments;
}

// Output

void FCNAMessages::ReportToCompiler(FCompilerResultsLog& Log, const UEdGraphNode* Node, ECNAMessage Code, const FFormatNamedArguments& Arguments)
{
	ReportTextToCompiler(Log, Node, Code, Format(Code, Arguments));
}

void FCNAMessages::ReportTextToCompiler(FCompilerResultsLog& Log, const UEdGraphNode* Node, ECNAMessage Code, const FText& Text)
{
	ReportTextToCompilerWithSeverity(Log, Node, IsError(Code), Text);
}

void FCNAMessages::ReportTextToCompilerWithSeverity(FCompilerResultsLog& Log, const UEdGraphNode* Node, bool bError, const FText& Text)
{
	// "@@" becomes a link to the node in Compiler Results (spec 5.4).
	if (Node)
	{
		const FString Line = FString(TEXT("@@ ")) + Text.ToString();
		if (bError)
		{
			Log.Error(*Line, Node);
		}
		else
		{
			Log.Warning(*Line, Node);
		}
	}
	else if (bError)
	{
		Log.Error(*Text.ToString());
	}
	else
	{
		Log.Warning(*Text.ToString());
	}
	CNA_VERBOSE(TEXT("Compiler: %s"), *Text.ToString());
}

void FCNAMessages::ReportToLog(ECNAMessage Code, const FFormatNamedArguments& Arguments, const UObject* Context)
{
	ReportTextToLog(Code, Format(Code, Arguments), Context);
}

void FCNAMessages::ReportTextToLog(ECNAMessage Code, const FText& Text, const UObject* Context)
{
	ReportTextToLogWithSeverity(GetSeverity(Code), Text, Context);
}

void FCNAMessages::ReportTextToLogWithSeverity(EMessageSeverity::Type Severity, const FText& Text, const UObject* Context)
{
	FMessageLog(LogName).AddMessage(CNAMessagesLocal::MakeMessage(Severity, Text, Context));
	CNAMessagesLocal::WriteToOutputLog(Severity, Text, Context);
}

void FCNAMessages::LogInfo(const FText& Text, const UObject* Context)
{
	FMessageLog(LogName).AddMessage(CNAMessagesLocal::MakeMessage(EMessageSeverity::Info, Text, Context));
	CNAMessagesLocal::WriteToOutputLog(EMessageSeverity::Info, Text, Context);
}

void FCNAMessages::OpenLog(EMessageSeverity::Type MinSeverity)
{
	FMessageLog(LogName).Open(MinSeverity, false);
}

void FCNAMessages::NewPage(const FText& Title)
{
	FMessageLog(LogName).NewPage(Title);
}

void FCNAMessages::RegisterLogListing()
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
	if (MessageLogModule.IsRegisteredLogListing(LogName))
	{
		return;
	}
	FMessageLogInitializationOptions Options;
	Options.bShowFilters = true;
	Options.bShowPages = true;
	Options.bAllowClear = true;
	Options.MaxPageCount = 10;
	MessageLogModule.RegisterLogListing(LogName, LOCTEXT("MessageLogLabel", "NodeFactory"), Options);
}

void FCNAMessages::UnregisterLogListing()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("MessageLog")))
	{
		FMessageLogModule& MessageLogModule = FModuleManager::GetModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
		if (MessageLogModule.IsRegisteredLogListing(LogName))
		{
			MessageLogModule.UnregisterLogListing(LogName);
		}
	}
}

bool FCNAMessages_IsVerbose()
{
	const UCNAEditorSettings* Settings = GetDefault<UCNAEditorSettings>();
	return Settings && Settings->bVerboseLog;
}

#undef LOCTEXT_NAMESPACE
