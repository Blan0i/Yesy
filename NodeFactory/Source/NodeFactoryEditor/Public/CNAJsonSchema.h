// Copyright (c) NodeFactory. JSON format of Custom Node Assets: field names, versions, migrations, value
// conversions, path checks and the deterministic writer (spec 8.1, 8.2).

#pragma once

#include "CoreMinimal.h"
#include "CNATypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphPin.h"

/**
 * Names of the JSON fields (spec 8.1). The exporter writes them in the order of this list; arrays keep the order
 * of the asset. There are no time stamps, so a diff in source control shows only real changes.
 */
namespace CNAJsonFields
{
	// Root
	static const TCHAR* const SchemaVersion = TEXT("schemaVersion");
	static const TCHAR* const EngineVersion = TEXT("engineVersion");
	static const TCHAR* const PluginVersion = TEXT("pluginVersion");
	static const TCHAR* const Id = TEXT("id");
	static const TCHAR* const Name = TEXT("name");
	static const TCHAR* const DisplayName = TEXT("displayName");
	static const TCHAR* const Subtitle = TEXT("subtitle");
	static const TCHAR* const Tooltip = TEXT("tooltip");
	static const TCHAR* const Category = TEXT("category");
	static const TCHAR* const Keywords = TEXT("keywords");
	static const TCHAR* const DefaultComment = TEXT("defaultComment");
	static const TCHAR* const Appearance = TEXT("appearance");
	static const TCHAR* const Flags = TEXT("flags");
	static const TCHAR* const PinOverrides = TEXT("pinOverrides");
	static const TCHAR* const PinRedirects = TEXT("pinRedirects");
	static const TCHAR* const LocalVariables = TEXT("localVariables");
	static const TCHAR* const Graph = TEXT("graph");
	static const TCHAR* const Behavior = TEXT("behavior");
	static const TCHAR* const Textures = TEXT("textures");
	static const TCHAR* const Meta = TEXT("meta");

	// FText object
	static const TCHAR* const Text = TEXT("text");
	static const TCHAR* const Namespace = TEXT("namespace");
	static const TCHAR* const Key = TEXT("key");

	// appearance
	static const TCHAR* const UseHeaderColor = TEXT("useHeaderColor");
	static const TCHAR* const HeaderColor = TEXT("headerColor");
	static const TCHAR* const HeaderTextColor = TEXT("headerTextColor");
	static const TCHAR* const PinLabelColor = TEXT("pinLabelColor");
	static const TCHAR* const Icon = TEXT("icon");
	static const TCHAR* const IconTint = TEXT("iconTint");
	static const TCHAR* const IconTexture = TEXT("iconTexture");
	static const TCHAR* const IconTextureTint = TEXT("iconTextureTint");
	static const TCHAR* const CornerIcon = TEXT("cornerIcon");
	static const TCHAR* const CornerIconTexture = TEXT("cornerIconTexture");
	static const TCHAR* const CornerIconTextureTint = TEXT("cornerIconTextureTint");
	static const TCHAR* const CornerIconTextureSize = TEXT("cornerIconTextureSize");
	static const TCHAR* const Badges = TEXT("badges");
	static const TCHAR* const MinWidth = TEXT("minWidth");
	static const TCHAR* const MinHeight = TEXT("minHeight");
	static const TCHAR* const BodyWidgets = TEXT("bodyWidgets");
	static const TCHAR* const ColorWires = TEXT("colorWires");
	static const TCHAR* const Compact = TEXT("compact");
	static const TCHAR* const CompactTitle = TEXT("compactTitle");

	// badge, body widget
	static const TCHAR* const Color = TEXT("color");
	static const TCHAR* const TextColor = TEXT("textColor");
	static const TCHAR* const Corner = TEXT("corner");
	static const TCHAR* const PinName = TEXT("pinName");
	static const TCHAR* const Type = TEXT("type");
	static const TCHAR* const Options = TEXT("options");
	static const TCHAR* const Label = TEXT("label");
	static const TCHAR* const Width = TEXT("width");
	static const TCHAR* const Height = TEXT("height");
	static const TCHAR* const SliderMin = TEXT("sliderMin");
	static const TCHAR* const SliderMax = TEXT("sliderMax");

	// flags
	static const TCHAR* const Pure = TEXT("pure");
	static const TCHAR* const DevelopmentOnly = TEXT("developmentOnly");
	static const TCHAR* const Deprecated = TEXT("deprecated");
	static const TCHAR* const DeprecationMessage = TEXT("deprecationMessage");
	static const TCHAR* const AllowedGraphs = TEXT("allowedGraphs");
	static const TCHAR* const ContextClass = TEXT("contextClass");
	static const TCHAR* const HiddenInMenu = TEXT("hiddenInMenu");
	static const TCHAR* const AllowInstanceLabel = TEXT("allowInstanceLabel");

	// pin override
	static const TCHAR* const Advanced = TEXT("advanced");
	static const TCHAR* const HiddenByDefault = TEXT("hiddenByDefault");
	static const TCHAR* const Required = TEXT("required");
	static const TCHAR* const DefaultToSelf = TEXT("defaultToSelf");
	static const TCHAR* const PassThroughOf = TEXT("passThroughOf");
	static const TCHAR* const DynamicGroup = TEXT("dynamicGroup");
	static const TCHAR* const DynamicMin = TEXT("dynamicMin");
	static const TCHAR* const DynamicMax = TEXT("dynamicMax");
	static const TCHAR* const DynamicNameFormat = TEXT("dynamicNameFormat");
	static const TCHAR* const ConnectionRule = TEXT("connectionRule");
	static const TCHAR* const ClassFilter = TEXT("classFilter");
	static const TCHAR* const OverridePinColor = TEXT("overridePinColor");
	static const TCHAR* const PinColor = TEXT("pinColor");
	static const TCHAR* const OverrideLabelColor = TEXT("overrideLabelColor");
	static const TCHAR* const LabelColor = TEXT("labelColor");

	// redirect
	static const TCHAR* const OldName = TEXT("oldName");
	static const TCHAR* const NewName = TEXT("newName");

	// local variable, signature pin
	static const TCHAR* const Guid = TEXT("guid");
	static const TCHAR* const DefaultValue = TEXT("defaultValue");
	static const TCHAR* const Direction = TEXT("direction");

	// pin type
	static const TCHAR* const PinCategory = TEXT("category");
	static const TCHAR* const PinSubCategory = TEXT("subCategory");
	static const TCHAR* const PinObject = TEXT("object");
	static const TCHAR* const Container = TEXT("container");
	static const TCHAR* const IsReference = TEXT("isReference");
	static const TCHAR* const IsConst = TEXT("isConst");
	static const TCHAR* const IsWeak = TEXT("isWeak");
	static const TCHAR* const ValueType = TEXT("valueType");

	// graph, behavior
	static const TCHAR* const Signature = TEXT("signature");
	static const TCHAR* const T3D = TEXT("t3d");

	// meta
	static const TCHAR* const Author = TEXT("author");
	static const TCHAR* const Description = TEXT("description");
	static const TCHAR* const Tags = TEXT("tags");
	static const TCHAR* const SourceUrl = TEXT("sourceUrl");
}

/**
 * Helpers of the JSON format. The format is versioned by schemaVersion:
 *  1 - first format: texts are plain strings, allowedGraphs is a bit mask, t3d is one string;
 *  2 - texts are objects {text, namespace, key} (spec 9.3), allowedGraphs lists the graph kinds by name and t3d is
 *      an array of lines, so a diff shows changed nodes line by line.
 * Older files are migrated in memory before they are read; newer files are refused (spec 8.2, item 2).
 */
struct NODEFACTORYEDITOR_API FCNAJsonSchema
{
	enum
	{
		/** Version written by the exporter. */
		CurrentVersion = 2,

		/** Oldest version the importer migrates. */
		OldestSupportedVersion = 1
	};

	// Reading and writing

	/** Parses a JSON document; the root must be an object. */
	static bool Parse(const FString& Text, TSharedPtr<FJsonObject>& OutRoot, FText& OutError);

	/**
	 * Writes a document as UTF-8 friendly text: 2 spaces of indentation, LF line ends, fields in insertion order,
	 * short numeric arrays (colors) on one line, a final LF (spec 8.1).
	 */
	static FString Write(const TSharedRef<FJsonObject>& Root);

	/** Shortest decimal text that reads back as the same float: 0.1 instead of 0.100000001. */
	static FString FormatNumber(double Value);

	// Versions and structure

	/** schemaVersion of a document; 0 when the field is missing or not a number. */
	static int32 GetSchemaVersion(const TSharedPtr<FJsonObject>& Root);

	/** Migrates an older document in place to CurrentVersion. Fails for a newer or unknown version. */
	static bool Migrate(const TSharedPtr<FJsonObject>& Root, TArray<FText>& OutNotes, FText& OutError);

	/** Presence and types of the fields after migration; every problem is added to OutErrors. */
	static bool ValidateStructure(const TSharedPtr<FJsonObject>& Root, TArray<FText>& OutErrors);

	// Paths (spec 8.2, item 3)

	/** Roots objects may be loaded from: /Script/, /Game/, /Engine/ and the content roots of enabled plugins. */
	static TArray<FString> GetAllowedRoots();

	/** The path is a well formed object path under an allowed root. */
	static bool IsAllowedObjectPath(const FString& Path);

	/** Absolute object paths inside a text (Class'/Script/...', "/Game/...", Class=/Script/...). */
	static void FindObjectPathsInText(const FString& Text, TArray<FString>& OutPaths);

	/** Classes of the objects created by a T3D text (the Class= of every Begin Object line). */
	static void FindObjectClassesInT3D(const FString& T3D, TArray<FString>& OutClassNames);

	/** Class named in a T3D text: full path (/Script/Module.Class) or short name. Never loads. */
	static UClass* FindClassByT3DName(const FString& ClassName);

	/**
	 * Object at an allowed path: found in memory or loaded from an allowed content root. Returns null for a path
	 * outside the allowed roots (bOutForbidden) or an object that does not exist.
	 */
	static UObject* ResolveObjectPath(const FString& Path, UClass* ExpectedClass, bool& bOutForbidden);

	// Values

	/** FText as {text, namespace, key} (spec 8.1, 9.3). */
	static TSharedRef<FJsonValue> TextToJson(const FText& Text);

	/** Reads {text, namespace, key} or, for schema 1 files, a plain string. */
	static FText JsonToText(const TSharedPtr<FJsonValue>& Value);

	/** [r, g, b, a] with linear values. */
	static TSharedRef<FJsonValue> ColorToJson(const FLinearColor& Color);
	static bool JsonToColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor);

	/** Pin type with category, sub category, object path, container, flags and the value type of maps. */
	static TSharedRef<FJsonObject> PinTypeToJson(const FEdGraphPinType& Type);

	/**
	 * Reads a pin type. A class, struct or enum that cannot be found makes the type Wildcard and its path is
	 * returned in OutMissingObject (CNA010, spec 8.2, item 4). A path outside the allowed roots is returned in
	 * OutForbiddenPath and the type is Wildcard as well.
	 */
	static void JsonToPinType(const TSharedPtr<FJsonObject>& Object, FEdGraphPinType& OutType, FString& OutMissingObject, FString& OutForbiddenPath);

	/** Object path of the sub category object of a pin type, empty when there is none. */
	static FString GetPinTypeObjectPath(const FEdGraphPinType& Type);

	static FString CornerIconToString(ECNACornerIcon Value);
	static bool StringToCornerIcon(const FString& Text, ECNACornerIcon& OutValue);
	static FString ConnectionRuleToString(ECNAConnectionRule Value);
	static bool StringToConnectionRule(const FString& Text, ECNAConnectionRule& OutValue);
	static FString BadgeCornerToString(ECNABadgeCorner Value);
	static bool StringToBadgeCorner(const FString& Text, ECNABadgeCorner& OutValue);
	static FString BodyWidgetTypeToString(ECNABodyWidgetType Value);
	static bool StringToBodyWidgetType(const FString& Text, ECNABodyWidgetType& OutValue);
	static FString GraphKindToString(ECNAGraphKind Kind);

	/** AllowedGraphs mask as ["Function", "EventGraph", ...]. */
	static TSharedRef<FJsonValue> GraphKindsToJson(int32 Mask);

	/** Array of kind names, or the number mask of schema 1. */
	static int32 JsonToGraphKinds(const TSharedPtr<FJsonValue>& Value);

	/** Text split into lines (LF), for t3d. CR characters are dropped. */
	static TArray<TSharedPtr<FJsonValue>> SplitLines(const FString& Text);

	/** Array of lines or one string back to one text with LF line ends. */
	static FString JoinLines(const TSharedPtr<FJsonValue>& Value);

	static TArray<TSharedPtr<FJsonValue>> StringsToJson(const TArray<FString>& Strings);
	static void JsonToStrings(const TSharedPtr<FJsonValue>& Value, TArray<FString>& OutStrings);

	// Field access with defaults

	static FString GetString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Default = FString());
	static bool GetBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool bDefault = false);
	static double GetNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Default = 0.0);
	static TSharedPtr<FJsonObject> GetObject(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field);
	static TArray<TSharedPtr<FJsonValue>> GetArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field);
	static TSharedPtr<FJsonValue> GetField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field);
};
