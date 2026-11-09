// Copyright (c) NodeFactory. Safe import of a Custom Node Asset from JSON (spec 8.2) and creation of assets from
// presets of the Node Catalog (spec 8.3).

#include "CNAJsonImporter.h"
#include "CNAAssetFactory.h"
#include "CNAEditorSettings.h"
#include "CNAGraphUtils.h"
#include "CNAJsonExporter.h"
#include "CNAJsonSchema.h"
#include "CNAMessages.h"
#include "CNAPinUtils.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryModule.h"

#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DesktopPlatformModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "EditorDirectories.h"
#include "Engine/Texture2D.h"
#include "Factories.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "IDesktopPlatform.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNAJsonImporterLocal
{
	// Notes and messages

	void AddNote(TArray<FCNAImportNote>& Notes, EMessageSeverity::Type Severity, const FText& Text)
	{
		FCNAImportNote& Note = Notes[Notes.AddDefaulted()];
		Note.Severity = Severity;
		Note.Text = Text;
	}

	FText ForbiddenPathError(const FString& Field, const FString& Path)
	{
		return FText::Format(LOCTEXT("Import.ForbiddenPath", "{0}: the path {1} is outside the allowed roots (/Script/, /Game/, /Engine/ and the content of enabled plugins). The file is not imported."),
			FText::FromString(Field), FText::FromString(Path));
	}

	FText JoinTexts(const TArray<FText>& Texts, int32 MaxItems)
	{
		FString Result;
		for (int32 Index = 0; Index < Texts.Num() && Index < MaxItems; ++Index)
		{
			Result += TEXT("\u2022 ");
			Result += Texts[Index].ToString();
			Result += TEXT("\n");
		}
		if (Texts.Num() > MaxItems)
		{
			Result += FText::Format(LOCTEXT("Import.MoreLines", "... and {0} more"), Texts.Num() - MaxItems).ToString();
			Result += TEXT("\n");
		}
		Result.TrimEndInline();
		return FText::FromString(Result);
	}

	void ShowNotification(const FText& Text, bool bSuccess)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		FNotificationInfo Info(Text);
		Info.ExpireDuration = bSuccess ? 4.0f : 8.0f;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	// Values

	FText ReadText(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FText& Default)
	{
		const TSharedPtr<FJsonValue> Value = FCNAJsonSchema::GetField(Object, Field);
		return Value.IsValid() ? FCNAJsonSchema::JsonToText(Value) : Default;
	}

	FName ReadName(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FName Default)
	{
		if (!Object.IsValid() || !Object->HasField(Field))
		{
			return Default;
		}
		const FString Text = FCNAJsonSchema::GetString(Object, Field).TrimStartAndEnd();
		return Text.IsEmpty() ? NAME_None : FName(*Text);
	}

	int32 ReadInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32 Default, int32 Min, int32 Max)
	{
		const double Value = FCNAJsonSchema::GetNumber(Object, Field, static_cast<double>(Default));
		return FMath::Clamp(FMath::RoundToInt(static_cast<float>(Value)), Min, Max);
	}

	void ReadColor(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Owner, FLinearColor& InOutColor, FCNAImportData& Data)
	{
		const TSharedPtr<FJsonValue> Value = FCNAJsonSchema::GetField(Object, Field);
		if (Value.IsValid() && !Value->IsNull() && !FCNAJsonSchema::JsonToColor(Value, InOutColor))
		{
			AddNote(Data.Notes, EMessageSeverity::Warning, FText::Format(LOCTEXT("Import.BadColor", "{0}.{1} is not a color ([r, g, b, a] or \"#RRGGBB\"); the default is kept."),
				FText::FromString(Owner), FText::FromString(Field)));
		}
	}

	template<typename EnumType>
	void ReadEnum(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Owner, bool (*Parse)(const FString&, EnumType&), EnumType& InOutValue, FCNAImportData& Data)
	{
		if (!Object.IsValid() || !Object->HasField(Field))
		{
			return;
		}
		const FString Text = FCNAJsonSchema::GetString(Object, Field);
		EnumType Parsed = InOutValue;
		if (Parse(Text, Parsed))
		{
			InOutValue = Parsed;
		}
		else
		{
			AddNote(Data.Notes, EMessageSeverity::Warning, FText::Format(LOCTEXT("Import.BadEnum", "{0}.{1}: unknown value \"{2}\"; the default is kept."),
				FText::FromString(Owner), FText::FromString(Field), FText::FromString(Text)));
		}
	}

	/** Class of contextClass or classFilter. A forbidden path is an error, a missing class a warning. */
	UClass* ReadClass(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Owner, FCNAImportData& Data, TArray<FText>& OutErrors)
	{
		const FString Path = FCNAJsonSchema::GetString(Object, Field).TrimStartAndEnd();
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		bool bForbidden = false;
		UClass* Class = Cast<UClass>(FCNAJsonSchema::ResolveObjectPath(Path, UClass::StaticClass(), bForbidden));
		if (bForbidden)
		{
			OutErrors.Add(ForbiddenPathError(Owner + TEXT(".") + Field, Path));
			return nullptr;
		}
		if (!Class)
		{
			AddNote(Data.Notes, EMessageSeverity::Warning, FText::Format(LOCTEXT("Import.MissingClass", "{0}.{1}: class {2} was not found; the field is left empty."),
				FText::FromString(Owner), FText::FromString(Field), FText::FromString(Path)));
		}
		return Class;
	}

	/** Pin type of a local variable or a signature pin; a forbidden path is an error. */
	void ReadPinType(const TSharedPtr<FJsonObject>& Object, const FString& Owner, FEdGraphPinType& OutType, FString& OutMissing, TArray<FText>& OutErrors)
	{
		FString Forbidden;
		FCNAJsonSchema::JsonToPinType(FCNAJsonSchema::GetObject(Object, CNAJsonFields::Type), OutType, OutMissing, Forbidden);
		if (!Forbidden.IsEmpty())
		{
			OutErrors.Add(ForbiddenPathError(Owner, Forbidden));
		}
	}

	TArray<TSharedPtr<FJsonObject>> ReadObjects(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<TSharedPtr<FJsonObject>> Result;
		for (const TSharedPtr<FJsonValue>& Value : FCNAJsonSchema::GetArray(Object, Field))
		{
			const TSharedPtr<FJsonObject>* Element = nullptr;
			if (Value.IsValid() && Value->TryGetObject(Element) && Element && Element->IsValid())
			{
				Result.Add(*Element);
			}
		}
		return Result;
	}

	// T3D

	/** Top level objects of a T3D must be graph nodes the K2 schema accepts (spec 8.2, item 7). */
	bool IsAllowedNodeClass(const UClass* Class)
	{
		if (!Class || !Class->IsChildOf(UEdGraphNode::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return false;
		}
		const UEdGraphNode* Default = Cast<UEdGraphNode>(const_cast<UClass*>(Class)->GetDefaultObject());
		return Default && Default->CanCreateUnderSpecifiedSchema(GetDefault<UEdGraphSchema_K2>());
	}

	/** Nested objects are the graphs of collapsed nodes and their nodes. */
	bool IsAllowedNestedClass(const UClass* Class)
	{
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return false;
		}
		return Class->IsChildOf(UEdGraph::StaticClass()) || Class->IsChildOf(UEdGraphNode::StaticClass());
	}

	/** "/Script/Engine.Actor", "/Game/A/B.B:Execute" - not a plain text such as "/" or "/5". */
	bool LooksLikeObjectPath(const FString& Path)
	{
		return Path.Len() > 3 && Path[0] == TEXT('/') && FChar::IsAlpha(Path[1]) && Path.Contains(TEXT("."));
	}

	/** Exists in memory or as a package on disk; nothing is loaded. */
	bool DoesObjectExist(const FString& Path)
	{
		if (StaticFindObject(UObject::StaticClass(), nullptr, *Path))
		{
			return true;
		}
		if (Path.StartsWith(TEXT("/Script/"), ESearchCase::IgnoreCase))
		{
			return false;
		}
		const FString PackageName = FPackageName::ObjectPathToPackageName(Path);
		return FPackageName::IsValidLongPackageName(PackageName) && FPackageName::DoesPackageExist(PackageName);
	}

	struct FT3DObject
	{
		FString Name;
		FString ClassName;
		int32 Depth = 0;
	};

	/** Begin Object lines with their nesting depth. Returns false when Begin and End do not match. */
	bool ParseT3DObjects(const FString& T3D, TArray<FT3DObject>& OutObjects)
	{
		TArray<FString> Lines;
		T3D.ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);
		int32 Depth = 0;
		for (const FString& RawLine : Lines)
		{
			const FString Line = RawLine.TrimStartAndEnd();
			if (Line.StartsWith(TEXT("Begin Object"), ESearchCase::IgnoreCase))
			{
				FT3DObject& Object = OutObjects[OutObjects.AddDefaulted()];
				FParse::Value(*Line, TEXT("Class="), Object.ClassName);
				FParse::Value(*Line, TEXT("Name="), Object.Name);
				Object.Depth = Depth;
				++Depth;
			}
			else if (Line.StartsWith(TEXT("End Object"), ESearchCase::IgnoreCase))
			{
				if (--Depth < 0)
				{
					return false;
				}
			}
		}
		return Depth == 0;
	}

	bool IsIdentifierChar(TCHAR Char)
	{
		return FChar::IsAlnum(Char) || Char == TEXT('_');
	}

	/** Replaces whole identifiers: K2Node_Tunnel_1 is not touched by a rename of K2Node_Tunnel_10. */
	FString ReplaceIdentifiers(const FString& Text, const TMap<FString, FString>& Renames)
	{
		if (Renames.Num() == 0)
		{
			return Text;
		}
		FString Result;
		Result.Reserve(Text.Len() + Renames.Num() * 4);
		const int32 Length = Text.Len();
		int32 Index = 0;
		while (Index < Length)
		{
			if (!IsIdentifierChar(Text[Index]))
			{
				Result.AppendChar(Text[Index++]);
				continue;
			}
			int32 End = Index;
			while (End < Length && IsIdentifierChar(Text[End]))
			{
				++End;
			}
			const FString Token = Text.Mid(Index, End - Index);
			const FString* Replacement = Renames.Find(Token);
			Result += Replacement ? *Replacement : Token;
			Index = End;
		}
		return Result;
	}

	/**
	 * Gives the top level nodes of the text names that are free in the graph and were never used by a node that
	 * is removed now. The removed nodes stay in the graph until garbage collection and come back with Undo, so a
	 * new node must never take one of their names (spec 9.4). Wires reference nodes by name and are renamed along.
	 */
	FString MakeNodeNamesUnique(const FString& T3D, UEdGraph* Graph, TSet<FName>& InOutReserved)
	{
		TArray<FT3DObject> Objects;
		ParseT3DObjects(T3D, Objects);

		TSet<FName> TextNames;
		for (const FT3DObject& Object : Objects)
		{
			if (!Object.Name.IsEmpty())
			{
				TextNames.Add(FName(*Object.Name));
			}
		}

		TMap<FString, FString> Renames;
		for (const FT3DObject& Object : Objects)
		{
			if (Object.Depth != 0 || Object.Name.IsEmpty() || Renames.Contains(Object.Name))
			{
				continue;
			}
			UClass* Class = FCNAJsonSchema::FindClassByT3DName(Object.ClassName);
			const FName BaseName = Class ? Class->GetFName() : FName(TEXT("EdGraphNode"));
			FName NewName;
			int32 Attempts = 0;
			do
			{
				NewName = MakeUniqueObjectName(Graph, Class ? Class : UEdGraphNode::StaticClass(), BaseName);
			}
			while ((InOutReserved.Contains(NewName) || TextNames.Contains(NewName)) && ++Attempts < 10000);

			InOutReserved.Add(NewName);
			Renames.Add(Object.Name, NewName.ToString());
		}
		return ReplaceIdentifiers(T3D, Renames);
	}

	/**
	 * Creates the nodes of a T3D text. Unlike the paste of the Blueprint editor it also creates the Tunnel nodes of
	 * a macro, and it refuses every class that is not a graph node of the K2 schema (spec 8.2, item 7; V18).
	 */
	class FCNAGraphTextFactory : public FCustomizableTextObjectFactory
	{
	public:
		explicit FCNAGraphTextFactory(UEdGraph* InGraph)
			: FCustomizableTextObjectFactory(GWarn)
			, Graph(InGraph)
		{
		}

		TSet<UEdGraphNode*> SpawnedNodes;

	protected:
		virtual bool CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const override
		{
			bOmitSubObjs = false;
			return IsAllowedNodeClass(ObjectClass);
		}

		virtual void ProcessConstructedObject(UObject* CreatedObject) override
		{
			UEdGraphNode* Node = Cast<UEdGraphNode>(CreatedObject);
			if (Node && Node->GetOuter() == Graph)
			{
				SpawnedNodes.Add(Node);
				Graph->AddNode(Node, /*bUserAction*/ false, /*bSelectNewNode*/ false);
			}
		}

	private:
		UEdGraph* Graph;
	};

	// Changes

	FText PropertyLabel(FName PropertyName)
	{
		const UProperty* Property = UCustomNodeAsset::StaticClass()->FindPropertyByName(PropertyName);
		return Property ? Property->GetDisplayNameText() : FText::FromName(PropertyName);
	}

	FString Shorten(const FString& Text)
	{
		FString Result = Text.Replace(TEXT("\n"), TEXT(" "));
		if (Result.Len() > 60)
		{
			Result = Result.Left(57) + TEXT("...");
		}
		return Result;
	}

	FString Quote(const FString& Text)
	{
		return FString::Printf(TEXT("\"%s\""), *Shorten(Text));
	}

	FString ColorString(const FLinearColor& Color)
	{
		return FString::Printf(TEXT("(%s, %s, %s, %s)"), *FCNAJsonSchema::FormatNumber(Color.R), *FCNAJsonSchema::FormatNumber(Color.G),
			*FCNAJsonSchema::FormatNumber(Color.B), *FCNAJsonSchema::FormatNumber(Color.A));
	}

	FString TextKey(const FText& Text)
	{
		const TOptional<FString> Key = FTextInspector::GetKey(Text);
		return Key.IsSet() ? Key.GetValue() : FString();
	}

	/** Same text; keys only count when both texts have one (presets often carry none). */
	bool AreTextsEqual(const FText& A, const FText& B)
	{
		if (!A.ToString().Equals(B.ToString(), ESearchCase::CaseSensitive))
		{
			return false;
		}
		const FString KeyA = TextKey(A);
		const FString KeyB = TextKey(B);
		return KeyA.IsEmpty() || KeyB.IsEmpty() || KeyA == KeyB;
	}

	bool IsSameT3D(const FString& A, const FString& B)
	{
		return A.Replace(TEXT("\r"), TEXT("")).TrimStartAndEnd().Equals(B.Replace(TEXT("\r"), TEXT("")).TrimStartAndEnd(), ESearchCase::CaseSensitive);
	}

	struct FChangeList
	{
		TArray<FText>& Lines;

		explicit FChangeList(TArray<FText>& InLines) : Lines(InLines) {}

		void Add(FName Property, const FString& Old, const FString& New)
		{
			Lines.Add(FText::Format(LOCTEXT("Import.PropertyChange", "{0}: {1} -> {2}"), PropertyLabel(Property), FText::FromString(Old), FText::FromString(New)));
		}
		void Text(FName Property, const FText& Old, const FText& New)
		{
			if (!AreTextsEqual(Old, New))
			{
				Add(Property, Quote(Old.ToString()), Quote(New.ToString()));
			}
		}
		void String(FName Property, const FString& Old, const FString& New)
		{
			if (!Old.Equals(New, ESearchCase::CaseSensitive))
			{
				Add(Property, Quote(Old), Quote(New));
			}
		}
		void Bool(FName Property, bool bOld, bool bNew)
		{
			if (bOld != bNew)
			{
				Add(Property, bOld ? TEXT("true") : TEXT("false"), bNew ? TEXT("true") : TEXT("false"));
			}
		}
		void Number(FName Property, double Old, double New)
		{
			if (!FMath::IsNearlyEqual(Old, New, 1.0e-4))
			{
				Add(Property, FCNAJsonSchema::FormatNumber(Old), FCNAJsonSchema::FormatNumber(New));
			}
		}
		void Color(FName Property, const FLinearColor& Old, const FLinearColor& New)
		{
			if (!Old.Equals(New, 1.0e-4f))
			{
				Add(Property, ColorString(Old), ColorString(New));
			}
		}
		template<typename StructType>
		void Structs(FName Property, const TArray<StructType>& Old, const TArray<StructType>& New)
		{
			bool bSame = Old.Num() == New.Num();
			for (int32 Index = 0; bSame && Index < Old.Num(); ++Index)
			{
				bSame = StructType::StaticStruct()->CompareScriptStruct(&Old[Index], &New[Index], PPF_None);
			}
			if (!bSame)
			{
				Lines.Add(FText::Format(LOCTEXT("Import.ArrayChange", "{0}: {1} -> {2} entries, contents changed"), PropertyLabel(Property), Old.Num(), New.Num()));
			}
		}
	};

	FText DirectionText(EEdGraphPinDirection Direction)
	{
		return Direction == EGPD_Input ? LOCTEXT("Import.Input", "Input") : LOCTEXT("Import.Output", "Output");
	}

	bool IsGuidUsedByOtherAsset(const FGuid& Guid, const UCustomNodeAsset* Except)
	{
		for (TObjectIterator<UCustomNodeAsset> It; It; ++It)
		{
			if (*It != Except && !It->HasAnyFlags(RF_ClassDefaultObject | RF_Transient) && !It->IsPendingKill() && It->AssetGuid == Guid)
			{
				return true;
			}
		}
		return false;
	}
}

// FCNAImportData

FCNAImportData::FCNAImportData()
{
	// Fields missing in a file keep the values of a new asset, so Export -> Import gives the same asset (A13).
	const UCustomNodeAsset* Defaults = GetDefault<UCustomNodeAsset>();
	DisplayName = Defaults->DisplayName;
	Subtitle = Defaults->Subtitle;
	Tooltip = Defaults->Tooltip;
	Category = Defaults->Category;
	Keywords = Defaults->Keywords;
	DefaultComment = Defaults->DefaultComment;
	bUseHeaderColor = Defaults->bUseHeaderColor;
	HeaderColor = Defaults->HeaderColor;
	HeaderTextColor = Defaults->HeaderTextColor;
	PinLabelColor = Defaults->PinLabelColor;
	Icon = Defaults->Icon;
	IconTint = Defaults->IconTint;
	IconTextureTint = Defaults->IconTextureTint;
	CornerIcon = Defaults->CornerIcon;
	CornerIconTextureTint = Defaults->CornerIconTextureTint;
	CornerIconTextureSize = Defaults->CornerIconTextureSize;
	MinWidth = Defaults->MinWidth;
	MinHeight = Defaults->MinHeight;
	bColorWires = Defaults->bColorWires;
	bCompact = Defaults->bCompact;
	CompactTitle = Defaults->CompactTitle;
	bPure = Defaults->bPure;
	bDevelopmentOnly = Defaults->bDevelopmentOnly;
	bDeprecated = Defaults->bDeprecated;
	DeprecationMessage = Defaults->DeprecationMessage;
	AllowedGraphs = Defaults->AllowedGraphs;
	bHiddenInMenu = Defaults->bHiddenInMenu;
	bAllowInstanceLabel = Defaults->bAllowInstanceLabel;
}

const FCNAImportedPin* FCNAImportData::FindSignaturePin(FName PinName, EEdGraphPinDirection Direction) const
{
	return Signature.FindByPredicate([PinName, Direction](const FCNAImportedPin& Pin)
	{
		return Pin.Name == PinName && Pin.Direction == Direction;
	});
}

// Reading

bool FCNAJsonImporter::LoadFile(const FString& FilePath, FCNAImportData& OutData, TArray<FText>& OutErrors, bool bHeaderOnly)
{
	const FText FileText = FText::FromString(FilePath);
	if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.NoFile", "File {0} was not found."), FileText));
		return false;
	}

	// Item 1: the size is checked before anything is read.
	const int64 Size = IFileManager::Get().FileSize(*FilePath);
	const int64 Limit = GetDefault<UCNAEditorSettings>()->GetMaxImportFileSizeBytes();
	if (Size < 0)
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.NoSize", "The size of {0} cannot be read."), FileText));
		return false;
	}
	if (Size > Limit)
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.TooLarge", "{0} has {1} KB, more than the limit of {2} KB (Editor Preferences, Plugins, NodeFactory, Max Import File Size)."),
			FileText, FText::AsNumber((Size + 1023) / 1024), FText::AsNumber(Limit / 1024)));
		return false;
	}

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.ReadFailed", "{0} cannot be read."), FileText));
		return false;
	}

	const bool bLoaded = LoadString(Text, OutData, OutErrors, bHeaderOnly);
	OutData.SourceFile = FilePath;
	return bLoaded;
}

bool FCNAJsonImporter::LoadString(const FString& Text, FCNAImportData& OutData, TArray<FText>& OutErrors, bool bHeaderOnly)
{
	using namespace CNAJsonImporterLocal;
	const int32 ErrorsBefore = OutErrors.Num();
	OutData = FCNAImportData();

	TSharedPtr<FJsonObject> Root;
	FText Error;
	if (!FCNAJsonSchema::Parse(Text, Root, Error))
	{
		OutErrors.Add(Error);
		return false;
	}

	// Item 2: newer versions are refused, older ones migrated in memory.
	OutData.SourceSchemaVersion = FCNAJsonSchema::GetSchemaVersion(Root);
	TArray<FText> MigrationNotes;
	if (!FCNAJsonSchema::Migrate(Root, MigrationNotes, Error))
	{
		OutErrors.Add(Error);
		return false;
	}
	for (const FText& Note : MigrationNotes)
	{
		AddNote(OutData.Notes, EMessageSeverity::Info, Note);
	}

	if (!FCNAJsonSchema::ValidateStructure(Root, OutErrors))
	{
		return false;
	}

	ReadData(Root, OutData, OutErrors, bHeaderOnly);
	if (!bHeaderOnly)
	{
		CheckT3D(OutData.ExecuteT3D, /*bRequireTunnels*/ true, LOCTEXT("Import.ExecuteGraph", "Execution Graph"), OutData.Name, OutErrors, OutData.Notes);
		CheckT3D(OutData.BehaviorT3D, /*bRequireTunnels*/ false, LOCTEXT("Import.BehaviorGraph", "Editor Behavior Graph"), OutData.Name, OutErrors, OutData.Notes);
	}
	return OutErrors.Num() == ErrorsBefore;
}

void FCNAJsonImporter::ReadData(const TSharedPtr<FJsonObject>& Root, FCNAImportData& Data, TArray<FText>& OutErrors, bool bHeaderOnly)
{
	using namespace CNAJsonFields;
	using namespace CNAJsonImporterLocal;

	FGuid::Parse(FCNAJsonSchema::GetString(Root, Id), Data.Id);
	Data.Name = FCNAJsonSchema::GetString(Root, Name).TrimStartAndEnd();
	Data.DisplayName = ReadText(Root, DisplayName, Data.DisplayName);
	Data.Subtitle = ReadText(Root, Subtitle, Data.Subtitle);
	Data.Tooltip = ReadText(Root, Tooltip, Data.Tooltip);
	Data.Category = ReadText(Root, Category, Data.Category);
	Data.Keywords = ReadText(Root, Keywords, Data.Keywords);
	Data.DefaultComment = ReadText(Root, DefaultComment, Data.DefaultComment);

	const TSharedPtr<FJsonObject> MetaObject = FCNAJsonSchema::GetObject(Root, Meta);
	Data.Author = FCNAJsonSchema::GetString(MetaObject, Author);
	Data.Description = FCNAJsonSchema::GetString(MetaObject, Description);
	FCNAJsonSchema::JsonToStrings(FCNAJsonSchema::GetField(MetaObject, Tags), Data.Tags);
	Data.SourceUrl = FCNAJsonSchema::GetString(MetaObject, SourceUrl);

	// Flags that the catalog shows without a full import.
	const TSharedPtr<FJsonObject> FlagsObject = FCNAJsonSchema::GetObject(Root, Flags);
	Data.bPure = FCNAJsonSchema::GetBool(FlagsObject, Pure, Data.bPure);
	Data.bDeprecated = FCNAJsonSchema::GetBool(FlagsObject, Deprecated, Data.bDeprecated);

	const TSharedPtr<FJsonObject> GraphObject = FCNAJsonSchema::GetObject(Root, Graph);
	for (const TSharedPtr<FJsonObject>& PinObject : ReadObjects(GraphObject, Signature))
	{
		FCNAImportedPin& Pin = Data.Signature[Data.Signature.AddDefaulted()];
		Pin.Name = FName(*FCNAJsonSchema::GetString(PinObject, Name));
		Pin.Direction = FCNAJsonSchema::GetString(PinObject, Direction).Equals(TEXT("Output"), ESearchCase::IgnoreCase) ? EGPD_Output : EGPD_Input;
		Pin.Type.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		if (!bHeaderOnly)
		{
			ReadPinType(PinObject, FString::Printf(TEXT("graph.signature.%s"), *Pin.Name.ToString()), Pin.Type, Pin.MissingObject, OutErrors);
		}
	}
	if (bHeaderOnly)
	{
		return;
	}

	// appearance
	const TSharedPtr<FJsonObject> AppearanceObject = FCNAJsonSchema::GetObject(Root, Appearance);
	const FString AppearancePath(Appearance);
	Data.bUseHeaderColor = FCNAJsonSchema::GetBool(AppearanceObject, UseHeaderColor, Data.bUseHeaderColor);
	ReadColor(AppearanceObject, HeaderColor, AppearancePath, Data.HeaderColor, Data);
	ReadColor(AppearanceObject, HeaderTextColor, AppearancePath, Data.HeaderTextColor, Data);
	ReadColor(AppearanceObject, PinLabelColor, AppearancePath, Data.PinLabelColor, Data);
	Data.Icon = ReadName(AppearanceObject, Icon, Data.Icon);
	ReadColor(AppearanceObject, IconTint, AppearancePath, Data.IconTint, Data);
	ReadColor(AppearanceObject, IconTextureTint, AppearancePath, Data.IconTextureTint, Data);
	ReadEnum(AppearanceObject, CornerIcon, AppearancePath, &FCNAJsonSchema::StringToCornerIcon, Data.CornerIcon, Data);
	ReadColor(AppearanceObject, CornerIconTextureTint, AppearancePath, Data.CornerIconTextureTint, Data);
	Data.CornerIconTextureSize = FMath::Clamp(static_cast<float>(FCNAJsonSchema::GetNumber(AppearanceObject, CornerIconTextureSize, Data.CornerIconTextureSize)), 8.0f, 128.0f);
	Data.MinWidth = FMath::Clamp(static_cast<float>(FCNAJsonSchema::GetNumber(AppearanceObject, MinWidth, Data.MinWidth)), 0.0f, 1200.0f);
	Data.MinHeight = FMath::Clamp(static_cast<float>(FCNAJsonSchema::GetNumber(AppearanceObject, MinHeight, Data.MinHeight)), 0.0f, 1200.0f);
	Data.bColorWires = FCNAJsonSchema::GetBool(AppearanceObject, ColorWires, Data.bColorWires);
	Data.bCompact = FCNAJsonSchema::GetBool(AppearanceObject, Compact, Data.bCompact);
	Data.CompactTitle = ReadText(AppearanceObject, CompactTitle, Data.CompactTitle);

	// Items 3 and 6: the texture must be under an allowed root; a missing one falls back to the default icon.
	TArray<FString> TexturePaths;
	const FString IconTexturePath = FCNAJsonSchema::GetString(AppearanceObject, IconTexture).TrimStartAndEnd();
	const FString CornerTexturePath = FCNAJsonSchema::GetString(AppearanceObject, CornerIconTexture).TrimStartAndEnd();
	if (!CornerTexturePath.IsEmpty()) TexturePaths.AddUnique(CornerTexturePath);
	if (!IconTexturePath.IsEmpty())
	{
		TexturePaths.Add(IconTexturePath);
	}
	FCNAJsonSchema::JsonToStrings(FCNAJsonSchema::GetField(Root, Textures), Data.Textures);
	for (const FString& Path : Data.Textures)
	{
		TexturePaths.AddUnique(Path.TrimStartAndEnd());
	}
	for (const FString& Path : TexturePaths)
	{
		if (Path.IsEmpty())
		{
			continue;
		}
		bool bForbidden = false;
		UTexture2D* Texture = Cast<UTexture2D>(FCNAJsonSchema::ResolveObjectPath(Path, UTexture2D::StaticClass(), bForbidden));
		if (bForbidden)
		{
			OutErrors.Add(ForbiddenPathError(TEXT("textures"), Path));
			continue;
		}
		if (!Texture)
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("Texture"), FText::FromString(Path));
			AddNote(Data.Notes, EMessageSeverity::Warning, FCNAMessages::Format(ECNAMessage::CNA019, Arguments));
		}
		else
		{
			if (Path == IconTexturePath) Data.IconTexture = FSoftObjectPath(Texture);
			if (Path == CornerTexturePath) Data.CornerIconTexture = FSoftObjectPath(Texture);
		}
	}

	for (const TSharedPtr<FJsonObject>& BadgeObject : ReadObjects(AppearanceObject, Badges))
	{
		FCNABadge& Badge = Data.Badges[Data.Badges.AddDefaulted()];
		Badge.Text = ReadText(BadgeObject, Text, FText::GetEmpty());
		ReadColor(BadgeObject, Color, TEXT("appearance.badges"), Badge.Color, Data);
		ReadColor(BadgeObject, TextColor, TEXT("appearance.badges"), Badge.TextColor, Data);
		ReadEnum(BadgeObject, Corner, TEXT("appearance.badges"), &FCNAJsonSchema::StringToBadgeCorner, Badge.Corner, Data);
	}
	for (const TSharedPtr<FJsonObject>& WidgetObject : ReadObjects(AppearanceObject, BodyWidgets))
	{
		FCNABodyWidget& Widget = Data.BodyWidgets[Data.BodyWidgets.AddDefaulted()];
		Widget.PinName = ReadName(WidgetObject, PinName, NAME_None);
		ReadEnum(WidgetObject, Type, TEXT("appearance.bodyWidgets"), &FCNAJsonSchema::StringToBodyWidgetType, Widget.Type, Data);
		FCNAJsonSchema::JsonToStrings(FCNAJsonSchema::GetField(WidgetObject, Options), Widget.Options);
		Widget.Label = ReadText(WidgetObject, Label, FText::GetEmpty());
		Widget.Width = FMath::Clamp(static_cast<float>(FCNAJsonSchema::GetNumber(WidgetObject, Width, 0.0)), 0.0f, 600.0f);
		Widget.Height = FMath::Clamp(static_cast<float>(FCNAJsonSchema::GetNumber(WidgetObject, Height, 0.0)), 0.0f, 600.0f);
		Widget.SliderMin = FMath::Clamp(static_cast<float>(FCNAJsonSchema::GetNumber(WidgetObject, SliderMin, 0.0)), -1000000.0f, 1000000.0f);
		Widget.SliderMax = FMath::Clamp(static_cast<float>(FCNAJsonSchema::GetNumber(WidgetObject, SliderMax, 1.0)), -1000000.0f, 1000000.0f);
	}

	// flags
	Data.bDevelopmentOnly = FCNAJsonSchema::GetBool(FlagsObject, DevelopmentOnly, Data.bDevelopmentOnly);
	Data.DeprecationMessage = ReadText(FlagsObject, DeprecationMessage, Data.DeprecationMessage);
	if (FlagsObject.IsValid() && FlagsObject->HasField(AllowedGraphs))
	{
		Data.AllowedGraphs = FCNAJsonSchema::JsonToGraphKinds(FCNAJsonSchema::GetField(FlagsObject, AllowedGraphs));
	}
	Data.ContextClass = ReadClass(FlagsObject, ContextClass, TEXT("flags"), Data, OutErrors);
	Data.bHiddenInMenu = FCNAJsonSchema::GetBool(FlagsObject, HiddenInMenu, Data.bHiddenInMenu);
	Data.bAllowInstanceLabel = FCNAJsonSchema::GetBool(FlagsObject, AllowInstanceLabel, Data.bAllowInstanceLabel);

	// pins
	for (const TSharedPtr<FJsonObject>& OverrideObject : ReadObjects(Root, PinOverrides))
	{
		FCNAPinOverride& Override = Data.PinOverrides[Data.PinOverrides.AddDefaulted()];
		const FString Owner = FString::Printf(TEXT("pinOverrides.%s"), *FCNAJsonSchema::GetString(OverrideObject, PinName));
		Override.PinName = ReadName(OverrideObject, PinName, NAME_None);
		Override.DisplayName = ReadText(OverrideObject, DisplayName, FText::GetEmpty());
		Override.Tooltip = ReadText(OverrideObject, Tooltip, FText::GetEmpty());
		Override.bAdvanced = FCNAJsonSchema::GetBool(OverrideObject, Advanced, Override.bAdvanced);
		Override.bHiddenByDefault = FCNAJsonSchema::GetBool(OverrideObject, HiddenByDefault, Override.bHiddenByDefault);
		Override.bRequired = FCNAJsonSchema::GetBool(OverrideObject, Required, Override.bRequired);
		Override.bDefaultToSelf = FCNAJsonSchema::GetBool(OverrideObject, DefaultToSelf, Override.bDefaultToSelf);
		Override.PassThroughOf = ReadName(OverrideObject, PassThroughOf, NAME_None);
		Override.bDynamicGroup = FCNAJsonSchema::GetBool(OverrideObject, DynamicGroup, Override.bDynamicGroup);
		Override.DynamicMin = ReadInt(OverrideObject, DynamicMin, Override.DynamicMin, 0, 64);
		Override.DynamicMax = ReadInt(OverrideObject, DynamicMax, Override.DynamicMax, 1, 64);
		Override.DynamicNameFormat = ReadText(OverrideObject, DynamicNameFormat, FText::GetEmpty());
		ReadEnum(OverrideObject, ConnectionRule, Owner, &FCNAJsonSchema::StringToConnectionRule, Override.ConnectionRule, Data);
		Override.ClassFilter = ReadClass(OverrideObject, ClassFilter, Owner, Data, OutErrors);
		Override.bOverridePinColor = FCNAJsonSchema::GetBool(OverrideObject, OverridePinColor, Override.bOverridePinColor);
		ReadColor(OverrideObject, PinColor, Owner, Override.PinColor, Data);
		Override.bOverrideLabelColor = FCNAJsonSchema::GetBool(OverrideObject, OverrideLabelColor, Override.bOverrideLabelColor);
		ReadColor(OverrideObject, LabelColor, Owner, Override.LabelColor, Data);
	}
	for (const TSharedPtr<FJsonObject>& RedirectObject : ReadObjects(Root, PinRedirects))
	{
		Data.PinRedirects.Add(FCNAPinRedirect(ReadName(RedirectObject, OldName, NAME_None), ReadName(RedirectObject, NewName, NAME_None)));
	}

	// Local variables keep their GUID: Get Local and Set Local in the T3D refer to it (spec 4.8).
	for (const TSharedPtr<FJsonObject>& VariableObject : ReadObjects(Root, LocalVariables))
	{
		FCNALocalVariable& Variable = Data.LocalVariables[Data.LocalVariables.AddDefaulted()];
		if (!FGuid::Parse(FCNAJsonSchema::GetString(VariableObject, Guid), Variable.Guid) || !Variable.Guid.IsValid())
		{
			Variable.Guid = FGuid::NewGuid();
		}
		Variable.Name = ReadName(VariableObject, Name, NAME_None);
		FString Missing;
		ReadPinType(VariableObject, FString::Printf(TEXT("localVariables.%s"), *Variable.Name.ToString()), Variable.Type, Missing, OutErrors);
		if (!Missing.IsEmpty())
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("Type"), FText::FromString(Missing));
			Arguments.Add(TEXT("Pin"), FText::FromName(Variable.Name));
			AddNote(Data.Notes, EMessageSeverity::Error, FCNAMessages::Format(ECNAMessage::CNA010, Arguments));
		}
		Variable.DefaultValue = FCNAJsonSchema::GetString(VariableObject, DefaultValue);
		Variable.Tooltip = ReadText(VariableObject, Tooltip, FText::GetEmpty());
	}

	// graphs
	Data.ExecuteT3D = FCNAJsonSchema::JoinLines(FCNAJsonSchema::GetField(GraphObject, T3D));
	Data.BehaviorT3D = FCNAJsonSchema::JoinLines(FCNAJsonSchema::GetField(FCNAJsonSchema::GetObject(Root, Behavior), T3D));
}

bool FCNAJsonImporter::CheckT3D(const FString& T3D, bool bRequireTunnels, const FText& GraphLabel, const FString& SourceAssetName,
	TArray<FText>& OutErrors, TArray<FCNAImportNote>& OutNotes)
{
	using namespace CNAJsonImporterLocal;
	const int32 ErrorsBefore = OutErrors.Num();

	if (T3D.TrimStartAndEnd().IsEmpty())
	{
		if (bRequireTunnels)
		{
			OutErrors.Add(FText::Format(LOCTEXT("Import.EmptyExecute", "{0}: the graph is empty, the Tunnel nodes Inputs and Outputs are missing (CNA005)."), GraphLabel));
		}
		return OutErrors.Num() == ErrorsBefore;
	}

	TArray<FT3DObject> Objects;
	if (!ParseT3DObjects(T3D, Objects))
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.Unbalanced", "{0}: Begin Object and End Object do not match, the text is damaged."), GraphLabel));
		return false;
	}

	// Item 5: every class must exist, otherwise no graph is imported. Item 7: only nodes and their graphs.
	TArray<FString> MissingClasses;
	TArray<FString> RefusedClasses;
	int32 TopLevelTunnels = 0;
	for (const FT3DObject& Object : Objects)
	{
		if (Object.ClassName.IsEmpty())
		{
			if (Object.Depth == 0)
			{
				OutErrors.Add(FText::Format(LOCTEXT("Import.NoClass", "{0}: object {1} has no class, the text is damaged."), GraphLabel, FText::FromString(Object.Name)));
			}
			continue;
		}
		UClass* Class = FCNAJsonSchema::FindClassByT3DName(Object.ClassName);
		if (!Class)
		{
			MissingClasses.AddUnique(Object.ClassName);
		}
		else if (Object.Depth == 0 ? !IsAllowedNodeClass(Class) : !IsAllowedNestedClass(Class))
		{
			RefusedClasses.AddUnique(Class->GetPathName());
		}
		else if (Object.Depth == 0 && Class == UK2Node_Tunnel::StaticClass())
		{
			++TopLevelTunnels;
		}
	}
	if (MissingClasses.Num() > 0)
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.MissingNodeClasses", "{0}: the node classes {1} do not exist in this project (a plugin or module is missing). The graphs are not imported and the asset is not changed."),
			GraphLabel, FText::FromString(FCNAGraphUtils::JoinNames(MissingClasses, 12))));
	}
	if (RefusedClasses.Num() > 0)
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.RefusedClasses", "{0}: objects of the classes {1} are not graph nodes and are not created by the import."),
			GraphLabel, FText::FromString(FCNAGraphUtils::JoinNames(RefusedClasses, 12))));
	}
	if (bRequireTunnels && TopLevelTunnels < 2)
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.NoTunnels", "{0}: the Tunnel nodes Inputs and Outputs are missing (CNA005)."), GraphLabel));
	}

	// Item 3: every object path must be under an allowed root. Missing objects are reported, not refused: the
	// nodes that use them show the usual compiler errors.
	TArray<FString> Paths;
	FCNAJsonSchema::FindObjectPathsInText(T3D, Paths);
	TArray<FString> Forbidden;
	TArray<FString> Missing;
	for (const FString& Path : Paths)
	{
		if (!LooksLikeObjectPath(Path))
		{
			continue;
		}
		if (!FCNAJsonSchema::IsAllowedObjectPath(Path))
		{
			Forbidden.AddUnique(Path);
			continue;
		}
		const FString PackageName = FPackageName::ObjectPathToPackageName(Path);
		const bool bOwnPackage = !SourceAssetName.IsEmpty() && FPackageName::GetShortName(PackageName).Equals(SourceAssetName, ESearchCase::IgnoreCase);
		if (!bOwnPackage && !DoesObjectExist(Path))
		{
			Missing.AddUnique(Path);
		}
	}
	if (Forbidden.Num() > 0)
	{
		OutErrors.Add(ForbiddenPathError(GraphLabel.ToString(), FCNAGraphUtils::JoinNames(Forbidden, 8)));
	}
	if (Missing.Num() > 0)
	{
		AddNote(OutNotes, EMessageSeverity::Warning, FText::Format(LOCTEXT("Import.MissingObjects", "{0}: referenced objects were not found: {1}. The nodes that use them report errors when the asset is compiled."),
			GraphLabel, FText::FromString(FCNAGraphUtils::JoinNames(Missing, 8))));
	}
	return OutErrors.Num() == ErrorsBefore;
}

int32 FCNAJsonImporter::CountT3DNodes(const FString& T3D)
{
	TArray<CNAJsonImporterLocal::FT3DObject> Objects;
	CNAJsonImporterLocal::ParseT3DObjects(T3D, Objects);
	int32 Count = 0;
	for (const CNAJsonImporterLocal::FT3DObject& Object : Objects)
	{
		Count += Object.Depth == 0 ? 1 : 0;
	}
	return Count;
}

// Changes (spec 8.2, item 8)

void FCNAJsonImporter::CollectChanges(const UCustomNodeAsset* Asset, const FCNAImportData& Data, TArray<FText>& OutPinChanges, TArray<FText>& OutPropertyChanges)
{
	using namespace CNAJsonImporterLocal;
	if (!Asset)
	{
		return;
	}

	// Pins by name and direction, in the order of the file.
	TArray<FCNASignaturePin> Current;
	FCNAGraphUtils::GetSignature(Asset, Current);
	for (const FCNAImportedPin& Pin : Data.Signature)
	{
		const FCNASignaturePin* Old = FCNAGraphUtils::FindSignaturePin(Current, Pin.Name, Pin.Direction);
		if (!Old)
		{
			OutPinChanges.Add(FText::Format(LOCTEXT("Import.PinAdded", "+ {0} {1} ({2})"), DirectionText(Pin.Direction), FText::FromName(Pin.Name), FCNAPinUtils::GetTypeText(Pin.Type)));
		}
		else if (!FCNAPinUtils::AreTypesIdentical(Old->Type, Pin.Type) || Old->Type.bIsReference != Pin.Type.bIsReference)
		{
			OutPinChanges.Add(FText::Format(LOCTEXT("Import.PinChanged", "~ {0} {1}: {2} -> {3}"), DirectionText(Pin.Direction), FText::FromName(Pin.Name),
				FCNAPinUtils::GetTypeText(Old->Type), FCNAPinUtils::GetTypeText(Pin.Type)));
		}
	}
	for (const FCNASignaturePin& Old : Current)
	{
		if (!Data.FindSignaturePin(Old.Name, Old.Direction))
		{
			OutPinChanges.Add(FText::Format(LOCTEXT("Import.PinRemoved", "- {0} {1} ({2})"), DirectionText(Old.Direction), FText::FromName(Old.Name), FCNAPinUtils::GetTypeText(Old.Type)));
		}
	}
	for (const EEdGraphPinDirection Direction : { EGPD_Input, EGPD_Output })
	{
		TArray<FName> OldOrder;
		TArray<FName> NewOrder;
		for (const FCNASignaturePin& Old : Current)
		{
			if (Old.Direction == Direction && Data.FindSignaturePin(Old.Name, Direction))
			{
				OldOrder.Add(Old.Name);
			}
		}
		for (const FCNAImportedPin& Pin : Data.Signature)
		{
			if (Pin.Direction == Direction && FCNAGraphUtils::FindSignaturePin(Current, Pin.Name, Direction))
			{
				NewOrder.Add(Pin.Name);
			}
		}
		if (OldOrder != NewOrder)
		{
			OutPinChanges.Add(FText::Format(LOCTEXT("Import.PinOrder", "~ order of the {0} pins"), DirectionText(Direction)));
		}
	}

	// Properties in the order of the Details panel.
	FChangeList Changes(OutPropertyChanges);
	if (!Data.Name.IsEmpty() && !Data.Name.Equals(Asset->GetName(), ESearchCase::CaseSensitive))
	{
		OutPropertyChanges.Add(FText::Format(LOCTEXT("Import.OtherName", "The file was exported from {0}; the asset keeps its name {1} and its id."),
			FText::FromString(Data.Name), FText::FromString(Asset->GetName())));
	}
	Changes.Text(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, DisplayName), Asset->DisplayName, Data.DisplayName);
	Changes.Text(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Subtitle), Asset->Subtitle, Data.Subtitle);
	Changes.Text(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Tooltip), Asset->Tooltip, Data.Tooltip);
	Changes.Text(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Category), Asset->Category, Data.Category);
	Changes.Text(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Keywords), Asset->Keywords, Data.Keywords);
	Changes.Bool(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bHiddenInMenu), Asset->bHiddenInMenu, Data.bHiddenInMenu);
	Changes.Text(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, DefaultComment), Asset->DefaultComment, Data.DefaultComment);
	Changes.Bool(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bAllowInstanceLabel), Asset->bAllowInstanceLabel, Data.bAllowInstanceLabel);

	Changes.Bool(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bUseHeaderColor), Asset->bUseHeaderColor, Data.bUseHeaderColor);
	Changes.Color(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, HeaderColor), Asset->HeaderColor, Data.HeaderColor);
	Changes.Color(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, HeaderTextColor), Asset->HeaderTextColor, Data.HeaderTextColor);
	Changes.Color(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinLabelColor), Asset->PinLabelColor, Data.PinLabelColor);
	Changes.String(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Icon), Asset->Icon.IsNone() ? FString() : Asset->Icon.ToString(), Data.Icon.IsNone() ? FString() : Data.Icon.ToString());
	Changes.Color(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, IconTint), Asset->IconTint, Data.IconTint);
	Changes.String(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, IconTexture), Asset->IconTexture.ToSoftObjectPath().ToString(), Data.IconTexture.ToString());
	Changes.Color(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, IconTextureTint), Asset->IconTextureTint, Data.IconTextureTint);
	Changes.String(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIconTexture), Asset->CornerIconTexture.ToSoftObjectPath().ToString(), Data.CornerIconTexture.ToString());
	Changes.Color(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIconTextureTint), Asset->CornerIconTextureTint, Data.CornerIconTextureTint);
	Changes.Number(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIconTextureSize), Asset->CornerIconTextureSize, Data.CornerIconTextureSize);
	Changes.String(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIcon), FCNAJsonSchema::CornerIconToString(Asset->CornerIcon), FCNAJsonSchema::CornerIconToString(Data.CornerIcon));
	Changes.Bool(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bCompact), Asset->bCompact, Data.bCompact);
	Changes.Text(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CompactTitle), Asset->CompactTitle, Data.CompactTitle);
	Changes.Structs(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Badges), Asset->Badges, Data.Badges);
	Changes.Number(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, MinWidth), Asset->MinWidth, Data.MinWidth);
	Changes.Number(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, MinHeight), Asset->MinHeight, Data.MinHeight);
	Changes.Structs(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, BodyWidgets), Asset->BodyWidgets, Data.BodyWidgets);
	Changes.Bool(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bColorWires), Asset->bColorWires, Data.bColorWires);

	Changes.Bool(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bPure), Asset->bPure, Data.bPure);
	Changes.Bool(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bDevelopmentOnly), Asset->bDevelopmentOnly, Data.bDevelopmentOnly);
	Changes.Bool(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bDeprecated), Asset->bDeprecated, Data.bDeprecated);
	Changes.Text(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, DeprecationMessage), Asset->DeprecationMessage, Data.DeprecationMessage);
	if (Asset->AllowedGraphs != Data.AllowedGraphs)
	{
		Changes.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, AllowedGraphs), CNATypes::DescribeGraphKinds(Asset->AllowedGraphs).ToString(), CNATypes::DescribeGraphKinds(Data.AllowedGraphs).ToString());
	}
	Changes.String(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, ContextClass), Asset->ContextClass ? Asset->ContextClass->GetName() : FString(), Data.ContextClass ? Data.ContextClass->GetName() : FString());

	// Pin overrides and local variables by name, the other arrays as a whole.
	for (const FCNAPinOverride& Override : Data.PinOverrides)
	{
		const FCNAPinOverride* Old = Asset->FindPinOverride(Override.PinName);
		if (!Old)
		{
			OutPropertyChanges.Add(FText::Format(LOCTEXT("Import.OverrideAdded", "+ pin override {0}"), FText::FromName(Override.PinName)));
		}
		else if (!FCNAPinOverride::StaticStruct()->CompareScriptStruct(Old, &Override, PPF_None))
		{
			OutPropertyChanges.Add(FText::Format(LOCTEXT("Import.OverrideChanged", "~ pin override {0}"), FText::FromName(Override.PinName)));
		}
	}
	for (const FCNAPinOverride& Old : Asset->PinOverrides)
	{
		if (!Data.PinOverrides.ContainsByPredicate([&Old](const FCNAPinOverride& New) { return New.PinName == Old.PinName; }))
		{
			OutPropertyChanges.Add(FText::Format(LOCTEXT("Import.OverrideRemoved", "- pin override {0}"), FText::FromName(Old.PinName)));
		}
	}
	Changes.Structs(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinRedirects), Asset->PinRedirects, Data.PinRedirects);
	for (const FCNALocalVariable& Variable : Data.LocalVariables)
	{
		const FCNALocalVariable* Old = Asset->FindLocalVariableByName(Variable.Name);
		if (!Old)
		{
			OutPropertyChanges.Add(FText::Format(LOCTEXT("Import.VariableAdded", "+ local variable {0} ({1})"), FText::FromName(Variable.Name), FCNAPinUtils::GetTypeText(Variable.Type)));
		}
		else if (!FCNAPinUtils::AreTypesIdentical(Old->Type, Variable.Type) || Old->DefaultValue != Variable.DefaultValue || !AreTextsEqual(Old->Tooltip, Variable.Tooltip))
		{
			OutPropertyChanges.Add(FText::Format(LOCTEXT("Import.VariableChanged", "~ local variable {0}"), FText::FromName(Variable.Name)));
		}
	}
	for (const FCNALocalVariable& Old : Asset->LocalVariables)
	{
		if (!Data.LocalVariables.ContainsByPredicate([&Old](const FCNALocalVariable& New) { return New.Name == Old.Name; }))
		{
			OutPropertyChanges.Add(FText::Format(LOCTEXT("Import.VariableRemoved", "- local variable {0}"), FText::FromName(Old.Name)));
		}
	}

	Changes.String(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Author), Asset->Author, Data.Author);
	Changes.String(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Description), Asset->Description, Data.Description);
	if (Asset->Tags != Data.Tags)
	{
		Changes.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Tags), FString::Join(Asset->Tags, TEXT(", ")), FString::Join(Data.Tags, TEXT(", ")));
	}
	Changes.String(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, SourceUrl), Asset->SourceUrl, Data.SourceUrl);

	// Graphs: the T3D of the asset against the file.
	const FString CurrentExecute = FCNAJsonExporter::ExportGraphToT3D(Asset->FindExecuteGraph());
	if (!IsSameT3D(CurrentExecute, Data.ExecuteT3D))
	{
		OutPropertyChanges.Add(FText::Format(LOCTEXT("Import.ExecuteChanged", "Execution Graph: {0} -> {1} nodes, replaced"), CountT3DNodes(CurrentExecute), CountT3DNodes(Data.ExecuteT3D)));
	}
	const FString CurrentBehavior = FCNAJsonExporter::ExportGraphToT3D(Asset->FindBehaviorGraph());
	if (!IsSameT3D(CurrentBehavior, Data.BehaviorT3D))
	{
		OutPropertyChanges.Add(FText::Format(LOCTEXT("Import.BehaviorChanged", "Editor Behavior Graph: {0} -> {1} nodes, replaced"), CountT3DNodes(CurrentBehavior), CountT3DNodes(Data.BehaviorT3D)));
	}
}

FText FCNAJsonImporter::BuildChangeSummary(const UCustomNodeAsset* Asset, const FCNAImportData& Data, int32 MaxLinesPerSection)
{
	using namespace CNAJsonImporterLocal;
	TArray<FText> PinChanges;
	TArray<FText> PropertyChanges;
	CollectChanges(Asset, Data, PinChanges, PropertyChanges);
	if (PinChanges.Num() == 0 && PropertyChanges.Num() == 0)
	{
		return FText::GetEmpty();
	}

	FString Summary;
	if (PinChanges.Num() > 0)
	{
		Summary += FText::Format(LOCTEXT("Import.PinSection", "Pins ({0}):"), PinChanges.Num()).ToString() + TEXT("\n");
		Summary += JoinTexts(PinChanges, MaxLinesPerSection).ToString() + TEXT("\n\n");
	}
	if (PropertyChanges.Num() > 0)
	{
		Summary += FText::Format(LOCTEXT("Import.PropertySection", "Properties ({0}):"), PropertyChanges.Num()).ToString() + TEXT("\n");
		Summary += JoinTexts(PropertyChanges, MaxLinesPerSection).ToString();
	}
	Summary.TrimEndInline();
	return FText::FromString(Summary);
}

// Writing

bool FCNAJsonImporter::ApplyToAsset(UCustomNodeAsset* Asset, const FCNAImportData& Data, bool bNewAsset, TArray<FText>& OutErrors)
{
	using namespace CNAJsonImporterLocal;
	if (!Asset)
	{
		OutErrors.Add(LOCTEXT("Import.NoAsset", "No asset to import into."));
		return false;
	}

	TArray<FCNAImportNote> Notes = Data.Notes;
	bool bApplied = false;
	{
		// Item 8: one Undo step. A new asset records nothing; it is deleted when the import fails.
		FScopedTransaction Transaction(FText::Format(LOCTEXT("Import.Transaction", "Import {0} from JSON"), FText::FromString(Asset->GetName())), !bNewAsset);
		Asset->Modify();

		if (bNewAsset && Data.Id.IsValid())
		{
			if (IsGuidUsedByOtherAsset(Data.Id, Asset))
			{
				AddNote(Notes, EMessageSeverity::Info, FText::Format(LOCTEXT("Import.IdInUse", "Id {0} of the file is already used by another asset; {1} gets a new id."),
					FText::FromString(Data.Id.ToString(EGuidFormats::DigitsWithHyphens)), FText::FromString(Asset->GetName())));
			}
			else
			{
				Asset->AssetGuid = Data.Id;
			}
		}

		Asset->DisplayName = Data.DisplayName;
		Asset->Subtitle = Data.Subtitle;
		Asset->Tooltip = Data.Tooltip;
		Asset->Category = Data.Category;
		Asset->Keywords = Data.Keywords;
		Asset->bHiddenInMenu = Data.bHiddenInMenu;
		Asset->DefaultComment = Data.DefaultComment;
		Asset->bAllowInstanceLabel = Data.bAllowInstanceLabel;
		Asset->bUseHeaderColor = Data.bUseHeaderColor;
		Asset->HeaderColor = Data.HeaderColor;
		Asset->HeaderTextColor = Data.HeaderTextColor;
		Asset->PinLabelColor = Data.PinLabelColor;
		Asset->Icon = Data.Icon;
		Asset->IconTint = Data.IconTint;
		Asset->IconTexture = TSoftObjectPtr<UTexture2D>(Data.IconTexture);
		Asset->IconTextureTint = Data.IconTextureTint;
		Asset->CornerIcon = Data.CornerIcon;
		Asset->CornerIconTexture = TSoftObjectPtr<UTexture2D>(Data.CornerIconTexture);
		Asset->CornerIconTextureTint = Data.CornerIconTextureTint;
		Asset->CornerIconTextureSize = Data.CornerIconTextureSize;
		Asset->bCompact = Data.bCompact;
		Asset->CompactTitle = Data.CompactTitle;
		Asset->Badges = Data.Badges;
		Asset->MinWidth = Data.MinWidth;
		Asset->MinHeight = Data.MinHeight;
		Asset->BodyWidgets = Data.BodyWidgets;
		Asset->bColorWires = Data.bColorWires;
		Asset->bPure = Data.bPure;
		Asset->bDevelopmentOnly = Data.bDevelopmentOnly;
		Asset->bDeprecated = Data.bDeprecated;
		Asset->DeprecationMessage = Data.DeprecationMessage;
		Asset->AllowedGraphs = Data.AllowedGraphs;
		Asset->ContextClass = Data.ContextClass;
		Asset->PinOverrides = Data.PinOverrides;
		Asset->PinRedirects = Data.PinRedirects;
		Asset->Author = Data.Author;
		Asset->Description = Data.Description;
		Asset->Tags = Data.Tags;
		Asset->SourceUrl = Data.SourceUrl;

		// Variables before the graphs: Get Local and Set Local read their type while they are pasted.
		Asset->LocalVariables = Data.LocalVariables;
		Asset->EnsureUniqueLocalVariableGuids();

		// An asset without its graphs (made by hand or damaged) gets them first, then their nodes are replaced.
		UCNAAssetFactory::CreateDefaultGraphs(Asset, /*bWithDefaultPins*/ false);
		Asset->RefreshGraphPointers();

		bApplied = ReplaceGraphNodes(Asset, Asset->FindExecuteGraph(), Data.ExecuteT3D, OutErrors)
			&& ReplaceGraphNodes(Asset, Asset->FindBehaviorGraph(), Data.BehaviorT3D, OutErrors);
		if (bApplied && (!FCNAGraphUtils::FindEntryTunnel(Asset->FindExecuteGraph()) || !FCNAGraphUtils::FindExitTunnel(Asset->FindExecuteGraph())))
		{
			// V18: the text was accepted but the Tunnel nodes were not created.
			OutErrors.Add(FCNAMessages::Format(ECNAMessage::CNA005, FCNAMessages::AssetArgs(Asset)));
			bApplied = false;
		}
		if (bApplied)
		{
			FixupMissingPinTypes(Asset, Data, Notes);
			Asset->RefreshGraphPointers();
			FCNAGraphUtils::RefreshCachedAssetData(Asset);
			if (bNewAsset)
			{
				// The first save must not count as a change of the pin structure.
				Asset->SignatureHash = FCNAGraphUtils::ComputeSignatureHash(Asset);
			}
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Asset);
		}
	}

	if (!bApplied)
	{
		if (!bNewAsset && GEditor)
		{
			// The checks before the transaction make this rare; Undo brings back the previous state.
			GEditor->UndoTransaction();
		}
		return false;
	}

	FKismetEditorUtilities::CompileBlueprint(Asset);
	for (const FCNAImportNote& Note : Notes)
	{
		FCNAMessages::ReportTextToLogWithSeverity(Note.Severity, Note.Text, Asset);
	}
	Asset->NotifyAssetChanged(NAME_None);
	Asset->MarkPackageDirty();
	UE_LOG(LogNodeFactory, Log, TEXT("%s imported from %s (%d notes)."), *Asset->GetPathName(),
		Data.SourceFile.IsEmpty() ? TEXT("text") : *Data.SourceFile, Notes.Num());
	return true;
}

bool FCNAJsonImporter::ReplaceGraphNodes(UCustomNodeAsset* Asset, UEdGraph* Graph, const FString& T3D, TArray<FText>& OutErrors)
{
	using namespace CNAJsonImporterLocal;
	if (!Graph)
	{
		OutErrors.Add(FCNAMessages::Format(ECNAMessage::CNA005, FCNAMessages::AssetArgs(Asset)));
		return false;
	}

	Graph->Modify();

	// Names of everything inside the graph stay reserved, including the nodes removed below.
	TSet<FName> Reserved;
	TArray<UObject*> Inner;
	GetObjectsWithOuter(Graph, Inner, /*bIncludeNestedObjects*/ false);
	for (const UObject* Object : Inner)
	{
		Reserved.Add(Object->GetFName());
	}

	const TArray<UEdGraphNode*> OldNodes = Graph->Nodes;
	for (UEdGraphNode* Node : OldNodes)
	{
		if (Node)
		{
			FBlueprintEditorUtils::RemoveNode(Asset, Node, /*bDontRecompile*/ true);
		}
	}
	if (T3D.TrimStartAndEnd().IsEmpty())
	{
		return true;
	}

	// V18: own text factory instead of FEdGraphUtilities::ImportNodesFromText, so the Tunnel nodes are created
	// as well; links, PostPasteNode and ReconstructNode are handled by the engine afterwards.
	FCNAGraphTextFactory Factory(Graph);
	Factory.ProcessBuffer(Graph, RF_Transactional, MakeNodeNamesUnique(T3D, Graph, Reserved));
	FEdGraphUtilities::PostProcessPastedNodes(Factory.SpawnedNodes);

	const int32 Expected = CountT3DNodes(T3D);
	if (Factory.SpawnedNodes.Num() != Expected)
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.NodesMissing", "Graph {0}: {1} of {2} nodes could be created from the T3D text."),
			FText::FromString(Graph->GetName()), Factory.SpawnedNodes.Num(), Expected));
		return false;
	}
	Graph->NotifyGraphChanged();
	return true;
}

void FCNAJsonImporter::FixupMissingPinTypes(UCustomNodeAsset* Asset, const FCNAImportData& Data, TArray<FCNAImportNote>& OutNotes)
{
	// Item 4: a pin whose class, struct or enum is missing becomes Wildcard (CNA010).
	UEdGraph* Graph = Asset->FindExecuteGraph();
	UK2Node_Tunnel* Tunnels[2] = { FCNAGraphUtils::FindEntryTunnel(Graph), FCNAGraphUtils::FindExitTunnel(Graph) };
	for (int32 TunnelIndex = 0; TunnelIndex < 2; ++TunnelIndex)
	{
		UK2Node_Tunnel* Tunnel = Tunnels[TunnelIndex];
		if (!Tunnel)
		{
			continue;
		}
		// Outputs of Tunnel Input are inputs of the custom node.
		const EEdGraphPinDirection NodeDirection = TunnelIndex == 0 ? EGPD_Input : EGPD_Output;
		bool bChanged = false;
		for (TSharedPtr<FUserPinInfo>& Info : Tunnel->UserDefinedPins)
		{
			if (!Info.IsValid() || !FCNAPinUtils::IsTypeObjectMissing(Info->PinType))
			{
				continue;
			}
			const FCNAImportedPin* FilePin = Data.FindSignaturePin(Info->PinName, NodeDirection);
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("Type"), FilePin && !FilePin->MissingObject.IsEmpty() ? FText::FromString(FilePin->MissingObject) : FText::FromName(Info->PinType.PinCategory));
			Arguments.Add(TEXT("Pin"), FText::FromName(Info->PinName));
			CNAJsonImporterLocal::AddNote(OutNotes, EMessageSeverity::Error, FCNAMessages::Format(ECNAMessage::CNA010, Arguments));

			const EPinContainerType Container = Info->PinType.ContainerType == EPinContainerType::Map ? EPinContainerType::None : Info->PinType.ContainerType;
			Info->PinType = FEdGraphPinType();
			Info->PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			Info->PinType.ContainerType = Container;
			bChanged = true;
		}
		if (bChanged)
		{
			Tunnel->Modify();
			Tunnel->ReconstructNode();
		}
	}
}

// Commands

bool FCNAJsonImporter::ImportAndOverwrite(UCustomNodeAsset* Asset, const FString& FilePath, bool bConfirm)
{
	using namespace CNAJsonImporterLocal;
	if (!Asset)
	{
		return false;
	}
	const FText FileName = FText::FromString(FPaths::GetCleanFilename(FilePath));
	FCNAMessages::NewPage(FText::Format(LOCTEXT("Import.Page", "Import {0} into {1}"), FileName, FText::FromString(Asset->GetName())));

	FCNAImportData Data;
	TArray<FText> Errors;
	if (!LoadFile(FilePath, Data, Errors))
	{
		ReportFailure(Asset, FilePath, Errors);
		return false;
	}

	if (bConfirm && !FApp::IsUnattended())
	{
		const FText Summary = BuildChangeSummary(Asset, Data);
		const FText Title = LOCTEXT("Import.ConfirmTitle", "Import and Overwrite");
		if (Summary.IsEmpty())
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("Import.NoChanges", "{0} matches {1}; nothing to change."), FileName, FText::FromString(Asset->GetName())), &Title);
			return false;
		}
		const FText Question = FText::Format(LOCTEXT("Import.Confirm", "Overwrite {0} with {1}?\n\n{2}\n\nThe import can be undone with Undo in one step."),
			FText::FromString(Asset->GetName()), FileName, Summary);
		if (FMessageDialog::Open(EAppMsgType::OkCancel, Question, &Title) != EAppReturnType::Ok)
		{
			FCNAMessages::LogInfo(LOCTEXT("Import.Cancelled", "Import cancelled; the asset was not changed."), Asset);
			return false;
		}
	}

	if (!ApplyToAsset(Asset, Data, /*bNewAsset*/ false, Errors))
	{
		ReportFailure(Asset, FilePath, Errors);
		return false;
	}

	const FText Done = FText::Format(LOCTEXT("Import.Done", "{0} imported into {1}."), FileName, FText::FromString(Asset->GetName()));
	FCNAMessages::LogInfo(Done, Asset);
	ShowNotification(Done, true);
	if (Data.Notes.ContainsByPredicate([](const FCNAImportNote& Note) { return Note.Severity != EMessageSeverity::Info; }))
	{
		FCNAMessages::OpenLog(EMessageSeverity::Warning);
	}
	return true;
}

bool FCNAJsonImporter::PromptAndImport(UCustomNodeAsset* Asset)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!Asset || !DesktopPlatform || !FSlateApplication::IsInitialized())
	{
		return false;
	}

	TArray<FString> Files;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		FText::Format(LOCTEXT("Import.DialogTitle", "Import JSON into {0}"), FText::FromString(Asset->GetName())).ToString(),
		FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_IMPORT),
		FString(),
		TEXT("Custom Node JSON (*.json)|*.json"),
		EFileDialogFlags::None,
		Files);
	if (!bPicked || Files.Num() == 0)
	{
		return false;
	}
	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_IMPORT, FPaths::GetPath(Files[0]));
	return ImportAndOverwrite(Asset, Files[0], /*bConfirm*/ true);
}

UCustomNodeAsset* FCNAJsonImporter::CreateAssetFromFile(const FString& FilePath, const FString& PackagePath, TArray<FText>& OutErrors)
{
	FCNAImportData Data;
	if (!LoadFile(FilePath, Data, OutErrors))
	{
		return nullptr;
	}

	FString Folder = PackagePath;
	Folder.RemoveFromEnd(TEXT("/"));
	if (!FPackageName::IsValidLongPackageName(Folder / TEXT("X")))
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.BadFolder", "{0} is not a content folder."), FText::FromString(PackagePath)));
		return nullptr;
	}

	const FString BaseName = ObjectTools::SanitizeObjectName(Data.Name.IsEmpty() ? FPaths::GetBaseFilename(FilePath) : Data.Name);
	const FString AssetName = MakeUniqueAssetName(Folder, BaseName.IsEmpty() ? FString(TEXT("CNA_Imported")) : BaseName);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UCNAAssetFactory* Factory = NewObject<UCNAAssetFactory>();
	UCustomNodeAsset* Asset = Cast<UCustomNodeAsset>(AssetTools.CreateAsset(AssetName, Folder, UCustomNodeAsset::StaticClass(), Factory));
	if (!Asset)
	{
		OutErrors.Add(FText::Format(LOCTEXT("Import.CreateFailed", "Asset {0} could not be created in {1}."), FText::FromString(AssetName), FText::FromString(Folder)));
		return nullptr;
	}

	if (!ApplyToAsset(Asset, Data, /*bNewAsset*/ true, OutErrors))
	{
		TArray<UObject*> ToDelete;
		ToDelete.Add(Asset);
		ObjectTools::ForceDeleteObjects(ToDelete, /*ShowConfirmation*/ false);
		return nullptr;
	}

	FCNAMessages::LogInfo(FText::Format(LOCTEXT("Import.Created", "{0} created from {1}."), FText::FromString(Asset->GetPathName()), FText::FromString(FPaths::GetCleanFilename(FilePath))), Asset);
	return Asset;
}

FString FCNAJsonImporter::MakeUniqueAssetName(const FString& PackagePath, const FString& BaseName)
{
	FString Folder = PackagePath;
	Folder.RemoveFromEnd(TEXT("/"));
	const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	auto IsTaken = [&Folder, &Registry](const FString& Name)
	{
		const FString PackageName = Folder / Name;
		if (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName))
		{
			return true;
		}
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPackageName(FName(*PackageName), Assets);
		return Assets.Num() > 0;
	};

	if (!IsTaken(BaseName))
	{
		return BaseName;
	}
	// Spec 8.3: a conflict is solved with the suffix _1, then _2 and so on.
	for (int32 Suffix = 1; Suffix < 10000; ++Suffix)
	{
		const FString Candidate = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix);
		if (!IsTaken(Candidate))
		{
			return Candidate;
		}
	}
	return BaseName + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
}

void FCNAJsonImporter::ReportFailure(UCustomNodeAsset* Asset, const FString& FilePath, const TArray<FText>& Errors)
{
	for (const FText& Error : Errors)
	{
		FCNAMessages::ReportTextToLogWithSeverity(EMessageSeverity::Error, Error, Asset);
	}
	FCNAMessages::OpenLog(EMessageSeverity::Error);
	if (!FApp::IsUnattended() && !GIsAutomationTesting)
	{
		const FText Message = FText::Format(LOCTEXT("Import.Failed", "{0} was not imported, the asset was not changed:\n\n{1}"),
			FText::FromString(FPaths::GetCleanFilename(FilePath)), CNAJsonImporterLocal::JoinTexts(Errors, 8));
		FMessageDialog::Open(EAppMsgType::Ok, Message);
	}
}

#undef LOCTEXT_NAMESPACE
