// Copyright (c) NodeFactory. Data types and enumerations of a Custom Node Asset (spec 3, 4.2, 4.7, 4.8).

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "EdGraph/EdGraphPin.h"
#include "CNATypes.generated.h"

/** Icon in the corner of the node (spec 3.1, CornerIcon). Latent is forced when the subgraph is latent. */
UENUM(BlueprintType)
enum class ECNACornerIcon : uint8
{
	None,
	/** Clock, like Delay. */
	Latent,
	/** Authority only. */
	Server,
	Client,
	Multicast,
	/** Interface message envelope. */
	Message
};

/** Restriction of the wires a pin accepts (spec 4.2, ConnectionRule; V7). */
UENUM(BlueprintType)
enum class ECNAConnectionRule : uint8
{
	/** The K2 schema decides alone. */
	None,
	/** Only wires from pins of exactly the same type, container and sub type. */
	SameTypeOnly,
	/** Only object or class pins whose class is a child of ClassFilter. */
	ClassFilter
};

/** Corner of a text badge (spec 3.2, Badges). */
UENUM(BlueprintType)
enum class ECNABadgeCorner : uint8
{
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight
};

/** Widget drawn on the body of the node instead of the value field of a pin (spec 3.2, BodyWidgets). */
UENUM(BlueprintType)
enum class ECNABodyWidgetType : uint8
{
	CheckBox,
	ComboBox,
	TextBox,
	ScrollList,
	Slider
};

/**
 * Graphs where the node can be placed (spec 3.1, AllowedGraphs). Used as bit indices: the flag of a kind is
 * 1 << static_cast<int32>(Kind).
 */
UENUM(BlueprintType, meta = (Bitflags))
enum class ECNAGraphKind : uint8
{
	Function,
	EventGraph,
	Macro,
	ConstructionScript
};

/**
 * Override of one pin of the node (spec 4.2 and 4.7). The pin itself comes from the signature of the macro Execute;
 * the override finds its pin by name. An override without a matching pin produces CNA014.
 */
USTRUCT(BlueprintType)
struct NODEFACTORY_API FCNAPinOverride
{
	GENERATED_BODY()

	/** Key: name of the pin of the macro Execute. For a dynamic group the name of the array input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	FName PinName;

	/** Display name of the pin (PinFriendlyName). Empty: the pin name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	FText DisplayName;

	/** Tooltip of the pin (PinToolTip). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin", meta = (MultiLine = true))
	FText Tooltip;

	/** The pin goes to the Advanced section behind the arrow of the node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	bool bAdvanced = false;

	/** The pin is hidden when the node is created; the Behavior can show it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	bool bHiddenByDefault = false;

	/** Without a wire and without a value the compilation fails with CNA008. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	bool bRequired = false;

	/** Object input: when no wire is connected the expansion connects Self (spec 4.6). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	bool bDefaultToSelf = false;

	/** Output only: returns the value of the input with this name without computation (CNA015). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pin")
	FName PassThroughOf;

	/** Array input only: the node shows a set of element pins instead of the array (spec 4.4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Pins")
	bool bDynamicGroup = false;

	/** Lower bound and initial number of element pins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Pins", meta = (EditCondition = "bDynamicGroup", ClampMin = "0", ClampMax = "64", UIMin = "0", UIMax = "64"))
	int32 DynamicMin = 1;

	/** Upper bound of element pins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Pins", meta = (EditCondition = "bDynamicGroup", ClampMin = "1", ClampMax = "64", UIMin = "1", UIMax = "64"))
	int32 DynamicMax = 16;

	/** Display name of an element pin; {0} is the index starting at 0, {1} the index starting at 1. Empty: "Name 0". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic Pins", meta = (EditCondition = "bDynamicGroup"))
	FText DynamicNameFormat;

	/** Restriction of the wires through IsConnectionDisallowed (V7). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	ECNAConnectionRule ConnectionRule = ECNAConnectionRule::None;

	/** Class for ECNAConnectionRule::ClassFilter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	TSubclassOf<UObject> ClassFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (InlineEditConditionToggle))
	bool bOverridePinColor = false;

	/** Any color of the pin instead of the color of its type; also colors the wire when bColorWires is set (spec 4.7). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (EditCondition = "bOverridePinColor"))
	FLinearColor PinColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (InlineEditConditionToggle))
	bool bOverrideLabelColor = false;

	/** Color of the label of the pin. Without a value the PinLabelColor of the asset is used (spec 4.7). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (EditCondition = "bOverrideLabelColor"))
	FLinearColor LabelColor = FLinearColor::White;

	FCNAPinOverride() {}
	explicit FCNAPinOverride(FName InPinName) : PinName(InPinName) {}

	bool IsDynamic() const { return bDynamicGroup; }

	/** DynamicMin and DynamicMax sorted and limited to 0..64. */
	void GetDynamicRange(int32& OutMin, int32& OutMax) const;

	/** Count limited to [DynamicMin, DynamicMax] (spec 4.4, item 4). */
	int32 ClampDynamicCount(int32 Count) const;

	/** Display name of the element pin Index, built from DynamicNameFormat or from the display name of the group. */
	FText FormatDynamicName(int32 Index) const;

	/** The override changes nothing and could be removed (used by the Pin Manager to clean up). */
	bool IsEmpty() const;
};

/** Old pin name -> new pin name, keeps wires when a pin of the macro is renamed (spec 3.1, PinRedirects; V11). */
USTRUCT(BlueprintType)
struct NODEFACTORY_API FCNAPinRedirect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Redirect")
	FName OldName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Redirect")
	FName NewName;

	FCNAPinRedirect() {}
	FCNAPinRedirect(FName InOldName, FName InNewName) : OldName(InOldName), NewName(InNewName) {}
};

/**
 * Local variable of the subgraph (spec 4.8): a temporary cell that exists while one node instance executes.
 * Get Local and Set Local reference the variable by Guid, so renaming keeps the nodes connected.
 */
USTRUCT(BlueprintType)
struct NODEFACTORY_API FCNALocalVariable
{
	GENERATED_BODY()

	/** Stable identity. Generated once; duplicates are regenerated in PostLoad. */
	UPROPERTY(VisibleAnywhere, Category = "Variable", AdvancedDisplay)
	FGuid Guid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	FName Name;

	/** Any pin type except Exec and Wildcard. */
	UPROPERTY(EditAnywhere, Category = "Variable")
	FEdGraphPinType Type;

	/** Value assigned on every entry through an exec input, in the text form of pin default values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	FString DefaultValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable", meta = (MultiLine = true))
	FText Tooltip;

	FCNALocalVariable();
	FCNALocalVariable(FName InName, const FEdGraphPinType& InType);

	/** Exec, Wildcard and delegates cannot be stored in a local variable (spec 4.8, item 1). */
	static bool IsTypeAllowed(const FEdGraphPinType& InType);

	bool IsValid() const { return Guid.IsValid() && !Name.IsNone(); }
};

/** Text badge in a corner of the node, for example PURE or ASYNC (spec 3.2, Badges). */
USTRUCT(BlueprintType)
struct NODEFACTORY_API FCNABadge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Badge")
	FText Text;

	/** Background of the badge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Badge")
	FLinearColor Color = FLinearColor(0.0f, 0.48f, 0.44f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Badge")
	FLinearColor TextColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Badge")
	ECNABadgeCorner Corner = ECNABadgeCorner::TopRight;
};

/**
 * Widget on the body of the node that edits the default value of an input pin (spec 3.2, BodyWidgets).
 * The value is stored as the default value of the pin and raises OnPinValueChanged.
 */
USTRUCT(BlueprintType)
struct NODEFACTORY_API FCNABodyWidget
{
	GENERATED_BODY()

	/** Input pin whose default value the widget edits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Widget")
	FName PinName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Widget")
	ECNABodyWidgetType Type = ECNABodyWidgetType::CheckBox;

	/** Entries of a ComboBox. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Widget")
	TArray<FString> Options;

	/** Text left of the widget. Empty: the display name of the pin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Widget")
	FText Label;

	/** Width of the widget in slate units, 0 for automatic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Widget", meta = (ClampMin = "0", ClampMax = "600"))
	float Width = 0.0f;

	/** Maximum height of a ScrollList, 0 uses 120 units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Widget", meta = (ClampMin = "0", ClampMax = "600"))
	float Height = 0.0f;
	/** Slider bounds; maximum must be greater than minimum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Widget", meta = (ClampMin = "-1000000", ClampMax = "1000000"))
	float SliderMin = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body Widget", meta = (ClampMin = "-1000000", ClampMax = "1000000"))
	float SliderMax = 1.0f;
};

/** Helpers shared by both modules. */
namespace CNATypes
{
	/** Separator of the name of a dynamic element pin: "Items__0", "Items__1" (spec 4.4). */
	NODEFACTORY_API extern const TCHAR* DynamicPinSeparator;

	/** "Items" + 2 -> "Items__2". */
	NODEFACTORY_API FName MakeDynamicPinName(FName BaseName, int32 Index);

	/** "Items__2" -> "Items", 2. Returns false for names that are not element pins. */
	NODEFACTORY_API bool ParseDynamicPinName(FName PinName, FName& OutBaseName, int32& OutIndex);

	/** Flag of one graph kind inside the AllowedGraphs mask. */
	NODEFACTORY_API int32 GetGraphKindFlag(ECNAGraphKind Kind);

	/** All four graph kinds. */
	NODEFACTORY_API int32 AllGraphKindsMask();

	NODEFACTORY_API bool IsGraphKindAllowed(int32 Mask, ECNAGraphKind Kind);

	/** Brush of the corner icon in EditorStyle (V20); NAME_None for ECNACornerIcon::None. */
	NODEFACTORY_API FName GetCornerIconBrushName(ECNACornerIcon Icon);

	NODEFACTORY_API FText GetCornerIconDisplayName(ECNACornerIcon Icon);

	NODEFACTORY_API FText GetGraphKindDisplayName(ECNAGraphKind Kind);

	/** "Function, Event Graph" for a mask. */
	NODEFACTORY_API FText DescribeGraphKinds(int32 Mask);

	/** Accent color of the plugin (asset color in the Content Browser, Behavior graph wires): RGB 0, 184, 169. */
	NODEFACTORY_API FLinearColor GetAccentColor();
}
