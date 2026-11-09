// Copyright (c) NodeFactory. Editor Preferences > Plugins > NodeFactory (spec 9.7, D7).

#include "CNAEditorSettings.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "CNALocalization.h"

const FName UCNAEditorSettings::ContainerName(TEXT("Editor"));
const FName UCNAEditorSettings::CategoryName(TEXT("Plugins"));
const FName UCNAEditorSettings::SectionName(TEXT("NodeFactory"));

UCNAEditorSettings::UCNAEditorSettings()
	: Language(ECNAPluginLanguage::RussianAndEnglish)
	, MaxExpansionDepth(16)
	, bConfirmBulkUpdate(true)
	, MaxImportFileSizeKB(4096)
	, bVerboseLog(false)
	, bLivePreviewEnabled(true)
{
}

FString UCNAEditorSettings::GetCatalogFolderAbsolute() const
{
	FString Folder = CatalogFolder.Path;
	if (Folder.IsEmpty())
	{
		// The presets shipped with the plugin are listed until the project has a folder of its own (spec 11.3).
		Folder = FPaths::Combine(FPaths::ProjectDir(), TEXT("NodeFactory"), TEXT("Presets"));
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeFactory"));
		if (!FPaths::DirectoryExists(Folder) && Plugin.IsValid() && FPaths::DirectoryExists(Plugin->GetBaseDir() / TEXT("Presets")))
		{
			Folder = Plugin->GetBaseDir() / TEXT("Presets");
		}
	}
	else if (FPaths::IsRelative(Folder))
	{
		Folder = FPaths::Combine(FPaths::ProjectDir(), Folder);
	}
	Folder = FPaths::ConvertRelativePathToFull(Folder);
	FPaths::NormalizeDirectoryName(Folder);
	return Folder;
}

int64 UCNAEditorSettings::GetMaxImportFileSizeBytes() const
{
	return static_cast<int64>(FMath::Max(MaxImportFileSizeKB, 1)) * 1024;
}

void UCNAEditorSettings::SetLivePreviewEnabled(bool bEnabled)
{
	if (bLivePreviewEnabled != bEnabled)
	{
		bLivePreviewEnabled = bEnabled;
		SaveConfig();
	}
}

void UCNAEditorSettings::ApplyLanguage() const
{
	// ECNAPluginLanguage and ECNALanguageMode list the same values in the same order.
	const uint8 Value = static_cast<uint8>(Language);
	FCNALocalization::SetMode(Value <= static_cast<uint8>(ECNALanguageMode::RussianAndEnglish)
		? static_cast<ECNALanguageMode>(Value)
		: ECNALanguageMode::RussianAndEnglish);
}

#if WITH_EDITOR
void UCNAEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MaxExpansionDepth = FMath::Clamp(MaxExpansionDepth, 1, 128);
	MaxImportFileSizeKB = FMath::Clamp(MaxImportFileSizeKB, 1, 65536);
	SaveConfig();
	ApplyLanguage();
}
#endif
