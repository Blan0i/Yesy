// Copyright (c) NodeFactory. Runtime module of the plugin (spec 2.1).

#include "NodeFactoryModule.h"
#include "CNAFormatVersion.h"
#include "CNALocalization.h"

DEFINE_LOG_CATEGORY(LogNodeFactory);

#define LOCTEXT_NAMESPACE "NodeFactory"

void FNodeFactoryModule::StartupModule()
{
	// The custom version is registered by a static FCustomVersionRegistration in CNAFormatVersion.cpp,
	// so assets saved by any version of the plugin can be migrated in PostLoad (spec 3.1, FormatVersion).
	UE_LOG(LogNodeFactory, Verbose, TEXT("NodeFactory runtime module started, asset format version %d."),
		static_cast<int32>(FCNAFormatVersion::LatestVersion));
}

void FNodeFactoryModule::ShutdownModule()
{
	UE_LOG(LogNodeFactory, Verbose, TEXT("NodeFactory runtime module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNodeFactoryModule, NodeFactory)
