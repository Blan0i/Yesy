// Copyright (c) NodeFactory. Runtime module of the plugin (spec 2.1).

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/** Log category of the whole plugin (spec 2.5). The editor module writes into it as well. */
NODEFACTORY_API DECLARE_LOG_CATEGORY_EXTERN(LogNodeFactory, Log, All);

/**
 * Runtime module. It only owns the asset class UCustomNodeAsset and the data types of the asset;
 * nothing of a Custom Node Asset is needed by a packaged game (spec 2.2).
 */
class NODEFACTORY_API FNodeFactoryModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Hot Reload of the runtime module is supported: it holds no state (spec 9.5). */
	virtual bool SupportsDynamicReloading() override { return true; }

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("NodeFactory"));
	}

	static FNodeFactoryModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FNodeFactoryModule>(TEXT("NodeFactory"));
	}
};
