// Copyright (c) NodeFactory. Message codes CNA001-CNA019, their texts and the Message Log "NodeFactory" (spec 10.1).

#pragma once

#include "CoreMinimal.h"
#include "Logging/TokenizedMessage.h"
#include "UObject/WeakObjectPtr.h"

class FCompilerResultsLog;
class UEdGraphNode;
class UObject;

/** Message codes of spec 10.1. The numeric value is the number of the code. */
enum class ECNAMessage : uint8
{
	CNA001 = 1,	// Error: cycle of nested nodes
	CNA002,		// Error: expansion depth exceeded
	CNA003,		// Error: latent logic in a graph that forbids it
	CNA004,		// Error: pure node contains execution nodes
	CNA005,		// Error: asset, macro Execute or Tunnel nodes missing
	CNA006,		// Warning: node outdated (NodeVersion != AssetVersion)
	CNA007,		// Error: Wildcard type not resolved
	CNA008,		// Error: required pin without wire and value
	CNA009,		// Warning: node deprecated
	CNA010,		// Error: class, struct or enum of a pin type not found
	CNA011,		// Warning: Behavior error, refused action or uncompiled asset class
	CNA012,		// Error: Blueprint does not derive from ContextClass
	CNA013,		// Error: number of dynamic pins out of range
	CNA014,		// Warning: pin override without pin
	CNA015,		// Error: PassThroughOf target missing or incompatible
	CNA016,		// Warning: wire lost during an update
	CNA017,		// Error: Get Local or Set Local references a missing variable
	CNA018,		// Error: local variables in a pure node
	CNA019,		// Warning: icon texture missing, default icon used
	Count
};

/** One diagnostic: code, final text and the node or asset it refers to. */
struct NODEFACTORYEDITOR_API FCNADiagnostic
{
	ECNAMessage Code = ECNAMessage::CNA005;
	/** Full text including the "CNAxxx: " prefix. */
	FText Text;
	TWeakObjectPtr<const UObject> Context;

	/** Checks without a code of their own reuse the nearest code as a warning (for example bCompact, spec 10.2). */
	bool bDowngradedToWarning = false;

	bool IsError() const;
	EMessageSeverity::Type GetSeverity() const;
};

/** Diagnostics collected by the validator before they go to Compiler Results or the Message Log. */
struct NODEFACTORYEDITOR_API FCNADiagnosticList
{
	TArray<FCNADiagnostic> Items;

	/** Formats the template of the code with the arguments and stores it. */
	void Add(ECNAMessage Code, const FFormatNamedArguments& Arguments, const UObject* Context);

	/** Stores a text of its own under a code ("CNAxxx: " is prepended). bAsWarning lowers an error code. */
	void AddText(ECNAMessage Code, const FText& Details, const UObject* Context, bool bAsWarning = false);

	bool HasErrors() const;
	bool Contains(ECNAMessage Code) const;
	int32 Num() const { return Items.Num(); }
	int32 NumErrors() const;
	int32 NumWarnings() const;
	void Append(const FCNADiagnosticList& Other) { Items.Append(Other.Items); }
	void Reset() { Items.Reset(); }

	/** Writes every item into the Message Log "NodeFactory" with a link to its context. */
	void FlushToMessageLog() const;

	/** Writes every item into Compiler Results with a link to the node (spec 5.4). */
	void FlushToCompilerLog(FCompilerResultsLog& Log, const UEdGraphNode* Node) const;

	/** Texts of all items, for IsDataValid and tests. */
	void GetTexts(TArray<FText>& OutTexts) const;
};

/** Texts, severities and output of the message codes. */
struct NODEFACTORYEDITOR_API FCNAMessages
{
	/** Name of the Message Log listing (spec 2.5). */
	static const FName LogName;

	/** "CNA001". */
	static FString GetCode(ECNAMessage Code);

	/** Code from "CNA001"; ECNAMessage::Count when the text is not a code. */
	static ECNAMessage ParseCode(const FString& Text);

	static EMessageSeverity::Type GetSeverity(ECNAMessage Code);
	static bool IsError(ECNAMessage Code);

	/** Localized template of the code, key Msg.CNAxxx in the namespace NodeFactory (spec 10.1). */
	static FText GetTemplate(ECNAMessage Code);

	/** "CNAxxx: " + formatted template. */
	static FText Format(ECNAMessage Code, const FFormatNamedArguments& Arguments);

	/** {Asset} argument with the effective display name and the asset name. */
	static FFormatNamedArguments AssetArgs(const UObject* Asset);

	/** Compiler Results entry with a link to the node; "@@" is replaced by the node by the compiler log. */
	static void ReportToCompiler(FCompilerResultsLog& Log, const UEdGraphNode* Node, ECNAMessage Code, const FFormatNamedArguments& Arguments);
	static void ReportTextToCompiler(FCompilerResultsLog& Log, const UEdGraphNode* Node, ECNAMessage Code, const FText& Text);

	/** Message Log "NodeFactory" entry with a link to the context object and a copy in LogNodeFactory. */
	static void ReportToLog(ECNAMessage Code, const FFormatNamedArguments& Arguments, const UObject* Context);
	static void ReportTextToLog(ECNAMessage Code, const FText& Text, const UObject* Context);

	/** Same as ReportTextToLog / ReportTextToCompiler with an explicit severity (downgraded diagnostics). */
	static void ReportTextToLogWithSeverity(EMessageSeverity::Type Severity, const FText& Text, const UObject* Context);
	static void ReportTextToCompilerWithSeverity(FCompilerResultsLog& Log, const UEdGraphNode* Node, bool bError, const FText& Text);

	/** Informational line in the Message Log (update reports, import summaries). */
	static void LogInfo(const FText& Text, const UObject* Context = nullptr);

	/** Opens the Message Log when it holds messages of at least the given severity. */
	static void OpenLog(EMessageSeverity::Type MinSeverity = EMessageSeverity::Warning);

	/** Starts a new page in the Message Log, for example one page per validation run. */
	static void NewPage(const FText& Title);

	/** Registered by the editor module on startup, removed on shutdown (spec 9.5). */
	static void RegisterLogListing();
	static void UnregisterLogListing();
};

/** Verbose output of LogNodeFactory is enabled in the editor preferences (spec 9.7, bVerboseLog). */
NODEFACTORYEDITOR_API bool FCNAMessages_IsVerbose();

/** Log line that is written only when bVerboseLog is set. */
#define CNA_VERBOSE(Format, ...) \
	do \
	{ \
		if (FCNAMessages_IsVerbose()) \
		{ \
			UE_LOG(LogNodeFactory, Log, Format, ##__VA_ARGS__); \
		} \
	} while (0)
