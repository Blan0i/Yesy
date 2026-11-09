// Copyright (c) NodeFactory. JSON format of Custom Node Assets: field names, versions, migrations, value
// conversions, path checks and the deterministic writer (spec 8.1, 8.2).

#include "CNAJsonSchema.h"
#include "EdGraphSchema_K2.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"
#include "Math/Color.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNAJsonSchemaLocal
{
	// Writer

	void AppendIndent(FString& Out, int32 Level)
	{
		for (int32 Index = 0; Index < Level; ++Index)
		{
			Out += TEXT("  ");
		}
	}

	void AppendEscaped(FString& Out, const FString& Value)
	{
		Out += TEXT('"');
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			const TCHAR Char = Value[Index];
			switch (Char)
			{
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			case TEXT('\b'): Out += TEXT("\\b"); break;
			case TEXT('\f'): Out += TEXT("\\f"); break;
			default:
				if (Char < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Char));
				}
				else
				{
					Out += Char;
				}
				break;
			}
		}
		Out += TEXT('"');
	}

	/** Short arrays of numbers (colors) stay on one line: [0, 0.72, 0.66, 1]. */
	bool IsInlineArray(const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		if (Values.Num() == 0 || Values.Num() > 8)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : Values)
		{
			if (!Value.IsValid() || (Value->Type != EJson::Number && Value->Type != EJson::Boolean && Value->Type != EJson::Null))
			{
				return false;
			}
		}
		return true;
	}

	void AppendValue(FString& Out, const TSharedPtr<FJsonValue>& Value, int32 Level);

	void AppendObject(FString& Out, const TSharedPtr<FJsonObject>& Object, int32 Level)
	{
		if (!Object.IsValid() || Object->Values.Num() == 0)
		{
			Out += TEXT("{}");
			return;
		}

		Out += TEXT("{\n");
		int32 Written = 0;
		const int32 Count = Object->Values.Num();
		// TMap keeps the insertion order while nothing is removed: the order of the exporter is kept.
		for (const auto& Pair : Object->Values)
		{
			AppendIndent(Out, Level + 1);
			AppendEscaped(Out, Pair.Key);
			Out += TEXT(": ");
			AppendValue(Out, Pair.Value, Level + 1);
			if (++Written < Count)
			{
				Out += TEXT(',');
			}
			Out += TEXT('\n');
		}
		AppendIndent(Out, Level);
		Out += TEXT('}');
	}

	void AppendArray(FString& Out, const TArray<TSharedPtr<FJsonValue>>& Values, int32 Level)
	{
		if (Values.Num() == 0)
		{
			Out += TEXT("[]");
			return;
		}

		if (IsInlineArray(Values))
		{
			Out += TEXT('[');
			for (int32 Index = 0; Index < Values.Num(); ++Index)
			{
				if (Index > 0)
				{
					Out += TEXT(", ");
				}
				AppendValue(Out, Values[Index], Level);
			}
			Out += TEXT(']');
			return;
		}

		Out += TEXT("[\n");
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			AppendIndent(Out, Level + 1);
			AppendValue(Out, Values[Index], Level + 1);
			if (Index + 1 < Values.Num())
			{
				Out += TEXT(',');
			}
			Out += TEXT('\n');
		}
		AppendIndent(Out, Level);
		Out += TEXT(']');
	}

	void AppendValue(FString& Out, const TSharedPtr<FJsonValue>& Value, int32 Level)
	{
		if (!Value.IsValid())
		{
			Out += TEXT("null");
			return;
		}
		switch (Value->Type)
		{
		case EJson::String:
			AppendEscaped(Out, Value->AsString());
			break;
		case EJson::Number:
			Out += FCNAJsonSchema::FormatNumber(Value->AsNumber());
			break;
		case EJson::Boolean:
			Out += Value->AsBool() ? TEXT("true") : TEXT("false");
			break;
		case EJson::Array:
			AppendArray(Out, Value->AsArray(), Level);
			break;
		case EJson::Object:
			AppendObject(Out, Value->AsObject(), Level);
			break;
		case EJson::None:
		case EJson::Null:
		default:
			Out += TEXT("null");
			break;
		}
	}

	// Structure checks

	FText DescribeType(EJson Type)
	{
		switch (Type)
		{
		case EJson::String:  return LOCTEXT("Json.TypeString", "a string");
		case EJson::Number:  return LOCTEXT("Json.TypeNumber", "a number");
		case EJson::Boolean: return LOCTEXT("Json.TypeBool", "true or false");
		case EJson::Array:   return LOCTEXT("Json.TypeArray", "an array");
		case EJson::Object:  return LOCTEXT("Json.TypeObject", "an object");
		default:             return LOCTEXT("Json.TypeNull", "null");
		}
	}

	/** Checks the type of an optional or required field. */
	bool CheckField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, EJson Expected, bool bRequired, const FString& Path, TArray<FText>& OutErrors)
	{
		const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Field) : nullptr;
		const FString FullPath = Path.IsEmpty() ? FString(Field) : FString::Printf(TEXT("%s.%s"), *Path, Field);
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			if (bRequired)
			{
				OutErrors.Add(FText::Format(LOCTEXT("Json.MissingField", "Field \"{0}\" is missing."), FText::FromString(FullPath)));
				return false;
			}
			return true;
		}
		if (Value->Type != Expected)
		{
			OutErrors.Add(FText::Format(LOCTEXT("Json.WrongType", "Field \"{0}\" must be {1}."), FText::FromString(FullPath), DescribeType(Expected)));
			return false;
		}
		return true;
	}

	/** A text field: an object {text, namespace, key} (or a plain string, accepted for hand written files). */
	void CheckTextField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Path, TArray<FText>& OutErrors)
	{
		const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Field) : nullptr;
		if (!Value.IsValid() || Value->Type == EJson::Null || Value->Type == EJson::String)
		{
			return;
		}
		const FString FullPath = Path.IsEmpty() ? FString(Field) : FString::Printf(TEXT("%s.%s"), *Path, Field);
		if (Value->Type != EJson::Object)
		{
			OutErrors.Add(FText::Format(LOCTEXT("Json.WrongText", "Field \"{0}\" must be a text object {text, namespace, key}."), FText::FromString(FullPath)));
			return;
		}
		CheckField(Value->AsObject(), CNAJsonFields::Text, EJson::String, false, FullPath, OutErrors);
		CheckField(Value->AsObject(), CNAJsonFields::Namespace, EJson::String, false, FullPath, OutErrors);
		CheckField(Value->AsObject(), CNAJsonFields::Key, EJson::String, false, FullPath, OutErrors);
	}

	/** t3d: one string or an array of strings. */
	void CheckLinesField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool bRequired, const FString& Path, TArray<FText>& OutErrors)
	{
		const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Field) : nullptr;
		const FString FullPath = Path.IsEmpty() ? FString(Field) : FString::Printf(TEXT("%s.%s"), *Path, Field);
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			if (bRequired)
			{
				OutErrors.Add(FText::Format(LOCTEXT("Json.MissingField", "Field \"{0}\" is missing."), FText::FromString(FullPath)));
			}
			return;
		}
		if (Value->Type == EJson::String)
		{
			return;
		}
		if (Value->Type != EJson::Array)
		{
			OutErrors.Add(FText::Format(LOCTEXT("Json.WrongLines", "Field \"{0}\" must be a string or an array of strings."), FText::FromString(FullPath)));
			return;
		}
		for (const TSharedPtr<FJsonValue>& Line : Value->AsArray())
		{
			if (!Line.IsValid() || Line->Type != EJson::String)
			{
				OutErrors.Add(FText::Format(LOCTEXT("Json.WrongLines", "Field \"{0}\" must be a string or an array of strings."), FText::FromString(FullPath)));
				return;
			}
		}
	}

	/** Every element of an optional array is an object; returns the objects. */
	TArray<TSharedPtr<FJsonObject>> CheckObjectArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Path, TArray<FText>& OutErrors)
	{
		TArray<TSharedPtr<FJsonObject>> Result;
		if (!CheckField(Object, Field, EJson::Array, false, Path, OutErrors))
		{
			return Result;
		}
		const FString FullPath = Path.IsEmpty() ? FString(Field) : FString::Printf(TEXT("%s.%s"), *Path, Field);
		const TArray<TSharedPtr<FJsonValue>> Values = FCNAJsonSchema::GetArray(Object, Field);
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = Values[Index];
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				OutErrors.Add(FText::Format(LOCTEXT("Json.WrongElement", "Element {1} of \"{0}\" must be an object."), FText::FromString(FullPath), FText::AsNumber(Index)));
				continue;
			}
			Result.Add(Value->AsObject());
		}
		return Result;
	}

	void CheckPinType(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Path, TArray<FText>& OutErrors)
	{
		if (!CheckField(Object, Field, EJson::Object, true, Path, OutErrors))
		{
			return;
		}
		const FString FullPath = FString::Printf(TEXT("%s.%s"), *Path, Field);
		const TSharedPtr<FJsonObject> Type = FCNAJsonSchema::GetObject(Object, Field);
		CheckField(Type, CNAJsonFields::PinCategory, EJson::String, true, FullPath, OutErrors);
		CheckField(Type, CNAJsonFields::PinSubCategory, EJson::String, false, FullPath, OutErrors);
		CheckField(Type, CNAJsonFields::PinObject, EJson::String, false, FullPath, OutErrors);
		CheckField(Type, CNAJsonFields::Container, EJson::String, false, FullPath, OutErrors);
		CheckField(Type, CNAJsonFields::ValueType, EJson::Object, false, FullPath, OutErrors);
	}

	// Migration 1 -> 2

	void TextFieldToObject(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		if (!Object.IsValid())
		{
			return;
		}
		FString Plain;
		if (Object->TryGetStringField(Field, Plain))
		{
			Object->SetField(Field, FCNAJsonSchema::TextToJson(FText::FromString(Plain)));
		}
	}

	void TextFieldsToObjects(const TArray<TSharedPtr<FJsonValue>>& Elements, std::initializer_list<const TCHAR*> Fields)
	{
		for (const TSharedPtr<FJsonValue>& Element : Elements)
		{
			const TSharedPtr<FJsonObject>* ElementObject = nullptr;
			if (Element.IsValid() && Element->TryGetObject(ElementObject) && ElementObject)
			{
				for (const TCHAR* Field : Fields)
				{
					TextFieldToObject(*ElementObject, Field);
				}
			}
		}
	}

	void LinesFieldToArray(const TSharedPtr<FJsonObject>& Object)
	{
		FString Text;
		if (Object.IsValid() && Object->TryGetStringField(CNAJsonFields::T3D, Text))
		{
			Object->SetArrayField(CNAJsonFields::T3D, FCNAJsonSchema::SplitLines(Text));
		}
	}

	void MigrateV1ToV2(const TSharedPtr<FJsonObject>& Root)
	{
		using namespace CNAJsonFields;
		for (const TCHAR* Field : { DisplayName, Subtitle, Tooltip, Category, Keywords, DefaultComment })
		{
			TextFieldToObject(Root, Field);
		}

		const TSharedPtr<FJsonObject> AppearanceObject = FCNAJsonSchema::GetObject(Root, Appearance);
		TextFieldToObject(AppearanceObject, CompactTitle);
		TextFieldsToObjects(FCNAJsonSchema::GetArray(AppearanceObject, Badges), { Text });
		TextFieldsToObjects(FCNAJsonSchema::GetArray(AppearanceObject, BodyWidgets), { Label });

		const TSharedPtr<FJsonObject> FlagsObject = FCNAJsonSchema::GetObject(Root, Flags);
		TextFieldToObject(FlagsObject, DeprecationMessage);
		double Mask = 0.0;
		if (FlagsObject.IsValid() && FlagsObject->TryGetNumberField(AllowedGraphs, Mask))
		{
			FlagsObject->SetField(AllowedGraphs, FCNAJsonSchema::GraphKindsToJson(static_cast<int32>(Mask)));
		}

		TextFieldsToObjects(FCNAJsonSchema::GetArray(Root, PinOverrides), { DisplayName, Tooltip, DynamicNameFormat });
		TextFieldsToObjects(FCNAJsonSchema::GetArray(Root, LocalVariables), { Tooltip });

		LinesFieldToArray(FCNAJsonSchema::GetObject(Root, Graph));
		LinesFieldToArray(FCNAJsonSchema::GetObject(Root, Behavior));
	}

	// Enumerations

	template <typename EnumType, int32 Count>
	FString EnumToString(EnumType Value, const TCHAR* const (&Names)[Count])
	{
		const int32 Index = static_cast<int32>(Value);
		return Index >= 0 && Index < Count ? FString(Names[Index]) : FString(Names[0]);
	}

	template <typename EnumType, int32 Count>
	bool StringToEnum(const FString& Text, const TCHAR* const (&Names)[Count], EnumType& OutValue)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Text.Equals(Names[Index], ESearchCase::IgnoreCase))
			{
				OutValue = static_cast<EnumType>(Index);
				return true;
			}
		}
		return false;
	}

	static const TCHAR* const CornerIconNames[] = { TEXT("None"), TEXT("Latent"), TEXT("Server"), TEXT("Client"), TEXT("Multicast"), TEXT("Message") };
	static const TCHAR* const ConnectionRuleNames[] = { TEXT("None"), TEXT("SameTypeOnly"), TEXT("ClassFilter") };
	static const TCHAR* const BadgeCornerNames[] = { TEXT("TopLeft"), TEXT("TopRight"), TEXT("BottomLeft"), TEXT("BottomRight") };
	static const TCHAR* const BodyWidgetTypeNames[] = { TEXT("CheckBox"), TEXT("ComboBox"), TEXT("TextBox"), TEXT("ScrollList"), TEXT("Slider") };
	static const TCHAR* const GraphKindNames[] = { TEXT("Function"), TEXT("EventGraph"), TEXT("Macro"), TEXT("ConstructionScript") };
	static const TCHAR* const ContainerNames[] = { TEXT("None"), TEXT("Array"), TEXT("Set"), TEXT("Map") };

	FString ContainerToString(EPinContainerType Container)
	{
		switch (Container)
		{
		case EPinContainerType::Array: return ContainerNames[1];
		case EPinContainerType::Set:   return ContainerNames[2];
		case EPinContainerType::Map:   return ContainerNames[3];
		default:                       return ContainerNames[0];
		}
	}

	EPinContainerType StringToContainer(const FString& Text)
	{
		if (Text.Equals(ContainerNames[1], ESearchCase::IgnoreCase)) { return EPinContainerType::Array; }
		if (Text.Equals(ContainerNames[2], ESearchCase::IgnoreCase)) { return EPinContainerType::Set; }
		if (Text.Equals(ContainerNames[3], ESearchCase::IgnoreCase)) { return EPinContainerType::Map; }
		return EPinContainerType::None;
	}

	void MakeWildcard(FEdGraphPinType& Type)
	{
		const EPinContainerType Container = Type.ContainerType == EPinContainerType::Map ? EPinContainerType::None : Type.ContainerType;
		Type = FEdGraphPinType();
		Type.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		Type.ContainerType = Container;
	}

	/** Reads the object of a type; reports a missing or forbidden path. */
	UObject* ResolveTypeObject(const FString& Path, FString& OutMissingObject, FString& OutForbiddenPath)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		bool bForbidden = false;
		UObject* Object = FCNAJsonSchema::ResolveObjectPath(Path, nullptr, bForbidden);
		if (bForbidden)
		{
			OutForbiddenPath = Path;
		}
		else if (!Object)
		{
			OutMissingObject = Path;
		}
		return Object;
	}
}

// Reading and writing

bool FCNAJsonSchema::Parse(const FString& Text, TSharedPtr<FJsonObject>& OutRoot, FText& OutError)
{
	OutRoot.Reset();
	if (Text.TrimStartAndEnd().IsEmpty())
	{
		OutError = LOCTEXT("Json.Empty", "The file is empty.");
		return false;
	}

	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FText::Format(LOCTEXT("Json.ParseError", "The file is not valid JSON: {0}"), FText::FromString(Reader->GetErrorMessage()));
		return false;
	}
	OutRoot = Root;
	return true;
}

FString FCNAJsonSchema::Write(const TSharedRef<FJsonObject>& Root)
{
	FString Out;
	Out.Reserve(16 * 1024);
	CNAJsonSchemaLocal::AppendObject(Out, Root, 0);
	Out += TEXT('\n');
	return Out;
}

FString FCNAJsonSchema::FormatNumber(double Value)
{
	const bool bFinite = Value == Value && Value <= TNumericLimits<double>::Max() && Value >= -TNumericLimits<double>::Max();
	if (!bFinite)
	{
		return TEXT("0");
	}
	if (FMath::Abs(Value) < 1.0e15 && FMath::FloorToDouble(Value) == Value)
	{
		return FString::Printf(TEXT("%lld"), static_cast<long long>(Value));
	}

	// The values of the asset are floats: the shortest text that reads back as the same float is written.
	const float AsFloat = static_cast<float>(Value);
	for (int32 Precision = 6; Precision <= 9; ++Precision)
	{
		const FString Candidate = FString::Printf(TEXT("%.*g"), Precision, Value);
		if (static_cast<float>(FCString::Atod(*Candidate)) == AsFloat)
		{
			return Candidate;
		}
	}
	return FString::Printf(TEXT("%.17g"), Value);
}

// Versions and structure

int32 FCNAJsonSchema::GetSchemaVersion(const TSharedPtr<FJsonObject>& Root)
{
	double Version = 0.0;
	if (Root.IsValid() && Root->TryGetNumberField(CNAJsonFields::SchemaVersion, Version))
	{
		return static_cast<int32>(Version);
	}
	return 0;
}

bool FCNAJsonSchema::Migrate(const TSharedPtr<FJsonObject>& Root, TArray<FText>& OutNotes, FText& OutError)
{
	if (!Root.IsValid())
	{
		OutError = LOCTEXT("Json.NoRoot", "The file does not contain a JSON object.");
		return false;
	}

	const int32 Version = GetSchemaVersion(Root);
	if (Version > CurrentVersion)
	{
		OutError = FText::Format(LOCTEXT("Json.TooNew", "The file has schemaVersion {0}; this version of NodeFactory reads up to {1}. Update the plugin to import it."),
			FText::AsNumber(Version), FText::AsNumber(static_cast<int32>(CurrentVersion)));
		return false;
	}
	if (Version < OldestSupportedVersion)
	{
		OutError = LOCTEXT("Json.NoVersion", "The file has no valid schemaVersion; it was not written by NodeFactory.");
		return false;
	}

	if (Version < 2)
	{
		CNAJsonSchemaLocal::MigrateV1ToV2(Root);
		OutNotes.Add(LOCTEXT("Json.Migrated1", "The file was migrated from schemaVersion 1: texts got namespace and key fields, graph kinds are listed by name."));
	}

	Root->SetNumberField(CNAJsonFields::SchemaVersion, static_cast<double>(CurrentVersion));
	return true;
}

bool FCNAJsonSchema::ValidateStructure(const TSharedPtr<FJsonObject>& Root, TArray<FText>& OutErrors)
{
	using namespace CNAJsonSchemaLocal;
	using namespace CNAJsonFields;
	const int32 ErrorsBefore = OutErrors.Num();

	if (!Root.IsValid())
	{
		OutErrors.Add(LOCTEXT("Json.NoRoot", "The file does not contain a JSON object."));
		return false;
	}

	CheckField(Root, SchemaVersion, EJson::Number, true, FString(), OutErrors);
	CheckField(Root, EngineVersion, EJson::String, false, FString(), OutErrors);
	CheckField(Root, PluginVersion, EJson::String, false, FString(), OutErrors);
	if (CheckField(Root, Id, EJson::String, false, FString(), OutErrors))
	{
		FGuid Parsed;
		const FString IdText = GetString(Root, Id);
		if (!IdText.IsEmpty() && !FGuid::Parse(IdText, Parsed))
		{
			OutErrors.Add(FText::Format(LOCTEXT("Json.BadId", "Field \"id\" is not a GUID: {0}"), FText::FromString(IdText)));
		}
	}
	if (CheckField(Root, Name, EJson::String, true, FString(), OutErrors) && GetString(Root, Name).TrimStartAndEnd().IsEmpty())
	{
		OutErrors.Add(LOCTEXT("Json.EmptyName", "Field \"name\" is empty."));
	}
	for (const TCHAR* Field : { DisplayName, Subtitle, Tooltip, Category, Keywords, DefaultComment })
	{
		CheckTextField(Root, Field, FString(), OutErrors);
	}

	// appearance
	if (CheckField(Root, Appearance, EJson::Object, false, FString(), OutErrors))
	{
		const TSharedPtr<FJsonObject> AppearanceObject = GetObject(Root, Appearance);
		const FString Path(Appearance);
		CheckField(AppearanceObject, UseHeaderColor, EJson::Boolean, false, Path, OutErrors);
		CheckField(AppearanceObject, Icon, EJson::String, false, Path, OutErrors);
		CheckField(AppearanceObject, IconTexture, EJson::String, false, Path, OutErrors);
		CheckField(AppearanceObject, CornerIcon, EJson::String, false, Path, OutErrors);
		CheckField(AppearanceObject, CornerIconTexture, EJson::String, false, Path, OutErrors);
		CheckField(AppearanceObject, CornerIconTextureSize, EJson::Number, false, Path, OutErrors);
		CheckField(AppearanceObject, MinWidth, EJson::Number, false, Path, OutErrors);
		CheckField(AppearanceObject, MinHeight, EJson::Number, false, Path, OutErrors);
		CheckField(AppearanceObject, ColorWires, EJson::Boolean, false, Path, OutErrors);
		CheckField(AppearanceObject, Compact, EJson::Boolean, false, Path, OutErrors);
		CheckTextField(AppearanceObject, CompactTitle, Path, OutErrors);
		for (const TSharedPtr<FJsonObject>& Badge : CheckObjectArray(AppearanceObject, Badges, Path, OutErrors))
		{
			CheckTextField(Badge, Text, Path + TEXT(".badges"), OutErrors);
			CheckField(Badge, Corner, EJson::String, false, Path + TEXT(".badges"), OutErrors);
		}
		for (const TSharedPtr<FJsonObject>& Widget : CheckObjectArray(AppearanceObject, BodyWidgets, Path, OutErrors))
		{
			CheckField(Widget, PinName, EJson::String, true, Path + TEXT(".bodyWidgets"), OutErrors);
			CheckField(Widget, Type, EJson::String, false, Path + TEXT(".bodyWidgets"), OutErrors);
			CheckField(Widget, Options, EJson::Array, false, Path + TEXT(".bodyWidgets"), OutErrors);
			CheckTextField(Widget, Label, Path + TEXT(".bodyWidgets"), OutErrors);
			CheckField(Widget, Height, EJson::Number, false, Path + TEXT(".bodyWidgets"), OutErrors);
			CheckField(Widget, SliderMin, EJson::Number, false, Path + TEXT(".bodyWidgets"), OutErrors);
			CheckField(Widget, SliderMax, EJson::Number, false, Path + TEXT(".bodyWidgets"), OutErrors);
		}
	}

	// flags
	if (CheckField(Root, Flags, EJson::Object, false, FString(), OutErrors))
	{
		const TSharedPtr<FJsonObject> FlagsObject = GetObject(Root, Flags);
		const FString Path(Flags);
		for (const TCHAR* Field : { Pure, DevelopmentOnly, Deprecated, HiddenInMenu, AllowInstanceLabel })
		{
			CheckField(FlagsObject, Field, EJson::Boolean, false, Path, OutErrors);
		}
		CheckTextField(FlagsObject, DeprecationMessage, Path, OutErrors);
		CheckField(FlagsObject, AllowedGraphs, EJson::Array, false, Path, OutErrors);
		CheckField(FlagsObject, ContextClass, EJson::String, false, Path, OutErrors);
	}

	// pins
	for (const TSharedPtr<FJsonObject>& Override : CheckObjectArray(Root, PinOverrides, FString(), OutErrors))
	{
		const FString Path(PinOverrides);
		CheckField(Override, PinName, EJson::String, true, Path, OutErrors);
		CheckTextField(Override, DisplayName, Path, OutErrors);
		CheckTextField(Override, Tooltip, Path, OutErrors);
		CheckTextField(Override, DynamicNameFormat, Path, OutErrors);
		CheckField(Override, DynamicMin, EJson::Number, false, Path, OutErrors);
		CheckField(Override, DynamicMax, EJson::Number, false, Path, OutErrors);
		CheckField(Override, ConnectionRule, EJson::String, false, Path, OutErrors);
		CheckField(Override, ClassFilter, EJson::String, false, Path, OutErrors);
	}
	for (const TSharedPtr<FJsonObject>& Redirect : CheckObjectArray(Root, PinRedirects, FString(), OutErrors))
	{
		CheckField(Redirect, OldName, EJson::String, true, PinRedirects, OutErrors);
		CheckField(Redirect, NewName, EJson::String, true, PinRedirects, OutErrors);
	}
	for (const TSharedPtr<FJsonObject>& Variable : CheckObjectArray(Root, LocalVariables, FString(), OutErrors))
	{
		CheckField(Variable, Name, EJson::String, true, LocalVariables, OutErrors);
		CheckField(Variable, Guid, EJson::String, false, LocalVariables, OutErrors);
		CheckField(Variable, DefaultValue, EJson::String, false, LocalVariables, OutErrors);
		CheckTextField(Variable, Tooltip, LocalVariables, OutErrors);
		CheckPinType(Variable, Type, LocalVariables, OutErrors);
	}

	// graphs
	if (CheckField(Root, Graph, EJson::Object, true, FString(), OutErrors))
	{
		const TSharedPtr<FJsonObject> GraphObject = GetObject(Root, Graph);
		CheckLinesField(GraphObject, T3D, true, Graph, OutErrors);
		for (const TSharedPtr<FJsonObject>& Pin : CheckObjectArray(GraphObject, Signature, Graph, OutErrors))
		{
			const FString Path = FString(Graph) + TEXT(".signature");
			CheckField(Pin, Name, EJson::String, true, Path, OutErrors);
			CheckField(Pin, Direction, EJson::String, true, Path, OutErrors);
			CheckPinType(Pin, Type, Path, OutErrors);
		}
	}
	if (CheckField(Root, Behavior, EJson::Object, false, FString(), OutErrors))
	{
		CheckLinesField(GetObject(Root, Behavior), T3D, false, Behavior, OutErrors);
	}

	if (CheckField(Root, Textures, EJson::Array, false, FString(), OutErrors))
	{
		for (const TSharedPtr<FJsonValue>& Value : GetArray(Root, Textures))
		{
			if (!Value.IsValid() || Value->Type != EJson::String)
			{
				OutErrors.Add(LOCTEXT("Json.WrongTextures", "Field \"textures\" must be an array of strings."));
				break;
			}
		}
	}
	if (CheckField(Root, Meta, EJson::Object, false, FString(), OutErrors))
	{
		const TSharedPtr<FJsonObject> MetaObject = GetObject(Root, Meta);
		CheckField(MetaObject, Author, EJson::String, false, Meta, OutErrors);
		CheckField(MetaObject, Description, EJson::String, false, Meta, OutErrors);
		CheckField(MetaObject, Tags, EJson::Array, false, Meta, OutErrors);
		CheckField(MetaObject, SourceUrl, EJson::String, false, Meta, OutErrors);
	}

	return OutErrors.Num() == ErrorsBefore;
}

// Paths

TArray<FString> FCNAJsonSchema::GetAllowedRoots()
{
	TArray<FString> Roots;
	Roots.Add(TEXT("/Script/"));
	Roots.Add(TEXT("/Game/"));
	// Engine content is read only and ships with the engine; the standard macros (ForEachLoop and others) live
	// there, so graphs that use them can be imported.
	Roots.Add(TEXT("/Engine/"));

	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		if (Plugin->CanContainContent())
		{
			Roots.AddUnique(FString::Printf(TEXT("/%s/"), *Plugin->GetName()));
		}
	}
	return Roots;
}

bool FCNAJsonSchema::IsAllowedObjectPath(const FString& Path)
{
	if (Path.Len() < 3 || !Path.StartsWith(TEXT("/")))
	{
		return false;
	}
	if (Path.Contains(TEXT("..")) || Path.Contains(TEXT("\\")) || Path.Contains(TEXT("//")) || Path.Contains(TEXT("%")))
	{
		return false;
	}
	for (const FString& Root : GetAllowedRoots())
	{
		if (Path.StartsWith(Root, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

void FCNAJsonSchema::FindObjectPathsInText(const FString& Text, TArray<FString>& OutPaths)
{
	const int32 Length = Text.Len();
	for (int32 Index = 0; Index + 1 < Length; ++Index)
	{
		const TCHAR Char = Text[Index];
		if (Text[Index + 1] != TEXT('/'))
		{
			continue;
		}

		// Class'/Script/Engine.Actor' and "/Game/Folder/Asset.Asset": read up to the closing quote.
		if (Char == TEXT('\'') || Char == TEXT('"'))
		{
			int32 End = Index + 1;
			while (End < Length && Text[End] != Char && Text[End] != TEXT('\n') && Text[End] != TEXT('\r'))
			{
				++End;
			}
			const FString Path = Text.Mid(Index + 1, End - Index - 1);
			if (Path.Len() > 1)
			{
				OutPaths.AddUnique(Path);
			}
			Index = End;
			continue;
		}

		// Class=/Script/BlueprintGraph.K2Node_CallFunction: read up to a separator.
		if (Char == TEXT('='))
		{
			int32 End = Index + 1;
			while (End < Length && !FChar::IsWhitespace(Text[End]) && Text[End] != TEXT(',') && Text[End] != TEXT(')') && Text[End] != TEXT('"') && Text[End] != TEXT('\''))
			{
				++End;
			}
			const FString Path = Text.Mid(Index + 1, End - Index - 1);
			if (Path.Len() > 1)
			{
				OutPaths.AddUnique(Path);
			}
			Index = End - 1;
		}
	}
}

void FCNAJsonSchema::FindObjectClassesInT3D(const FString& T3D, TArray<FString>& OutClassNames)
{
	TArray<FString> Lines;
	T3D.ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);
	for (FString& Line : Lines)
	{
		Line.TrimStartInline();
		if (!Line.StartsWith(TEXT("Begin Object"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		FString ClassName;
		if (FParse::Value(*Line, TEXT("Class="), ClassName) && !ClassName.IsEmpty())
		{
			OutClassNames.AddUnique(ClassName);
		}
	}
}

UClass* FCNAJsonSchema::FindClassByT3DName(const FString& ClassName)
{
	if (ClassName.IsEmpty())
	{
		return nullptr;
	}
	if (ClassName.StartsWith(TEXT("/")))
	{
		return FindObject<UClass>(nullptr, *ClassName);
	}
	return FindObject<UClass>(ANY_PACKAGE, *ClassName);
}

UObject* FCNAJsonSchema::ResolveObjectPath(const FString& Path, UClass* ExpectedClass, bool& bOutForbidden)
{
	bOutForbidden = false;
	if (Path.IsEmpty())
	{
		return nullptr;
	}
	if (!IsAllowedObjectPath(Path))
	{
		bOutForbidden = true;
		return nullptr;
	}

	UClass* SearchClass = ExpectedClass ? ExpectedClass : UObject::StaticClass();
	UObject* Object = StaticFindObject(SearchClass, nullptr, *Path);
	if (!Object && !Path.StartsWith(TEXT("/Script/"), ESearchCase::IgnoreCase))
	{
		// Content of an allowed root is loaded only when its package exists; native classes of modules that are
		// not loaded stay missing (no module is loaded by an import).
		const FString PackageName = FPackageName::ObjectPathToPackageName(Path);
		if (FPackageName::IsValidLongPackageName(PackageName) && FPackageName::DoesPackageExist(PackageName))
		{
			Object = StaticLoadObject(SearchClass, nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
	}
	return Object;
}

// Values

TSharedRef<FJsonValue> FCNAJsonSchema::TextToJson(const FText& Text)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	const FString* Source = FTextInspector::GetSourceString(Text);
	const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text);
	const TOptional<FString> Key = FTextInspector::GetKey(Text);

	Object->SetStringField(CNAJsonFields::Text, Source ? *Source : Text.ToString());
	Object->SetStringField(CNAJsonFields::Namespace, Namespace.IsSet() ? Namespace.GetValue() : FString());
	Object->SetStringField(CNAJsonFields::Key, Key.IsSet() ? Key.GetValue() : FString());
	return MakeShared<FJsonValueObject>(Object);
}

FText FCNAJsonSchema::JsonToText(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->IsNull())
	{
		return FText::GetEmpty();
	}
	if (Value->Type == EJson::String)
	{
		return FText::FromString(Value->AsString());
	}

	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (!Value->TryGetObject(Object) || !Object || !Object->IsValid())
	{
		return FText::GetEmpty();
	}
	const FString Source = GetString(*Object, CNAJsonFields::Text);
	const FString Namespace = GetString(*Object, CNAJsonFields::Namespace);
	const FString Key = GetString(*Object, CNAJsonFields::Key);
	if (Source.IsEmpty())
	{
		return FText::GetEmpty();
	}
	if (Key.IsEmpty())
	{
		return FText::FromString(Source);
	}
	// V-check: the function behind LOCTEXT creates a localizable text with the given namespace and key (4.20.3).
	return FInternationalization::ForUseOnlyByLocMacroAndGraphNodeTextLiterals_CreateText(*Source, *Namespace, *Key);
}

TSharedRef<FJsonValue> FCNAJsonSchema::ColorToJson(const FLinearColor& Color)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Add(MakeShared<FJsonValueNumber>(Color.R));
	Values.Add(MakeShared<FJsonValueNumber>(Color.G));
	Values.Add(MakeShared<FJsonValueNumber>(Color.B));
	Values.Add(MakeShared<FJsonValueNumber>(Color.A));
	return MakeShared<FJsonValueArray>(Values);
}

bool FCNAJsonSchema::JsonToColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor)
{
	if (!Value.IsValid())
	{
		return false;
	}

	// "#00B8A9" or "#00B8A9FF": sRGB hex, convenient in hand written presets.
	FString Hex;
	if (Value->TryGetString(Hex))
	{
		Hex.TrimStartAndEndInline();
		if (Hex.StartsWith(TEXT("#")))
		{
			Hex = Hex.RightChop(1);
		}
		if (Hex.Len() != 6 && Hex.Len() != 8)
		{
			return false;
		}
		for (int32 Index = 0; Index < Hex.Len(); ++Index)
		{
			if (!FChar::IsHexDigit(Hex[Index]))
			{
				return false;
			}
		}
		OutColor = FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Value->TryGetArray(Values) || !Values || (Values->Num() != 3 && Values->Num() != 4))
	{
		return false;
	}
	float Channels[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	for (int32 Index = 0; Index < Values->Num(); ++Index)
	{
		double Number = 0.0;
		if (!(*Values)[Index].IsValid() || !(*Values)[Index]->TryGetNumber(Number))
		{
			return false;
		}
		Channels[Index] = static_cast<float>(Number);
	}
	OutColor = FLinearColor(Channels[0], Channels[1], Channels[2], Channels[3]);
	return true;
}

TSharedRef<FJsonObject> FCNAJsonSchema::PinTypeToJson(const FEdGraphPinType& Type)
{
	using namespace CNAJsonFields;
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(PinCategory, Type.PinCategory.IsNone() ? FString() : Type.PinCategory.ToString());
	Object->SetStringField(PinSubCategory, Type.PinSubCategory.IsNone() ? FString() : Type.PinSubCategory.ToString());
	Object->SetStringField(PinObject, GetPinTypeObjectPath(Type));
	Object->SetStringField(Container, CNAJsonSchemaLocal::ContainerToString(Type.ContainerType));
	Object->SetBoolField(IsReference, Type.bIsReference);
	Object->SetBoolField(IsConst, Type.bIsConst);
	Object->SetBoolField(IsWeak, Type.bIsWeakPointer);

	if (Type.ContainerType == EPinContainerType::Map)
	{
		const UObject* ValueObject = Type.PinValueType.TerminalSubCategoryObject.Get();
		TSharedRef<FJsonObject> ValueTypeObject = MakeShared<FJsonObject>();
		ValueTypeObject->SetStringField(PinCategory, Type.PinValueType.TerminalCategory.IsNone() ? FString() : Type.PinValueType.TerminalCategory.ToString());
		ValueTypeObject->SetStringField(PinSubCategory, Type.PinValueType.TerminalSubCategory.IsNone() ? FString() : Type.PinValueType.TerminalSubCategory.ToString());
		ValueTypeObject->SetStringField(PinObject, ValueObject ? ValueObject->GetPathName() : FString());
		ValueTypeObject->SetBoolField(IsConst, Type.PinValueType.bTerminalIsConst);
		ValueTypeObject->SetBoolField(IsWeak, Type.PinValueType.bTerminalIsWeakPointer);
		Object->SetObjectField(ValueType, ValueTypeObject);
	}
	return Object;
}

void FCNAJsonSchema::JsonToPinType(const TSharedPtr<FJsonObject>& Object, FEdGraphPinType& OutType, FString& OutMissingObject, FString& OutForbiddenPath)
{
	using namespace CNAJsonFields;
	OutType = FEdGraphPinType();
	OutMissingObject.Reset();
	OutForbiddenPath.Reset();

	if (!Object.IsValid())
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		return;
	}

	const FString CategoryText = GetString(Object, PinCategory);
	const FString SubCategoryText = GetString(Object, PinSubCategory);
	OutType.PinCategory = CategoryText.IsEmpty() ? UEdGraphSchema_K2::PC_Wildcard : FName(*CategoryText);
	OutType.PinSubCategory = SubCategoryText.IsEmpty() ? NAME_None : FName(*SubCategoryText);
	OutType.ContainerType = CNAJsonSchemaLocal::StringToContainer(GetString(Object, Container));
	OutType.bIsReference = GetBool(Object, IsReference);
	OutType.bIsConst = GetBool(Object, IsConst);
	OutType.bIsWeakPointer = GetBool(Object, IsWeak);

	if (UObject* TypeObject = CNAJsonSchemaLocal::ResolveTypeObject(GetString(Object, PinObject), OutMissingObject, OutForbiddenPath))
	{
		OutType.PinSubCategoryObject = TypeObject;
	}

	if (OutType.ContainerType == EPinContainerType::Map)
	{
		const TSharedPtr<FJsonObject> ValueTypeObject = GetObject(Object, ValueType);
		const FString ValueCategory = GetString(ValueTypeObject, PinCategory);
		const FString ValueSubCategory = GetString(ValueTypeObject, PinSubCategory);
		OutType.PinValueType.TerminalCategory = ValueCategory.IsEmpty() ? UEdGraphSchema_K2::PC_Wildcard : FName(*ValueCategory);
		OutType.PinValueType.TerminalSubCategory = ValueSubCategory.IsEmpty() ? NAME_None : FName(*ValueSubCategory);
		OutType.PinValueType.bTerminalIsConst = GetBool(ValueTypeObject, IsConst);
		OutType.PinValueType.bTerminalIsWeakPointer = GetBool(ValueTypeObject, IsWeak);
		if (OutMissingObject.IsEmpty() && OutForbiddenPath.IsEmpty())
		{
			if (UObject* ValueObject = CNAJsonSchemaLocal::ResolveTypeObject(GetString(ValueTypeObject, PinObject), OutMissingObject, OutForbiddenPath))
			{
				OutType.PinValueType.TerminalSubCategoryObject = ValueObject;
			}
		}
	}

	// A missing class, struct or enum makes the pin Wildcard (spec 8.2, item 4).
	if (!OutMissingObject.IsEmpty() || !OutForbiddenPath.IsEmpty())
	{
		CNAJsonSchemaLocal::MakeWildcard(OutType);
	}
}

FString FCNAJsonSchema::GetPinTypeObjectPath(const FEdGraphPinType& Type)
{
	const UObject* Object = Type.PinSubCategoryObject.Get();
	return Object ? Object->GetPathName() : FString();
}

FString FCNAJsonSchema::CornerIconToString(ECNACornerIcon Value)
{
	return CNAJsonSchemaLocal::EnumToString(Value, CNAJsonSchemaLocal::CornerIconNames);
}

bool FCNAJsonSchema::StringToCornerIcon(const FString& Text, ECNACornerIcon& OutValue)
{
	return CNAJsonSchemaLocal::StringToEnum(Text, CNAJsonSchemaLocal::CornerIconNames, OutValue);
}

FString FCNAJsonSchema::ConnectionRuleToString(ECNAConnectionRule Value)
{
	return CNAJsonSchemaLocal::EnumToString(Value, CNAJsonSchemaLocal::ConnectionRuleNames);
}

bool FCNAJsonSchema::StringToConnectionRule(const FString& Text, ECNAConnectionRule& OutValue)
{
	return CNAJsonSchemaLocal::StringToEnum(Text, CNAJsonSchemaLocal::ConnectionRuleNames, OutValue);
}

FString FCNAJsonSchema::BadgeCornerToString(ECNABadgeCorner Value)
{
	return CNAJsonSchemaLocal::EnumToString(Value, CNAJsonSchemaLocal::BadgeCornerNames);
}

bool FCNAJsonSchema::StringToBadgeCorner(const FString& Text, ECNABadgeCorner& OutValue)
{
	return CNAJsonSchemaLocal::StringToEnum(Text, CNAJsonSchemaLocal::BadgeCornerNames, OutValue);
}

FString FCNAJsonSchema::BodyWidgetTypeToString(ECNABodyWidgetType Value)
{
	return CNAJsonSchemaLocal::EnumToString(Value, CNAJsonSchemaLocal::BodyWidgetTypeNames);
}

bool FCNAJsonSchema::StringToBodyWidgetType(const FString& Text, ECNABodyWidgetType& OutValue)
{
	return CNAJsonSchemaLocal::StringToEnum(Text, CNAJsonSchemaLocal::BodyWidgetTypeNames, OutValue);
}

FString FCNAJsonSchema::GraphKindToString(ECNAGraphKind Kind)
{
	return CNAJsonSchemaLocal::EnumToString(Kind, CNAJsonSchemaLocal::GraphKindNames);
}

TSharedRef<FJsonValue> FCNAJsonSchema::GraphKindsToJson(int32 Mask)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (int32 Index = 0; Index < static_cast<int32>(ARRAY_COUNT(CNAJsonSchemaLocal::GraphKindNames)); ++Index)
	{
		const ECNAGraphKind Kind = static_cast<ECNAGraphKind>(Index);
		if (CNATypes::IsGraphKindAllowed(Mask, Kind))
		{
			Values.Add(MakeShared<FJsonValueString>(GraphKindToString(Kind)));
		}
	}
	return MakeShared<FJsonValueArray>(Values);
}

int32 FCNAJsonSchema::JsonToGraphKinds(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->IsNull())
	{
		return CNATypes::AllGraphKindsMask();
	}
	double Number = 0.0;
	if (Value->TryGetNumber(Number))
	{
		return static_cast<int32>(Number) & CNATypes::AllGraphKindsMask();
	}

	int32 Mask = 0;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Value->TryGetArray(Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& Element : *Values)
		{
			FString Name;
			ECNAGraphKind Kind = ECNAGraphKind::Function;
			if (Element.IsValid() && Element->TryGetString(Name) && CNAJsonSchemaLocal::StringToEnum(Name, CNAJsonSchemaLocal::GraphKindNames, Kind))
			{
				Mask |= CNATypes::GetGraphKindFlag(Kind);
			}
		}
	}
	return Mask;
}

TArray<TSharedPtr<FJsonValue>> FCNAJsonSchema::SplitLines(const FString& Text)
{
	FString Normalized = Text.Replace(TEXT("\r"), TEXT(""));
	TArray<FString> Lines;
	Normalized.ParseIntoArray(Lines, TEXT("\n"), /*bCullEmpty*/ false);
	if (Lines.Num() > 0 && Lines.Last().IsEmpty())
	{
		Lines.Pop();
	}
	return StringsToJson(Lines);
}

FString FCNAJsonSchema::JoinLines(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return FString();
	}
	FString Text;
	if (Value->TryGetString(Text))
	{
		return Text.Replace(TEXT("\r"), TEXT(""));
	}
	TArray<FString> Lines;
	JsonToStrings(Value, Lines);
	FString Result;
	for (const FString& Line : Lines)
	{
		Result += Line;
		Result += TEXT('\n');
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> FCNAJsonSchema::StringsToJson(const TArray<FString>& Strings)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Strings.Num());
	for (const FString& String : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(String));
	}
	return Values;
}

void FCNAJsonSchema::JsonToStrings(const TSharedPtr<FJsonValue>& Value, TArray<FString>& OutStrings)
{
	OutStrings.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Value.IsValid() || !Value->TryGetArray(Values) || !Values)
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& Element : *Values)
	{
		FString String;
		if (Element.IsValid() && Element->TryGetString(String))
		{
			OutStrings.Add(String);
		}
	}
}

// Field access

FString FCNAJsonSchema::GetString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Default)
{
	FString Value;
	return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : Default;
}

bool FCNAJsonSchema::GetBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool bDefault)
{
	bool bValue = bDefault;
	return Object.IsValid() && Object->TryGetBoolField(Field, bValue) ? bValue : bDefault;
}

double FCNAJsonSchema::GetNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Default)
{
	double Value = Default;
	return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? Value : Default;
}

TSharedPtr<FJsonObject> FCNAJsonSchema::GetObject(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	const TSharedPtr<FJsonObject>* Result = nullptr;
	if (Object.IsValid() && Object->TryGetObjectField(Field, Result) && Result)
	{
		return *Result;
	}
	return nullptr;
}

TArray<TSharedPtr<FJsonValue>> FCNAJsonSchema::GetArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	const TArray<TSharedPtr<FJsonValue>>* Result = nullptr;
	if (Object.IsValid() && Object->TryGetArrayField(Field, Result) && Result)
	{
		return *Result;
	}
	return TArray<TSharedPtr<FJsonValue>>();
}

TSharedPtr<FJsonValue> FCNAJsonSchema::GetField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	return Object.IsValid() ? Object->TryGetField(Field) : nullptr;
}

#undef LOCTEXT_NAMESPACE
