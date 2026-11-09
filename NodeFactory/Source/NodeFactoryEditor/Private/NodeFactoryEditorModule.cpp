// Copyright (c) NodeFactory. Editor module: registration of every editor part (spec 2.1, 9.5).

#include "NodeFactoryEditorModule.h"
#include "CNAAssetDetails.h"
#include "CNAAssetTypeActions.h"
#include "CNABehaviorBase.h"
#include "CNABehaviorHost.h"
#include "CNAEditorCommands.h"
#include "CNAEditorSettings.h"
#include "CNAGraphFactories.h"
#include "CNAMessages.h"
#include "CNAStructCustomizations.h"
#include "CNANodeMenu.h"
#include "CNAUpdateService.h"
#include "CNAValidator.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryStyle.h"
#include "SCNANodeCatalog.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ISettingsModule.h"
#include "PropertyEditorModule.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

IMPLEMENT_MODULE(FNodeFactoryEditorModule, NodeFactoryEditor)

namespace CNAEditorModuleLocal
{
	const FName AssetToolsName(TEXT("AssetTools"));
	const FName PropertyEditorName(TEXT("PropertyEditor"));
	const FName SettingsName(TEXT("Settings"));

	// USTRUCT names without the F prefix (RegisterCustomPropertyTypeLayout).
	const FName PinOverrideStructName(TEXT("CNAPinOverride"));
	const FName PinRedirectStructName(TEXT("CNAPinRedirect"));
	const FName BadgeStructName(TEXT("CNABadge"));
	const FName BodyWidgetStructName(TEXT("CNABodyWidget"));
	const FName CurveSettingsStructName(TEXT("CNACurveSettings"));
}

bool FNodeFactoryEditorModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("NodeFactoryEditor"));
}

FNodeFactoryEditorModule& FNodeFactoryEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FNodeFactoryEditorModule>(TEXT("NodeFactoryEditor"));
}

void FNodeFactoryEditorModule::StartupModule()
{
	// The language of the plugin UI must be known before any panel, log listing or command creates its texts.
	GetDefault<UCNAEditorSettings>()->ApplyLanguage();

	FNodeFactoryStyle::Initialize();
	FCNAMessages::RegisterLogListing();

	// The runtime module checks the parent class of a Behavior Graph without depending on this module (D4).
	FCNAEditorHooks::BehaviorBaseClass = UCNABehaviorBase::StaticClass();

	RegisterSettings();
	FCNAValidator::Register();
	FCNABehaviorHost::Startup();
	FCNAGraphFactories::Register();
	FCNANodeMenu::Startup();
	FCNAUpdateService::Startup();
	FCNAEditorCommands::Register();
	RegisterAssetTools();
	RegisterDetails();
	RegisterCatalogTab();
}

void FNodeFactoryEditorModule::ShutdownModule()
{
	// Reverse order of StartupModule (spec 9.5).
	UnregisterCatalogTab();
	UnregisterDetails();
	UnregisterAssetTools();
	FCNAEditorCommands::Unregister();
	FCNAUpdateService::Shutdown();
	FCNANodeMenu::Shutdown();
	FCNAGraphFactories::Unregister();
	FCNABehaviorHost::Shutdown();
	FCNAValidator::Unregister();
	UnregisterSettings();

	FCNAEditorHooks::BehaviorBaseClass.Reset();

	FCNAMessages::UnregisterLogListing();
	FNodeFactoryStyle::Shutdown();
}

void FNodeFactoryEditorModule::RegisterSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>(CNAEditorModuleLocal::SettingsName);
	if (SettingsModule && !bSettingsRegistered)
	{
		// Editor Preferences, Plugins, NodeFactory (spec 9.7).
		SettingsModule->RegisterSettings(UCNAEditorSettings::ContainerName, UCNAEditorSettings::CategoryName, UCNAEditorSettings::SectionName,
			LOCTEXT("Module.SettingsName", "NodeFactory"),
			LOCTEXT("Module.SettingsDescription", "Custom Node Assets: preset folder of the Node Catalog, Live Preview, update of the nodes and import limits."),
			GetMutableDefault<UCNAEditorSettings>());
		bSettingsRegistered = true;
	}
}

void FNodeFactoryEditorModule::UnregisterSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>(CNAEditorModuleLocal::SettingsName);
	if (SettingsModule && bSettingsRegistered)
	{
		SettingsModule->UnregisterSettings(UCNAEditorSettings::ContainerName, UCNAEditorSettings::CategoryName, UCNAEditorSettings::SectionName);
	}
	bSettingsRegistered = false;
}

void FNodeFactoryEditorModule::RegisterAssetTools()
{
	if (RegisteredAssetTypeActions.Num() > 0)
	{
		return;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(CNAEditorModuleLocal::AssetToolsName).Get();
	TSharedRef<IAssetTypeActions> Actions = MakeShareable(new FCNAAssetTypeActions);
	AssetTools.RegisterAssetTypeActions(Actions);
	RegisteredAssetTypeActions.Add(Actions);
}

void FNodeFactoryEditorModule::UnregisterAssetTools()
{
	if (FModuleManager::Get().IsModuleLoaded(CNAEditorModuleLocal::AssetToolsName))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(CNAEditorModuleLocal::AssetToolsName).Get();
		for (const TSharedRef<IAssetTypeActions>& Actions : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Actions);
		}
	}
	RegisteredAssetTypeActions.Reset();
}

void FNodeFactoryEditorModule::RegisterDetails()
{
	if (bDetailsRegistered)
	{
		return;
	}
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(CNAEditorModuleLocal::PropertyEditorName);
	PropertyModule.RegisterCustomClassLayout(UCustomNodeAsset::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FCNAAssetDetails::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(UCNAEditorSettings::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FCNAEditorSettingsDetails::MakeInstance));
	// Structs of the asset: only the settings that apply to the current choice, names in the plugin language.
	PropertyModule.RegisterCustomPropertyTypeLayout(CNAEditorModuleLocal::PinOverrideStructName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCNAPinOverrideDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(CNAEditorModuleLocal::PinRedirectStructName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCNAPinRedirectDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(CNAEditorModuleLocal::BadgeStructName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCNABadgeDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(CNAEditorModuleLocal::BodyWidgetStructName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCNABodyWidgetDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(CNAEditorModuleLocal::CurveSettingsStructName, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCNACurveSettingsDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
	bDetailsRegistered = true;
}

void FNodeFactoryEditorModule::UnregisterDetails()
{
	if (bDetailsRegistered && FModuleManager::Get().IsModuleLoaded(CNAEditorModuleLocal::PropertyEditorName))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(CNAEditorModuleLocal::PropertyEditorName);
		// The classes may already be gone during engine exit; the names are enough.
		PropertyModule.UnregisterCustomClassLayout(FName(TEXT("CustomNodeAsset")));
		PropertyModule.UnregisterCustomClassLayout(FName(TEXT("CNAEditorSettings")));
		PropertyModule.UnregisterCustomPropertyTypeLayout(CNAEditorModuleLocal::PinOverrideStructName);
		PropertyModule.UnregisterCustomPropertyTypeLayout(CNAEditorModuleLocal::PinRedirectStructName);
		PropertyModule.UnregisterCustomPropertyTypeLayout(CNAEditorModuleLocal::BadgeStructName);
		PropertyModule.UnregisterCustomPropertyTypeLayout(CNAEditorModuleLocal::BodyWidgetStructName);
		PropertyModule.UnregisterCustomPropertyTypeLayout(CNAEditorModuleLocal::CurveSettingsStructName);
		PropertyModule.NotifyCustomizationModuleChanged();
	}
	bDetailsRegistered = false;
}

void FNodeFactoryEditorModule::RegisterCatalogTab()
{
	if (bCatalogTabRegistered || !FSlateApplication::IsInitialized())
	{
		return;
	}
	// Window, Tools, Node Catalog. The folder is only scanned when the tab opens (spec 8.3).
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(SCNANodeCatalog::TabName, FOnSpawnTab::CreateStatic(&SCNANodeCatalog::SpawnTab))
		.SetDisplayName(LOCTEXT("Module.CatalogTab", "Node Catalog"))
		.SetTooltipText(LOCTEXT("Module.CatalogTabTip", "Presets of custom nodes: search, then import into a Content Browser folder."))
		.SetIcon(FSlateIcon(FNodeFactoryStyle::GetStyleSetName(), FNodeFactoryStyle::CatalogTabIconName))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
	bCatalogTabRegistered = true;
}

void FNodeFactoryEditorModule::UnregisterCatalogTab()
{
	if (bCatalogTabRegistered && FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SCNANodeCatalog::TabName);
	}
	bCatalogTabRegistered = false;
}

#undef LOCTEXT_NAMESPACE
