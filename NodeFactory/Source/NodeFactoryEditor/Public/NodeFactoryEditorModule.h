// Copyright (c) NodeFactory. Editor module: registration of every editor part (spec 2.1, 9.5).

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IAssetTypeActions;

/**
 * Registers in StartupModule and removes in ShutdownModule, in reverse order (spec 9.5): style, message log,
 * settings, compiler extension, Behavior host, graph factories, node menu, update service, commands, asset actions,
 * Details customization and the Node Catalog tab. Every step is safe to repeat, so a hot reload does not duplicate
 * registrations (A15).
 */
class NODEFACTORYEDITOR_API FNodeFactoryEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override { return true; }

	static bool IsAvailable();
	static FNodeFactoryEditorModule& Get();

private:
	void RegisterSettings();
	void UnregisterSettings();
	void RegisterAssetTools();
	void UnregisterAssetTools();
	void RegisterDetails();
	void UnregisterDetails();
	void RegisterCatalogTab();
	void UnregisterCatalogTab();

	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
	bool bSettingsRegistered = false;
	bool bDetailsRegistered = false;
	bool bCatalogTabRegistered = false;
};
