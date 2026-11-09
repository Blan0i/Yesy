// Copyright (c) NodeFactory. Custom serialization version of Custom Node Assets (spec 3.1, FormatVersion).

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

/**
 * Version of the data layout of UCustomNodeAsset. Registered through FCustomVersionRegistration and written into
 * every package that contains an asset, so PostLoad can migrate older assets.
 *
 * Add new entries right above VersionPlusOne and never reorder or remove existing ones.
 */
struct NODEFACTORY_API FCNAFormatVersion
{
	enum Type
	{
		/** Assets saved before the custom version existed. */
		BeforeCustomVersionWasAdded = 0,

		/** First public layout: pin overrides, redirects, appearance and flags. */
		InitialVersion = 1,

		/** Local variables are referenced by FGuid, not by name (spec 4.8). */
		LocalVariableGuids = 2,

		/** AssetGuid and the cached data of the asset registry tags (bContainsLatent, dynamic pins). */
		AssetGuidAndRegistryCache = 3,
		/** Node style families, curve body widgets and WidgetGuid of body widgets. */
		NodeStylesAndCurves = 4,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	/** Unique key of the custom version. */
	static const FGuid GUID;

	/** Version stored in the archive, LatestVersion when the archive does not know the key yet. */
	static int32 GetArchiveVersion(const FArchive& Ar);

private:
	FCNAFormatVersion() {}
};
