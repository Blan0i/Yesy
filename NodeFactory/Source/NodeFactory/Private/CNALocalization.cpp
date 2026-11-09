// Copyright (c) NodeFactory. Language of the plugin UI, independent of the editor culture.

#include "CNALocalization.h"
#include "NodeFactoryModule.h"
#include "Misc/ConfigCacheIni.h"
#include "CoreGlobals.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformAtomics.h"
#include "Interfaces/IPluginManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CNALocalizationLocal
{
	const TCHAR* PluginNamespace = TEXT("NodeFactory");

	// Section and key UCNAEditorSettings (config = EditorPerProjectUserSettings) writes the Language property to.
	const TCHAR* SettingsSection = TEXT("/Script/NodeFactoryEditor.CNAEditorSettings");
	const TCHAR* SettingsKey = TEXT("Language");

	/** Without a setting the node settings are Russian and the rest stays English. */
	const ECNALanguageMode DefaultMode = ECNALanguageMode::RussianAndEnglish;

	struct FTranslation
	{
		FString Source;
		FString Translation;
	};

	/** One LOCTEXT site. Literal pointers are only a fast key; the strings are compared on every hit (hot reload). */
	struct FCacheEntry
	{
		FString Namespace;
		FString Key;
		FString Source;
		FText English;
		FText Russian;
	};

	// Function statics: safe for FText statics of other translation units initialised before this one.
	FCriticalSection& GetLock()
	{
		static FCriticalSection Lock;
		return Lock;
	}

	TMap<FString, FTranslation>& GetTable()
	{
		static TMap<FString, FTranslation> Table;
		return Table;
	}

	TMap<UPTRINT, TArray<FCacheEntry>>& GetCache()
	{
		static TMap<UPTRINT, TArray<FCacheEntry>> Cache;
		return Cache;
	}

	bool bTableLoaded = false;

	/** -1 until the mode was read from the ini or set by the editor module. */
	volatile int32 ModeValue = -1;

	/** Depth of FScopedDetailsLanguage; only touched on the game thread. */
	int32 DetailsScopeDepth = 0;

	FString MakeTableKey(const FString& Namespace, const FString& Key)
	{
		return Namespace + TEXT(",") + Key;
	}

	bool ParseMode(FString Value, ECNALanguageMode& OutMode)
	{
		// Enum class values are written without the type prefix; accept "ECNAPluginLanguage::X" as well.
		int32 Separator = INDEX_NONE;
		if (Value.FindLastChar(TEXT(':'), Separator))
		{
			Value = Value.Mid(Separator + 1);
		}
		Value.TrimStartAndEndInline();
		if (Value.Equals(TEXT("Russian"), ESearchCase::IgnoreCase))
		{
			OutMode = ECNALanguageMode::Russian;
			return true;
		}
		if (Value.Equals(TEXT("English"), ESearchCase::IgnoreCase))
		{
			OutMode = ECNALanguageMode::English;
			return true;
		}
		if (Value.Equals(TEXT("RussianAndEnglish"), ESearchCase::IgnoreCase))
		{
			OutMode = ECNALanguageMode::RussianAndEnglish;
			return true;
		}
		return false;
	}

	void AddTranslation(const FString& Namespace, const FString& Key, const FString& Source, const FString& Translation, bool bOverwrite)
	{
		if (Key.IsEmpty() || Translation.IsEmpty())
		{
			return;
		}
		const FString TableKey = MakeTableKey(Namespace, Key);
		if (!bOverwrite && GetTable().Contains(TableKey))
		{
			return;
		}
		FTranslation& Entry = GetTable().FindOrAdd(TableKey);
		Entry.Source = Source;
		Entry.Translation = Translation;
	}

	FString GetTextField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Inner = nullptr;
		FString Text;
		if (Object.IsValid() && Object->TryGetObjectField(Field, Inner) && Inner && Inner->IsValid())
		{
			(*Inner)->TryGetStringField(TEXT("Text"), Text);
		}
		return Text;
	}

	/** Archive of the GatherText pipeline (FormatVersion 2): namespaces with children {Source, Translation, Key}. */
	void ReadArchiveNamespace(const TSharedPtr<FJsonObject>& Node, const FString& Namespace)
	{
		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (Node->TryGetArrayField(TEXT("Children"), Children) && Children)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Children)
			{
				const TSharedPtr<FJsonObject> Child = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Child.IsValid())
				{
					continue;
				}
				FString Key;
				Child->TryGetStringField(TEXT("Key"), Key);
				const FString Source = GetTextField(Child, TEXT("Source"));
				const FString Translation = GetTextField(Child, TEXT("Translation"));
				AddTranslation(Namespace, Key, Source, Translation, /*bOverwrite*/ true);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Subnamespaces = nullptr;
		if (Node->TryGetArrayField(TEXT("Subnamespaces"), Subnamespaces) && Subnamespaces)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Subnamespaces)
			{
				const TSharedPtr<FJsonObject> Child = Value.IsValid() ? Value->AsObject() : nullptr;
				if (Child.IsValid())
				{
					FString ChildNamespace;
					Child->TryGetStringField(TEXT("Namespace"), ChildNamespace);
					ReadArchiveNamespace(Child, ChildNamespace);
				}
			}
		}
	}

	bool LoadArchive(const FString& FilePath)
	{
		FString Text;
		// LoadFileToString detects the UTF-16 byte order mark of the archive.
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogNodeFactory, Warning, TEXT("NodeFactory localization: %s is not valid JSON."), *FilePath);
			return false;
		}
		FString RootNamespace;
		Root->TryGetStringField(TEXT("Namespace"), RootNamespace);
		ReadArchiveNamespace(Root, RootNamespace);
		return true;
	}

	/** Body of a quoted PO string with the C escapes resolved. */
	FString UnquotePo(const FString& Line)
	{
		FString Trimmed = Line;
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.Len() < 2 || !Trimmed.StartsWith(TEXT("\"")) || !Trimmed.EndsWith(TEXT("\"")))
		{
			return FString();
		}
		const FString Body = Trimmed.Mid(1, Trimmed.Len() - 2);
		FString Result;
		Result.Reserve(Body.Len());
		for (int32 Index = 0; Index < Body.Len(); ++Index)
		{
			const TCHAR Character = Body[Index];
			if (Character != TEXT('\\') || Index + 1 >= Body.Len())
			{
				Result.AppendChar(Character);
				continue;
			}
			const TCHAR Next = Body[++Index];
			switch (Next)
			{
			case TEXT('n'): Result.AppendChar(TEXT('\n')); break;
			case TEXT('r'): Result.AppendChar(TEXT('\r')); break;
			case TEXT('t'): Result.AppendChar(TEXT('\t')); break;
			default:        Result.AppendChar(Next); break;
			}
		}
		return Result;
	}

	/**
	 * PO file of the Export step (msgctxt "Namespace,Key"). Only fills keys the archive does not have, so a new
	 * entry can be added by editing either file.
	 */
	bool LoadPo(const FString& FilePath)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			return false;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*InCullEmpty*/ false);

		enum class EField { None, Context, Id, Str };
		EField Current = EField::None;
		FString Context;
		FString Id;
		FString Str;

		auto Flush = [&]()
		{
			int32 Comma = INDEX_NONE;
			if (!Context.IsEmpty() && Context.FindChar(TEXT(','), Comma))
			{
				AddTranslation(Context.Left(Comma), Context.Mid(Comma + 1), Id, Str, /*bOverwrite*/ false);
			}
			Context.Reset();
			Id.Reset();
			Str.Reset();
			Current = EField::None;
		};

		for (const FString& RawLine : Lines)
		{
			FString Line = RawLine;
			Line.TrimStartAndEndInline();
			if (Line.IsEmpty())
			{
				Flush();
				continue;
			}
			if (Line.StartsWith(TEXT("#")))
			{
				continue;
			}
			if (Line.StartsWith(TEXT("msgctxt ")))
			{
				if (Current != EField::None && Current != EField::Context)
				{
					Flush();
				}
				Current = EField::Context;
				Context = UnquotePo(Line.Mid(8));
			}
			else if (Line.StartsWith(TEXT("msgid ")))
			{
				Current = EField::Id;
				Id = UnquotePo(Line.Mid(6));
			}
			else if (Line.StartsWith(TEXT("msgstr ")))
			{
				Current = EField::Str;
				Str = UnquotePo(Line.Mid(7));
			}
			else if (Line.StartsWith(TEXT("\"")))
			{
				// Continuation of the previous field.
				switch (Current)
				{
				case EField::Context: Context += UnquotePo(Line); break;
				case EField::Id:      Id += UnquotePo(Line); break;
				case EField::Str:     Str += UnquotePo(Line); break;
				default: break;
				}
			}
		}
		Flush();
		return true;
	}

	/** Loads the table once the plugin is known. Called with the lock held. */
	void EnsureTableLoaded()
	{
		if (bTableLoaded)
		{
			return;
		}
		// The plugin manager knows the plugin as soon as the module is loaded; until then the texts stay English.
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginNamespace);
		if (!Plugin.IsValid())
		{
			return;
		}
		bTableLoaded = true;
		GetTable().Reset();
		const FString Folder = FPaths::Combine(Plugin->GetContentDir(), TEXT("Localization"), TEXT("NodeFactory"), TEXT("ru"));
		const bool bArchive = LoadArchive(FPaths::Combine(Folder, TEXT("NodeFactory.archive")));
		const bool bPo = LoadPo(FPaths::Combine(Folder, TEXT("NodeFactory.po")));
		if (!bArchive && !bPo)
		{
			UE_LOG(LogNodeFactory, Warning, TEXT("NodeFactory localization: no Russian archive or PO file in %s; the plugin UI stays English."), *Folder);
		}
	}

	bool IsDetailsScopeActive()
	{
		return DetailsScopeDepth > 0 && IsInGameThread();
	}

	/** Called with the lock held and the table loaded. */
	bool FindRussianLocked(const FString& Namespace, const FString& Key, const FString& Source, FString& OutTranslation)
	{
		const FTranslation* Entry = GetTable().Find(MakeTableKey(Namespace, Key));
		// A translation written for another source text is stale: English is shown, as the engine does.
		if (!Entry || !Entry->Source.Equals(Source, ESearchCase::CaseSensitive))
		{
			return false;
		}
		OutTranslation = Entry->Translation;
		return true;
	}

	bool ResolveRussian(const FString& Namespace, const FString& Key, const FString& Source, FText& OutText)
	{
		FString Translation;
		if (FindRussianLocked(Namespace, Key, Source, Translation))
		{
			OutText = FText::AsCultureInvariant(Translation);
			return true;
		}
		// Texts of other namespaces (engine texts borrowed with NSLOCTEXT) keep the engine localization.
		if (!Namespace.Equals(PluginNamespace))
		{
			// V-check: FText::FindText(Namespace, Key, OutText, SourceString) of 4.20.3.
			return FText::FindText(Namespace, Key, OutText, &Source);
		}
		return false;
	}
}

ECNALanguageMode FCNALocalization::GetMode()
{
	using namespace CNALocalizationLocal;
	const int32 Value = ModeValue;
	if (Value >= 0)
	{
		return static_cast<ECNALanguageMode>(Value);
	}
	// Static texts may be created before the editor module pushed the setting: read the ini directly.
	ECNALanguageMode Mode = DefaultMode;
	if (GConfig && !GEditorPerProjectIni.IsEmpty())
	{
		FString Stored;
		if (GConfig->GetString(SettingsSection, SettingsKey, Stored, GEditorPerProjectIni))
		{
			ParseMode(Stored, Mode);
		}
		FPlatformAtomics::InterlockedExchange(&ModeValue, static_cast<int32>(Mode));
	}
	return Mode;
}

void FCNALocalization::SetMode(ECNALanguageMode NewMode)
{
	using namespace CNALocalizationLocal;
	const int32 Previous = FPlatformAtomics::InterlockedExchange(&ModeValue, static_cast<int32>(NewMode));
	if (Previous != static_cast<int32>(NewMode) && Previous >= 0 && IsInGameThread())
	{
		OnModeChanged().Broadcast();
	}
}

bool FCNALocalization::IsDetailsRussian()
{
	const ECNALanguageMode Mode = GetMode();
	return Mode == ECNALanguageMode::Russian || Mode == ECNALanguageMode::RussianAndEnglish;
}

bool FCNALocalization::IsRussianActive()
{
	const ECNALanguageMode Mode = GetMode();
	return Mode == ECNALanguageMode::Russian
		|| (Mode == ECNALanguageMode::RussianAndEnglish && CNALocalizationLocal::IsDetailsScopeActive());
}

FText FCNALocalization::Resolve(const TCHAR* Namespace, const TCHAR* Key, const TCHAR* Source)
{
	using namespace CNALocalizationLocal;
	if (!Namespace || !Key || !Source)
	{
		return FText::GetEmpty();
	}
	const bool bRussian = IsRussianActive();

	FScopeLock Lock(&GetLock());
	TArray<FCacheEntry>& Entries = GetCache().FindOrAdd(reinterpret_cast<UPTRINT>(Source));
	for (const FCacheEntry& Entry : Entries)
	{
		if (FCString::Strcmp(*Entry.Key, Key) == 0 && FCString::Strcmp(*Entry.Source, Source) == 0 && FCString::Strcmp(*Entry.Namespace, Namespace) == 0)
		{
			return bRussian ? Entry.Russian : Entry.English;
		}
	}

	FCacheEntry& Entry = Entries[Entries.AddDefaulted()];
	Entry.Namespace = Namespace;
	Entry.Key = Key;
	Entry.Source = Source;
	Entry.English = FText::AsCultureInvariant(Entry.Source);
	EnsureTableLoaded();
	if (!bTableLoaded)
	{
		// Not cached: the table may be loadable on the next call.
		const FText English = Entry.English;
		Entries.Pop();
		return English;
	}
	if (!ResolveRussian(Entry.Namespace, Entry.Key, Entry.Source, Entry.Russian))
	{
		Entry.Russian = Entry.English;
	}
	return bRussian ? Entry.Russian : Entry.English;
}

bool FCNALocalization::FindRussian(const FString& Namespace, const FString& Key, const FString& Source, FString& OutTranslation)
{
	using namespace CNALocalizationLocal;
	FScopeLock Lock(&GetLock());
	EnsureTableLoaded();
	return FindRussianLocked(Namespace, Key, Source, OutTranslation);
}

FText FCNALocalization::GetModeDisplayName(ECNALanguageMode Mode)
{
	// Each language is named in itself, whatever the current mode.
	switch (Mode)
	{
	case ECNALanguageMode::Russian:           return FText::AsCultureInvariant(TEXT("\u0420\u0443\u0441\u0441\u043A\u0438\u0439"));
	case ECNALanguageMode::English:           return FText::AsCultureInvariant(TEXT("English"));
	case ECNALanguageMode::RussianAndEnglish:
	default:                                  return FText::AsCultureInvariant(TEXT("\u0420\u0443\u0441\u0441\u043A\u0438\u0439 & English"));
	}
}

void FCNALocalization::ReloadTable()
{
	using namespace CNALocalizationLocal;
	{
		FScopeLock Lock(&GetLock());
		bTableLoaded = false;
		GetCache().Reset();
		EnsureTableLoaded();
	}
	if (IsInGameThread())
	{
		OnModeChanged().Broadcast();
	}
}

int32 FCNALocalization::GetNumTranslations()
{
	using namespace CNALocalizationLocal;
	FScopeLock Lock(&GetLock());
	EnsureTableLoaded();
	return GetTable().Num();
}

FSimpleMulticastDelegate& FCNALocalization::OnModeChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

FCNALocalization::FScopedDetailsLanguage::FScopedDetailsLanguage()
	: bActive(IsInGameThread())
{
	if (bActive)
	{
		++CNALocalizationLocal::DetailsScopeDepth;
	}
}

FCNALocalization::FScopedDetailsLanguage::~FScopedDetailsLanguage()
{
	if (bActive)
	{
		--CNALocalizationLocal::DetailsScopeDepth;
	}
}
