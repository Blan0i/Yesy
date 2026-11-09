// Copyright (c) NodeFactory. Language of the plugin UI, independent of the editor culture.
//
// Every .cpp of the plugin includes this header as its LAST include. It redefines LOCTEXT, NSLOCTEXT and
// (when Commands.h was included) UI_COMMAND, so every text of the plugin is resolved through FCNALocalization:
//  - English: the source text of the code;
//  - Russian: the translation of the plugin archive (Content/Localization/NodeFactory/ru), English when missing;
//  - Russian & English: English, except inside FCNALocalization::FScopedDetailsLanguage (the node settings of
//    the Details panel of a Custom Node Asset), where Russian is used.
// The source code still uses the LOCTEXT macros, so the GatherText commandlet keeps finding every text.
// Do not include this header from other headers.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

/** Values match ECNAPluginLanguage of UCNAEditorSettings (editor module) one to one. */
enum class ECNALanguageMode : uint8
{
	Russian,
	English,
	RussianAndEnglish
};

struct NODEFACTORY_API FCNALocalization
{
	/** Current mode. Before the editor module pushed it, the value is read from the user settings ini. */
	static ECNALanguageMode GetMode();

	/** Sets the mode (editor settings) and broadcasts OnModeChanged when it changed. */
	static void SetMode(ECNALanguageMode NewMode);

	/** The node settings in the Details panel are Russian (modes Russian and Russian & English). */
	static bool IsDetailsRussian();

	/** All texts are Russian right now (mode Russian, or a details scope in Russian & English). */
	static bool IsRussianActive();

	/** Text of a LOCTEXT / NSLOCTEXT for the current mode. Thread safe; the table is loaded on first use. */
	static FText Resolve(const TCHAR* Namespace, const TCHAR* Key, const TCHAR* Source);

	/** Russian translation of an entry of the plugin archive; false when missing or written for another source. */
	static bool FindRussian(const FString& Namespace, const FString& Key, const FString& Source, FString& OutTranslation);

	/** Native name of a mode: "Русский", "English", "Русский & English". */
	static FText GetModeDisplayName(ECNALanguageMode Mode);

	/** Reloads the translations from the archive and the PO file (after editing them). */
	static void ReloadTable();

	/** Number of loaded translations (diagnostics). */
	static int32 GetNumTranslations();

	/** Broadcast on the game thread when the mode changed: open panels rebuild their texts. */
	static FSimpleMulticastDelegate& OnModeChanged();

	/** While alive (game thread only), mode Russian & English resolves texts in Russian: node settings of the Details. */
	struct NODEFACTORY_API FScopedDetailsLanguage
	{
		FScopedDetailsLanguage();
		~FScopedDetailsLanguage();
	private:
		bool bActive;
	};
};

#ifdef LOCTEXT
#undef LOCTEXT
#endif
#define LOCTEXT(InKey, InTextLiteral) FCNALocalization::Resolve(TEXT(LOCTEXT_NAMESPACE), TEXT(InKey), TEXT(InTextLiteral))

#ifdef NSLOCTEXT
#undef NSLOCTEXT
#endif
#define NSLOCTEXT(InNamespace, InKey, InTextLiteral) FCNALocalization::Resolve(TEXT(InNamespace), TEXT(InKey), TEXT(InTextLiteral))

// UI_COMMAND of Framework/Commands/Commands.h creates its texts through the engine localization. This version keeps
// the command names, icons and chords and takes the label and tooltip from the table: key <Command> and
// <Command>_ToolTip in LOCTEXT_NAMESPACE, as in the archive. Commands are created once, so a new mode applies
// after restarting the editor.
// V-check: FUICommandInfo::MakeCommandInfo(Context, OutCommand, Name, Label, Description, Icon, Type, Chord,
// AlternateChord) is public in 4.20.3. The icon name is "<Context>.<Command>" like the engine macro builds it.
#ifdef UI_COMMAND
#undef UI_COMMAND
#define UI_COMMAND(CommandId, FriendlyName, InDescription, CommandType, InDefaultChord, ...) \
	FUICommandInfo::MakeCommandInfo(this->AsShared(), CommandId, FName(TEXT(#CommandId)), \
		FCNALocalization::Resolve(TEXT(LOCTEXT_NAMESPACE), TEXT(#CommandId), TEXT(FriendlyName)), \
		FCNALocalization::Resolve(TEXT(LOCTEXT_NAMESPACE), TEXT(#CommandId) TEXT("_ToolTip"), TEXT(InDescription)), \
		FSlateIcon(this->GetStyleSetName(), FName(*(this->GetContextName().ToString() + TEXT(".") + TEXT(#CommandId)))), \
		CommandType, InDefaultChord, ## __VA_ARGS__)
#endif
