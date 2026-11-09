// Copyright (c) NodeFactory. Checks of custom nodes at compile time (spec 5.1, item 1) and of assets (spec 10.2).

#pragma once

#include "CoreMinimal.h"
#include "KismetCompiler.h"
#include "CNAMessages.h"

class UBlueprint;
class UCustomNodeAsset;
class UEdGraph;
class UK2Node_CustomNode;
struct FCNAPinOverride;
struct FCNASignaturePin;

/**
 * Compile of a Custom Node Asset runs the asset checks through its own compiler context (spec 7.3, item 5).
 * It needs FKismetCompilerContext::RegisterCompilerForBP and the virtual CreateFunctionList of 4.20.3 (V-check).
 * Set to 0 if they differ: Compile then skips the checks, the Validate command still runs them.
 */
#ifndef CNA_WITH_ASSET_COMPILER
#define CNA_WITH_ASSET_COMPILER 1
#endif

/**
 * All checks of the plugin. The node checks run in ValidateNodeDuringCompilation and before the expansion
 * (spec 5.1, item 1); the asset checks run for Validate, IsDataValid and Compile of the asset (spec 10.2).
 * Every message carries a code CNA001..CNA023 (spec 10.1).
 */
struct NODEFACTORYEDITOR_API FCNAValidator
{
	// Node

	/**
	 * Checks one node of a Blueprint that is being compiled: CNA005, CNA006, CNA009, CNA001, CNA002, CNA003,
	 * CNA004, CNA018, CNA007, CNA008, CNA010, CNA012, CNA013, CNA015, CNA017, CNA020-CNA023. An error stops the
	 * expansion.
	 */
	static void ValidateNodeForCompilation(const UK2Node_CustomNode* Node, FCNADiagnosticList& Out);

	// Asset

	/** Every check of spec 10.2. Returns true when no error was found (warnings are allowed). */
	static bool ValidateAsset(const UCustomNodeAsset* Asset, FCNADiagnosticList& Out);

	/** Validate command: a new page in the Message Log "NodeFactory" with the result of one asset. */
	static bool ValidateAssetToLog(const UCustomNodeAsset* Asset, bool bOpenLog);

	/** Validate for several assets on one log page. Returns the number of assets with errors. */
	static int32 ValidateAssetsToLog(const TArray<UCustomNodeAsset*>& Assets, bool bOpenLog);

	/** Loads every Custom Node Asset of the project and validates it (Node Catalog, Validate All). */
	static int32 ValidateAllAssetsToLog(bool bOpenLog);

	// Partial checks (Pin Manager rows, Details, import, tests)

	/** One entry of PinOverrides against the signature of the macro: CNA014, CNA013, CNA015 and warnings. */
	static void ValidatePinOverride(const UCustomNodeAsset* Asset, const TArray<FCNASignaturePin>& Signature, const FCNAPinOverride& Override, FCNADiagnosticList& Out);

	/** First problem of the override of PinName as one line for a tooltip; false when there is none. */
	static bool GetPinOverrideProblem(const UCustomNodeAsset* Asset, FName PinName, FText& OutProblem);

	/** LocalVariables: names, types (CNA010) and the Get Local and Set Local nodes (CNA017). */
	static void ValidateLocalVariables(const UCustomNodeAsset* Asset, FCNADiagnosticList& Out);

	/** Icon brush and IconTexture (CNA019), bCompact only for pure nodes, body widgets. */
	static void ValidateAppearance(const UCustomNodeAsset* Asset, FCNADiagnosticList& Out);

	/** ContextClass must be a usable, current, non-interface class (spec 10.2). */
	static void ValidateContextClass(const UCustomNodeAsset* Asset, FCNADiagnosticList& Out);

	/** Cycles (CNA001) and depth (CNA002) of the nested nodes; Stack holds the assets expanded around. */
	static bool CheckNesting(const UCustomNodeAsset* Asset, const TArray<const UCustomNodeAsset*>& Stack, const UObject* Context, FCNADiagnosticList& Out);

	/** Pure rules: no execution nodes (CNA004) and no local variables (CNA018). */
	static void CheckPurity(const UCustomNodeAsset* Asset, const UObject* Context, FCNADiagnosticList& Out);

	/** PassThroughOf of every override (CNA015). */
	static void CheckPassThrough(const UCustomNodeAsset* Asset, const TArray<FCNASignaturePin>& Signature, const UObject* Context, FCNADiagnosticList& Out);

	/** Get Local and Set Local with a variable that does not exist (CNA017). bOnlyWired skips unconnected nodes. */
	static void CheckLocalVariableNodes(const UCustomNodeAsset* Asset, bool bOnlyWired, const UObject* Context, FCNADiagnosticList& Out);

	/** Shared count of the dynamic groups: the ranges must overlap (CNA013). */
	static void CheckDynamicRanges(const UCustomNodeAsset* Asset, FCNADiagnosticList& Out);

	/**
	 * Curve widgets: Playback on a pure node (CNA020), more than one Playback curve (CNA021), Curve asset missing or
	 * of the wrong class (CNA022). bIncludeWarnings adds the warnings of the asset checks: empty own curve, curve
	 * names, latent nodes next to a playback loop.
	 */
	static void CheckCurves(const UCustomNodeAsset* Asset, const UObject* Context, bool bIncludeWarnings, FCNADiagnosticList& Out);

	/** Curve Value nodes whose curve is missing or whose value type is outdated (CNA023). bOnlyWired skips unconnected nodes. */
	static void CheckCurveValueNodes(const UCustomNodeAsset* Asset, bool bOnlyWired, const UObject* Context, FCNADiagnosticList& Out);

	// Registration (NodeFactoryEditorModule)

	/** Binds FCNAEditorHooks::ValidateAsset and the compiler of the asset. */
	static void Register();
	static void Unregister();

private:
	/** FCNAEditorHooks::ValidateAsset: IsDataValid of the asset (spec 10.2). */
	static bool HookValidateAsset(UCustomNodeAsset* Asset, TArray<FText>& OutErrors);

	static void CheckSignatureTypes(const UCustomNodeAsset* Asset, const TArray<FCNASignaturePin>& Signature, const UObject* Context, FCNADiagnosticList& Out);
	static void CheckPassThroughOverride(const UCustomNodeAsset* Asset, const TArray<FCNASignaturePin>& Signature, const FCNAPinOverride& Override, const UObject* Context, FCNADiagnosticList& Out);
	static void CheckPinRedirects(const UCustomNodeAsset* Asset, const TArray<FCNASignaturePin>& Signature, FCNADiagnosticList& Out);
	static void CheckRequiredPins(const UK2Node_CustomNode* Node, const UCustomNodeAsset* Asset, FCNADiagnosticList& Out);
	static void CheckContextOfBlueprint(const UK2Node_CustomNode* Node, const UCustomNodeAsset* Asset, FCNADiagnosticList& Out);
	static void CheckDynamicCount(const UK2Node_CustomNode* Node, const UCustomNodeAsset* Asset, FCNADiagnosticList& Out);
};

#if CNA_WITH_ASSET_COMPILER
/**
 * Compiler of a Custom Node Asset: the normal Blueprint compiler (the Behavior Graph is compiled as usual) plus the
 * asset checks. The lines go to Compiler Results and, when the asset window is open, to the Message Log
 * "NodeFactory" (spec 7.3, item 5).
 */
class NODEFACTORYEDITOR_API FCNAAssetCompilerContext : public FKismetCompilerContext
{
public:
	FCNAAssetCompilerContext(UBlueprint* SourceSketch, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompilerOptions);

	/** Factory for FKismetCompilerContext::RegisterCompilerForBP (V-check). */
	static TSharedPtr<FKismetCompilerContext> MakeCompilerContext(UBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions);

protected:
	// FKismetCompilerContext
	virtual void CreateFunctionList() override;

private:
	/** The checks are reported once per compile. */
	bool bAssetChecksReported;
};
#endif
