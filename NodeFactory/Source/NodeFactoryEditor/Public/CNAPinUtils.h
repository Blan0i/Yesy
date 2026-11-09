// Copyright (c) NodeFactory. Pin type helpers shared by the node, the expander, the widgets and JSON (spec 4).

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class UEdGraphNode;
class UCustomNodeAsset;
struct FCNAPinOverride;
enum class ECNABodyWidgetType : uint8;

/**
 * One pin of the signature of a Custom Node Asset, read from the Tunnel nodes of the macro Execute (spec 4.1).
 * Direction is the direction on the custom node: outputs of Tunnel Input become inputs of the node.
 */
struct NODEFACTORYEDITOR_API FCNASignaturePin
{
	FName Name;
	FEdGraphPinType Type;
	EEdGraphPinDirection Direction = EGPD_Input;
	FString DefaultValue;
	TWeakObjectPtr<UObject> DefaultObject;
	FText DefaultText;

	/** Position among the pins of the same direction, used for positional matching (spec 9.1, item 5). */
	int32 Index = 0;

	bool IsExec() const;
	bool IsWildcard() const;
	bool IsInput() const { return Direction == EGPD_Input; }
};

struct NODEFACTORYEDITOR_API FCNAPinUtils
{
	// Types

	static bool IsExecType(const FEdGraphPinType& Type);
	static bool IsWildcardType(const FEdGraphPinType& Type);
	static bool IsExecPin(const UEdGraphPin* Pin);

	/** Object, class, interface and soft references (spec 4.3). */
	static bool IsReferenceCategory(FName Category);

	/** Element type of a container: Array of Float -> Float. Single types are returned unchanged. */
	static FEdGraphPinType GetElementType(const FEdGraphPinType& Type);

	/** Same element type with the given container. */
	static FEdGraphPinType MakeContainerType(const FEdGraphPinType& ElementType, EPinContainerType Container);

	/** Category, sub category, object, container and value type are equal (ECNAConnectionRule::SameTypeOnly). */
	static bool AreTypesIdentical(const FEdGraphPinType& A, const FEdGraphPinType& B);

	/** The K2 schema accepts a wire from Output to Input. */
	static bool AreTypesCompatible(const FEdGraphPinType& Output, const FEdGraphPinType& Input);

	/** Replaces the Wildcard part of a type with the resolved type and keeps the container (spec 4.5, V14). */
	static void ApplyResolvedWildcard(FEdGraphPinType& InOutType, const FEdGraphPinType& Resolved);

	/** "Float", "Array of Actor Object Reference": the text of the type selector. */
	static FText GetTypeText(const FEdGraphPinType& Type);

	/** Stable machine string of a type, used for hashes and diffs. */
	static FString GetTypeSignatureString(const FEdGraphPinType& Type);

	/** Color of the type in the graph editor settings. */
	static FLinearColor GetTypeColor(const FEdGraphPinType& Type);

	/** Struct, enum or class of the type is missing although the category needs one (CNA010). */
	static bool IsTypeObjectMissing(const FEdGraphPinType& Type);

	/** Allowed types of local variables: anything except Exec and Wildcard (spec 4.8, item 1). */
	static bool CanBeLocalVariableType(const FEdGraphPinType& Type);

	// Values

	/** The pin has no wire-independent value (CNA008). Containers never have a literal value. */
	static bool IsValueEmpty(const UEdGraphPin* Pin);

	static void CopyDefaultValue(const UEdGraphPin* From, UEdGraphPin* To);
	static void ApplySignatureDefault(const FCNASignaturePin& From, UEdGraphPin* To);

	/** Value of the pin as text for the Behavior event OnPinValueChanged. */
	static FString GetValueAsString(const UEdGraphPin* Pin);

	// Pins

	/** Name of the group of a dynamic element pin ("Items__2" -> "Items"); other names unchanged. */
	static FName GetBaseName(FName PinName);

	/** Index of a dynamic element pin or INDEX_NONE. */
	static int32 GetDynamicIndex(FName PinName);

	/** Top level pin (no parent pin) by name and optional direction. */
	static UEdGraphPin* FindTopLevelPin(const UEdGraphNode* Node, FName PinName, EEdGraphPinDirection Direction = EGPD_MAX);

	/** The pin or one of its split sub pins has a wire. */
	static bool IsPinOrSubPinLinked(const UEdGraphPin* Pin);

	/** Display text of a pin, with the friendly name when one is set. */
	static FText GetPinDisplayText(const UEdGraphPin* Pin);

	/** Override that applies to a pin of a custom node: dynamic element pins use the override of their group. */
	static const FCNAPinOverride* FindOverrideForPin(const UCustomNodeAsset* Asset, FName PinName);

	/** Removes every wire of the pin and of its sub pins, notifying the linked nodes. */
	static void BreakAllLinksRecursive(UEdGraphPin* Pin);

	/**
	 * Types a body widget can edit (spec 3.2): CheckBox - Boolean; ComboBox - String, Name, Text or an enumeration;
	 * TextBox - String, Name, Text, Integer, Float. The validator and SGraphNode_NodeFactory use the same rule.
	 */
	static bool CanBodyWidgetEditType(ECNABodyWidgetType WidgetType, const FEdGraphPinType& Type);

	/** Enumeration pin: Byte with a UEnum. */
	static bool IsEnumType(const FEdGraphPinType& Type);
};
