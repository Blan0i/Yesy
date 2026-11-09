// Copyright (c) NodeFactory. Custom serialization version of Custom Node Assets (spec 3.1, FormatVersion).

#include "CNAFormatVersion.h"
#include "Serialization/CustomVersion.h"

const FGuid FCNAFormatVersion::GUID(0x6A1F3C27, 0x4B8E4D12, 0x9E5C7A30, 0x2D41B9F8);

// Registers the version with the engine; the friendly name shows up in package summaries and logs.
static FCustomVersionRegistration GRegisterCNAFormatVersion(FCNAFormatVersion::GUID, FCNAFormatVersion::LatestVersion, TEXT("NodeFactoryCNAFormat"));

int32 FCNAFormatVersion::GetArchiveVersion(const FArchive& Ar)
{
	const int32 Version = Ar.CustomVer(GUID);
	// An archive that never called UsingCustomVersion reports -1; treat such data as current (V-check).
	return Version >= 0 ? Version : static_cast<int32>(LatestVersion);
}
