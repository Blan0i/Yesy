// Copyright (c) NodeFactory. The Custom Node Asset: a Blueprint subclass that describes one node (spec 2, 3).

#include "CustomNodeAsset.h"
#include "CNAFormatVersion.h"
#include "NodeFactoryModule.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Texture2D.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

const FName UCustomNodeAsset::ExecuteGraphName(TEXT("Execute"));
const FName UCustomNodeAsset::BehaviorGraphName(TEXT("BehaviorGraph"));

// Registry tags

namespace CNATags
{
	const FName Category(TEXT("CNA.Category"));
	const FName Purity(TEXT("CNA.Purity"));
	const FName DynamicPins(TEXT("CNA.DynamicPins"));
	const FName Description(TEXT("CNA.Description"));
	const FName Version(TEXT("CNA.Version"));

	const FName DisplayName(TEXT("CNA.DisplayName"));
	const FName Keywords(TEXT("CNA.Keywords"));
	const FName Tooltip(TEXT("CNA.Tooltip"));
	const FName Hidden(TEXT("CNA.Hidden"));
	const FName AllowedGraphs(TEXT("CNA.AllowedGraphs"));
	const FName ContextClass(TEXT("CNA.ContextClass"));
	const FName Latent(TEXT("CNA.Latent"));
	const FName Author(TEXT("CNA.Author"));
	const FName Tags(TEXT("CNA.Tags"));
	const FName Deprecated(TEXT("CNA.Deprecated"));

	FString FromBool(bool bValue)
	{
		return bValue ? TEXT("True") : TEXT("False");
	}

	bool ToBool(const FString& Value)
	{
		return Value.Equals(TEXT("True"), ESearchCase::IgnoreCase) || Value == TEXT("1");
	}
}

#if WITH_EDITOR
FCNAEditorHooks::FValidateAsset FCNAEditorHooks::ValidateAsset;
FCNAEditorHooks::FOnAssetChanged FCNAEditorHooks::OnAssetChanged;
FCNAEditorHooks::FOnAssetPreSave FCNAEditorHooks::OnAssetPreSave;
TWeakObjectPtr<UClass> FCNAEditorHooks::BehaviorBaseClass;
#endif

// Construction and serialization

UCustomNodeAsset::UCustomNodeAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bHiddenInMenu = false;
	bAllowInstanceLabel = false;
	bUseHeaderColor = false;
	HeaderColor = FLinearColor(FColor(0, 122, 112));
	HeaderTextColor = FLinearColor::White;
	PinLabelColor = FLinearColor::White;
	Icon = FName(TEXT("Kismet.AllClasses.FunctionIcon"));
	IconTint = FLinearColor::White;
	IconTextureTint = FLinearColor::White;
	CornerIconTextureTint = FLinearColor::White;
	CornerIconTextureSize = 24.0f;
	CornerIcon = ECNACornerIcon::None;
	bCompact = false;
	MinWidth = 0.0f;
	MinHeight = 0.0f;
	bColorWires = true;
	NodeStyle = ECNANodeStyle::Blueprint;
	AnimStyle = ECNAAnimNodeStyle::AnimNode;
	bExecPinsAsPosePins = true;
	bShowFastPathIcon = false;
	NiagaraStyle = ECNANiagaraNodeStyle::FunctionCall;
	bNiagaraAddPinRows = true;
	bNiagaraNamespaceLabels = true;
	bExecPinsAsParameterMap = true;
	bPure = false;
	bDevelopmentOnly = false;
	bDeprecated = false;
	AllowedGraphs = CNATypes::AllGraphKindsMask();
	AssetVersion = 1;
	SignatureHash = 0;
	bContainsLatent = false;
	CachedDynamicPinsDescription = TEXT("None");
	ExecuteGraph = nullptr;
	BehaviorGraph = nullptr;
#endif
}

void UCustomNodeAsset::Serialize(FArchive& Ar)
{
	// Written before the properties so PostLoad can read the version through GetLinkerCustomVersion.
	Ar.UsingCustomVersion(FCNAFormatVersion::GUID);
	Super::Serialize(Ar);
}

void UCustomNodeAsset::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_ArchetypeObject))
	{
		// Every change of the asset must be undoable (spec 9.4).
		SetFlags(RF_Transactional);
		if (!AssetGuid.IsValid())
		{
			AssetGuid = FGuid::NewGuid();
		}
	}
#endif
}

#if WITH_EDITOR

// UObject

void UCustomNodeAsset::PostLoad()
{
	Super::PostLoad();

	const int32 FormatVersion = GetLinkerCustomVersion(FCNAFormatVersion::GUID);

	if (FormatVersion < FCNAFormatVersion::LocalVariableGuids)
	{
		// Local variables used to be referenced by name; give every variable an identity.
		for (FCNALocalVariable& Variable : LocalVariables)
		{
			if (!Variable.Guid.IsValid())
			{
				Variable.Guid = FGuid::NewGuid();
			}
		}
	}

	if (FormatVersion < FCNAFormatVersion::AssetGuidAndRegistryCache)
	{
		if (!AssetGuid.IsValid())
		{
			AssetGuid = FGuid::NewGuid();
		}
		// The registry cache did not exist; the editor module recomputes it on the next save.
		if (CachedDynamicPinsDescription.IsEmpty())
		{
			CachedDynamicPinsDescription = TEXT("None");
		}
	}

	EnsureUniqueLocalVariableGuids();
	EnsureUniqueBodyWidgetGuids();
	RefreshGraphPointers();

	AssetVersion = FMath::Max(AssetVersion, 1);
	MinWidth = FMath::Clamp(MinWidth, 0.0f, 1200.0f);
	MinHeight = FMath::Clamp(MinHeight, 0.0f, 1200.0f);
	CornerIconTextureSize = FMath::Clamp(CornerIconTextureSize, 8.0f, 128.0f);

	if (FormatVersion < FCNAFormatVersion::LatestVersion)
	{
		UE_LOG(LogNodeFactory, Log, TEXT("%s migrated from format version %d to %d."),
			*GetPathName(), FormatVersion, static_cast<int32>(FCNAFormatVersion::LatestVersion));
	}
}

void UCustomNodeAsset::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		// A duplicated asset is a different node: new identity, same structure.
		AssetGuid = FGuid::NewGuid();
	}
	RefreshGraphPointers();
}

void UCustomNodeAsset::PreSave(const ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

	// Only editor saves refresh version and caches; a cook must not change the asset.
	if (TargetPlatform == nullptr)
	{
		EnsureUniqueLocalVariableGuids();
		EnsureUniqueBodyWidgetGuids();
		FCNAEditorHooks::OnAssetPreSave.Broadcast(this);
	}
}

void UCustomNodeAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : PropertyName;

	if (MemberName == GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, LocalVariables))
	{
		EnsureUniqueLocalVariableGuids();
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, BodyWidgets))
	{
		EnsureUniqueBodyWidgetGuids();
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, MinWidth))
	{
		MinWidth = FMath::Clamp(MinWidth, 0.0f, 1200.0f);
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, MinHeight))
	{
		MinHeight = FMath::Clamp(MinHeight, 0.0f, 1200.0f);
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinOverrides))
	{
		for (FCNAPinOverride& Override : PinOverrides)
		{
			int32 Min = 0;
			int32 Max = 1;
			Override.GetDynamicRange(Min, Max);
			Override.DynamicMin = Min;
			Override.DynamicMax = Max;
		}
	}

	NotifyAssetChanged(MemberName);
}

void UCustomNodeAsset::PostEditUndo()
{
	Super::PostEditUndo();
	RefreshGraphPointers();
	NotifyAssetChanged(NAME_None);
}

void UCustomNodeAsset::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);

	// Visible tags: the tooltip of the Content Browser (spec 7.2, V19).
	OutTags.Add(FAssetRegistryTag(CNATags::Category, Category.ToString(), FAssetRegistryTag::TT_Alphabetical));
	OutTags.Add(FAssetRegistryTag(CNATags::Purity, bPure ? TEXT("Pure") : TEXT("Impure"), FAssetRegistryTag::TT_Alphabetical));
	OutTags.Add(FAssetRegistryTag(CNATags::DynamicPins, CachedDynamicPinsDescription.IsEmpty() ? FString(TEXT("None")) : CachedDynamicPinsDescription, FAssetRegistryTag::TT_Alphabetical));
	OutTags.Add(FAssetRegistryTag(CNATags::Description, Description, FAssetRegistryTag::TT_Alphabetical));
	OutTags.Add(FAssetRegistryTag(CNATags::Version, FString::FromInt(AssetVersion), FAssetRegistryTag::TT_Numerical));

	// Hidden tags: the node menu builds its entries without loading the asset (spec 9.8).
	OutTags.Add(FAssetRegistryTag(CNATags::DisplayName, GetEffectiveDisplayName().ToString(), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(CNATags::Keywords, Keywords.ToString(), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(CNATags::Tooltip, Tooltip.ToString(), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(CNATags::Hidden, CNATags::FromBool(bHiddenInMenu), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(CNATags::AllowedGraphs, FString::FromInt(AllowedGraphs), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(CNATags::ContextClass, ContextClass ? ContextClass->GetPathName() : FString(), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(CNATags::Latent, CNATags::FromBool(bContainsLatent), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(CNATags::Author, Author, FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(CNATags::Tags, FString::Join(Tags, TEXT(",")), FAssetRegistryTag::TT_Hidden));
	OutTags.Add(FAssetRegistryTag(CNATags::Deprecated, CNATags::FromBool(bDeprecated), FAssetRegistryTag::TT_Hidden));
}

#if CNA_WITH_DATA_VALIDATION
EDataValidationResult UCustomNodeAsset::IsDataValid(TArray<FText>& ValidationErrors)
{
	EDataValidationResult Result = Super::IsDataValid(ValidationErrors);
	if (FCNAEditorHooks::ValidateAsset.IsBound())
	{
		const bool bValid = FCNAEditorHooks::ValidateAsset.Execute(this, ValidationErrors);
		if (!bValid)
		{
			Result = EDataValidationResult::Invalid;
		}
		else if (Result == EDataValidationResult::NotValidated)
		{
			Result = EDataValidationResult::Valid;
		}
	}
	return Result;
}
#endif

// UBlueprint

void UCustomNodeAsset::GetReparentingRules(TSet<const UClass*>& AllowedChildrenOfClasses, TSet<const UClass*>& DisallowedChildrenOfClasses) const
{
	// The Behavior Graph only works with UCNABehaviorBase and its children (spec D4).
	if (UClass* BaseClass = FCNAEditorHooks::BehaviorBaseClass.Get())
	{
		AllowedChildrenOfClasses.Add(BaseClass);
	}
}

// Graphs

UEdGraph* UCustomNodeAsset::FindExecuteGraph() const
{
	// The cached pointer survives a rename of the macro as long as it is still one of the macro graphs.
	if (ExecuteGraph && !ExecuteGraph->IsPendingKill() && MacroGraphs.Contains(ExecuteGraph))
	{
		return ExecuteGraph;
	}
	for (UEdGraph* Graph : MacroGraphs)
	{
		if (Graph && Graph->GetFName() == ExecuteGraphName)
		{
			return Graph;
		}
	}
	return nullptr;
}

UEdGraph* UCustomNodeAsset::FindBehaviorGraph() const
{
	if (BehaviorGraph && !BehaviorGraph->IsPendingKill() && UbergraphPages.Contains(BehaviorGraph))
	{
		return BehaviorGraph;
	}
	for (UEdGraph* Graph : UbergraphPages)
	{
		if (Graph && Graph->GetFName() == BehaviorGraphName)
		{
			return Graph;
		}
	}
	// Assets created by hand: the first Event Graph page is the Behavior Graph.
	return UbergraphPages.Num() > 0 ? UbergraphPages[0] : nullptr;
}

void UCustomNodeAsset::RefreshGraphPointers()
{
	ExecuteGraph = FindExecuteGraph();
	BehaviorGraph = FindBehaviorGraph();
}

// Pins

const FCNAPinOverride* UCustomNodeAsset::FindPinOverride(FName PinName) const
{
	if (PinName.IsNone())
	{
		return nullptr;
	}
	for (const FCNAPinOverride& Override : PinOverrides)
	{
		if (Override.PinName == PinName)
		{
			return &Override;
		}
	}
	return nullptr;
}

FCNAPinOverride* UCustomNodeAsset::FindPinOverrideMutable(FName PinName)
{
	return const_cast<FCNAPinOverride*>(FindPinOverride(PinName));
}

FCNAPinOverride& UCustomNodeAsset::FindOrAddPinOverride(FName PinName)
{
	if (FCNAPinOverride* Existing = FindPinOverrideMutable(PinName))
	{
		return *Existing;
	}
	const int32 NewIndex = PinOverrides.Add(FCNAPinOverride(PinName));
	return PinOverrides[NewIndex];
}

const FCNALocalVariable* UCustomNodeAsset::FindLocalVariable(const FGuid& Guid, FName FallbackName) const
{
	if (Guid.IsValid())
	{
		for (const FCNALocalVariable& Variable : LocalVariables)
		{
			if (Variable.Guid == Guid)
			{
				return &Variable;
			}
		}
	}
	return FallbackName.IsNone() ? nullptr : FindLocalVariableByName(FallbackName);
}

const FCNALocalVariable* UCustomNodeAsset::FindLocalVariableByName(FName Name) const
{
	for (const FCNALocalVariable& Variable : LocalVariables)
	{
		if (Variable.Name == Name)
		{
			return &Variable;
		}
	}
	return nullptr;
}

FName UCustomNodeAsset::MakeUniqueLocalVariableName(FName BaseName) const
{
	const FString Base = BaseName.IsNone() ? FString(TEXT("NewVar")) : BaseName.ToString();
	FName Candidate(*Base);
	for (int32 Suffix = 1; FindLocalVariableByName(Candidate) != nullptr; ++Suffix)
	{
		Candidate = FName(*FString::Printf(TEXT("%s_%d"), *Base, Suffix));
	}
	return Candidate;
}

FName UCustomNodeAsset::ResolvePinRedirect(FName OldName) const
{
	FName Current = OldName;
	TSet<FName> Visited;
	// Chains A -> B -> C are followed; a cycle stops at the last name before repeating.
	while (!Current.IsNone() && !Visited.Contains(Current))
	{
		Visited.Add(Current);
		const FCNAPinRedirect* Match = PinRedirects.FindByPredicate([Current](const FCNAPinRedirect& Redirect)
		{
			return Redirect.OldName == Current && !Redirect.NewName.IsNone();
		});
		if (!Match)
		{
			break;
		}
		Current = Match->NewName;
	}
	return Current;
}

bool UCustomNodeAsset::HasDynamicGroup() const
{
	return PinOverrides.ContainsByPredicate([](const FCNAPinOverride& Override) { return Override.bDynamicGroup; });
}

ECNACornerIcon UCustomNodeAsset::GetEffectiveCornerIcon() const
{
	return bContainsLatent ? ECNACornerIcon::Latent : CornerIcon;
}

FText UCustomNodeAsset::GetEffectiveDisplayName() const
{
	if (!DisplayName.IsEmpty())
	{
		return DisplayName;
	}
	FString Name = GetName();
	Name.RemoveFromStart(TEXT("CNA_"));
	return FText::FromString(FName::NameToDisplayString(Name, false));
}

// Changes

void UCustomNodeAsset::BumpAssetVersion()
{
	Modify();
	AssetVersion = FMath::Max(AssetVersion, 0) + 1;
	UE_LOG(LogNodeFactory, Log, TEXT("%s: pin structure changed, AssetVersion is now %d."), *GetName(), AssetVersion);
}

void UCustomNodeAsset::NotifyAssetChanged(FName PropertyName)
{
	FCNAEditorHooks::OnAssetChanged.Broadcast(this, PropertyName);
}

bool UCustomNodeAsset::EnsureUniqueLocalVariableGuids()
{
	bool bChanged = false;
	TSet<FGuid> Seen;
	for (FCNALocalVariable& Variable : LocalVariables)
	{
		if (!Variable.Guid.IsValid() || Seen.Contains(Variable.Guid))
		{
			Variable.Guid = FGuid::NewGuid();
			bChanged = true;
		}
		Seen.Add(Variable.Guid);
	}
	return bChanged;
}

bool UCustomNodeAsset::EnsureUniqueBodyWidgetGuids()
{
	bool bChanged = false;
	TSet<FGuid> Seen;
	for (FCNABodyWidget& Widget : BodyWidgets)
	{
		if (!Widget.WidgetGuid.IsValid() || Seen.Contains(Widget.WidgetGuid))
		{
			Widget.WidgetGuid = FGuid::NewGuid();
			bChanged = true;
		}
		Seen.Add(Widget.WidgetGuid);
	}
	return bChanged;
}

const FCNABodyWidget* UCustomNodeAsset::FindCurveWidget(const FGuid& WidgetGuid, FName FallbackName) const
{
	if (WidgetGuid.IsValid())
	{
		for (const FCNABodyWidget& Widget : BodyWidgets)
		{
			if (Widget.IsCurve() && Widget.WidgetGuid == WidgetGuid)
			{
				return &Widget;
			}
		}
	}
	if (!FallbackName.IsNone())
	{
		for (const FCNABodyWidget& Widget : BodyWidgets)
		{
			if (Widget.IsCurve() && Widget.PinName == FallbackName)
			{
				return &Widget;
			}
		}
	}
	return nullptr;
}

void UCustomNodeAsset::GetCurveWidgets(TArray<const FCNABodyWidget*>& OutWidgets) const
{
	OutWidgets.Reset();
	for (const FCNABodyWidget& Widget : BodyWidgets)
	{
		if (Widget.IsCurve())
		{
			OutWidgets.Add(&Widget);
		}
	}
}

bool UCustomNodeAsset::GetStyleHeaderColor(FLinearColor& OutColor) const
{
	if (bUseHeaderColor)
	{
		OutColor = HeaderColor;
		return true;
	}
	switch (NodeStyle)
	{
	case ECNANodeStyle::Animation:
		OutColor = CNATypes::GetAnimStyleTitleColor(AnimStyle);
		return true;
	case ECNANodeStyle::Niagara:
		// Header colors of the Niagara editor: function calls blue, map nodes grey, ops dark green.
		switch (NiagaraStyle)
		{
		case ECNANiagaraNodeStyle::MapGet:
		case ECNANiagaraNodeStyle::MapSet:     OutColor = FLinearColor(0.12f, 0.12f, 0.12f); break;
		case ECNANiagaraNodeStyle::Op:         OutColor = FLinearColor(0.05f, 0.25f, 0.10f); break;
		case ECNANiagaraNodeStyle::Input:
		case ECNANiagaraNodeStyle::Output:     OutColor = FLinearColor(0.40f, 0.10f, 0.10f); break;
		case ECNANiagaraNodeStyle::CustomHlsl: OutColor = FLinearColor(0.30f, 0.15f, 0.40f); break;
		case ECNANiagaraNodeStyle::FunctionCall:
		default:                               OutColor = FLinearColor(0.05f, 0.20f, 0.45f); break;
		}
		return true;
	case ECNANodeStyle::Blueprint:
	default:
		return false;
	}
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
