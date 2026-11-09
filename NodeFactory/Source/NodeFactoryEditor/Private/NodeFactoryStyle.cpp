// Copyright (c) NodeFactory. Slate style set of the plugin (spec 7.2, 9.5).

#include "NodeFactoryStyle.h"
#include "CNATypes.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

TSharedPtr<FSlateStyleSet> FNodeFactoryStyle::StyleInstance;

// Names follow the Content Browser convention ClassIcon.<ClassName> / ClassThumbnail.<ClassName> (spec 7.2).
const FName FNodeFactoryStyle::ClassIconName(TEXT("ClassIcon.CustomNodeAsset"));
const FName FNodeFactoryStyle::ClassThumbnailName(TEXT("ClassThumbnail.CustomNodeAsset"));
const FName FNodeFactoryStyle::PluginIconName(TEXT("NodeFactory.PluginIcon"));
const FName FNodeFactoryStyle::BadgeBrushName(TEXT("NodeFactory.Badge"));
const FName FNodeFactoryStyle::AccentStripName(TEXT("NodeFactory.AccentStrip"));
const FName FNodeFactoryStyle::ImportJsonIconName(TEXT("NodeFactory.ImportJson"));
const FName FNodeFactoryStyle::ExportJsonIconName(TEXT("NodeFactory.ExportJson"));
const FName FNodeFactoryStyle::LivePreviewIconName(TEXT("NodeFactory.LivePreview"));
const FName FNodeFactoryStyle::ValidateIconName(TEXT("NodeFactory.Validate"));
const FName FNodeFactoryStyle::RecompileNodeIconName(TEXT("NodeFactory.RecompileNode"));
const FName FNodeFactoryStyle::CatalogTabIconName(TEXT("NodeFactory.Tab.Catalog"));
const FName FNodeFactoryStyle::PinManagerTabIconName(TEXT("NodeFactory.Tab.PinManager"));
const FName FNodeFactoryStyle::LocalVariablesIconName(TEXT("NodeFactory.Tab.LocalVariables"));
const FName FNodeFactoryStyle::DetailsTabIconName(TEXT("NodeFactory.Tab.Details"));

namespace NodeFactoryStyleLocal
{
	const FVector2D Icon16(16.0f, 16.0f);
	const FVector2D Icon20(20.0f, 20.0f);
	const FVector2D Icon40(40.0f, 40.0f);
	const FVector2D Icon64(64.0f, 64.0f);
	const FVector2D Icon128(128.0f, 128.0f);

	/** Copies a brush of EditorStyle under a new name, so the toolbar keeps working without extra PNG files. */
	void AliasEditorBrush(FSlateStyleSet& Style, FName NewName, FName EditorBrush)
	{
		const FSlateBrush* Source = FEditorStyle::GetOptionalBrush(EditorBrush, nullptr, nullptr);
		if (Source)
		{
			Style.Set(NewName, new FSlateBrush(*Source));
		}
	}
}
void FNodeFactoryStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}
void FNodeFactoryStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		ensure(StyleInstance.IsUnique());
		StyleInstance.Reset();
	}
}
void FNodeFactoryStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}
const ISlateStyle& FNodeFactoryStyle::Get()
{
	check(StyleInstance.IsValid());
	return *StyleInstance;
}
FName FNodeFactoryStyle::GetStyleSetName()
{
	static const FName StyleSetName(TEXT("NodeFactoryStyle"));
	return StyleSetName;
}
TSharedRef<FSlateStyleSet> FNodeFactoryStyle::Create()
{
	using namespace NodeFactoryStyleLocal;

	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));

	FString ResourcesDir;
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeFactory"));
	if (Plugin.IsValid())
	{
		ResourcesDir = Plugin->GetBaseDir() / TEXT("Resources");
	}
	else
	{
		ResourcesDir = FPaths::ProjectPluginsDir() / TEXT("NodeFactory") / TEXT("Resources");
	}
	Style->SetContentRoot(ResourcesDir);
	Style->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	// Content Browser icon and thumbnail: white minimal "linked nodes with a gear" (spec 7.2).
	Style->Set(ClassIconName, new FSlateImageBrush(Style->RootToContentDir(TEXT("ClassIcon16"), TEXT(".png")), Icon16));
	Style->Set(ClassThumbnailName, new FSlateImageBrush(Style->RootToContentDir(TEXT("ClassThumbnail64"), TEXT(".png")), Icon64));
	Style->Set(PluginIconName, new FSlateImageBrush(Style->RootToContentDir(TEXT("Icon128"), TEXT(".png")), Icon128));

	// Rounded badge background, tinted per badge with FCNABadge::Color (spec 3.2).
	Style->Set(BadgeBrushName, new FSlateBoxBrush(Style->RootToCoreContentDir(TEXT("Common/RoundedSelection_16x"), TEXT(".png")), FMargin(4.0f / 16.0f)));
	Style->Set(AccentStripName, new FSlateColorBrush(CNATypes::GetAccentColor()));

	// Toolbar and tab icons reuse EditorStyle brushes that exist in 4.20 (V20).
	AliasEditorBrush(*Style, ImportJsonIconName, TEXT("AssetEditor.ReimportAsset"));
	AliasEditorBrush(*Style, ExportJsonIconName, TEXT("AssetEditor.SaveAsset"));
	AliasEditorBrush(*Style, LivePreviewIconName, TEXT("LevelEditor.Tabs.Viewports"));
	AliasEditorBrush(*Style, ValidateIconName, TEXT("Kismet.Status.Good"));
	AliasEditorBrush(*Style, RecompileNodeIconName, TEXT("BlueprintEditor.RefreshAllNodes"));
	AliasEditorBrush(*Style, CatalogTabIconName, TEXT("ContentBrowser.TabIcon"));
	AliasEditorBrush(*Style, PinManagerTabIconName, TEXT("Kismet.Tabs.MyBlueprint"));
	AliasEditorBrush(*Style, LocalVariablesIconName, TEXT("Kismet.AllClasses.VariableIcon"));
	AliasEditorBrush(*Style, DetailsTabIconName, TEXT("LevelEditor.Tabs.Details"));

	// Text styles of the custom node widget (spec 3.2, V24).
	const FTextBlockStyle& NormalText = FEditorStyle::GetWidgetStyle<FTextBlockStyle>("NormalText");
	Style->Set("NodeFactory.Badge.Text", FTextBlockStyle(NormalText)
		.SetFont(FEditorStyle::GetFontStyle("BoldFont"))
		.SetColorAndOpacity(FLinearColor::White));
	Style->Set("NodeFactory.BodyWidget.Label", FTextBlockStyle(NormalText)
		.SetColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f)));

	return Style;
}
const FSlateBrush* FNodeFactoryStyle::FindBrush(FName BrushName)
{
	if (BrushName.IsNone())
	{
		return nullptr;
	}
	if (StyleInstance.IsValid())
	{
		const FSlateBrush* Own = StyleInstance->GetOptionalBrush(BrushName, nullptr, nullptr);
		if (Own)
		{
			return Own;
		}
	}
	return FEditorStyle::GetOptionalBrush(BrushName, nullptr, nullptr);
}
bool FNodeFactoryStyle::DoesBrushExist(FName BrushName)
{
	return FindBrush(BrushName) != nullptr;
}
const TArray<FName>& FNodeFactoryStyle::GetIconCatalog()
{
	// spec 14: Event, Function, Macro, Cast, Debug, Array, Lock and the usual graph icons.
	static TArray<FName> Catalog;
	static bool bBuilt = false;
	if (!bBuilt)
	{
		bBuilt = true;
		const TCHAR* Candidates[] =
		{
			TEXT("Kismet.AllClasses.FunctionIcon"),
			TEXT("Kismet.AllClasses.VariableIcon"),
			TEXT("Kismet.AllClasses.ArrayVariableIcon"),
			TEXT("Kismet.AllClasses.SetVariableIcon"),
			TEXT("Kismet.AllClasses.MapValueTypeIcon"),
			TEXT("GraphEditor.Function_16x"),
			TEXT("GraphEditor.PureFunction_16x"),
			TEXT("GraphEditor.Macro_16x"),
			TEXT("GraphEditor.Event_16x"),
			TEXT("GraphEditor.CustomEvent_16x"),
			TEXT("GraphEditor.Branch_16x"),
			TEXT("GraphEditor.Sequence_16x"),
			TEXT("GraphEditor.Switch_16x"),
			TEXT("GraphEditor.Select_16x"),
			TEXT("GraphEditor.Cast_16x"),
			TEXT("GraphEditor.Delegate_16x"),
			TEXT("GraphEditor.Timeline_16x"),
			TEXT("GraphEditor.Comment_16x"),
			TEXT("GraphEditor.Default_16x"),
			TEXT("GraphEditor.Enum_16x"),
			TEXT("GraphEditor.FIB_Event"),
			TEXT("GraphEditor.SpawnActor_16x"),
			TEXT("GraphEditor.Message_16x"),
			TEXT("GraphEditor.Macro.Loop_16x"),
			TEXT("GraphEditor.Macro.ForEach_16x"),
			TEXT("GraphEditor.Macro.Gate_16x"),
			TEXT("GraphEditor.Macro.DoOnce_16x"),
			TEXT("GraphEditor.Macro.IsValid_16x"),
			TEXT("GraphEditor.MakeArray_16x"),
			TEXT("GraphEditor.MakeStruct_16x"),
			TEXT("GraphEditor.BreakStruct_16x"),
			TEXT("GraphEditor.GetSequenceBinding"),
			TEXT("GraphEditor.Clock"),
			TEXT("Graph.Latent.LatentIcon"),
			TEXT("Graph.Message.MessageIcon"),
			TEXT("Graph.Replication.AuthorityOnly"),
			TEXT("Graph.Replication.ClientEvent"),
			TEXT("Graph.Replication.Replicated"),
			TEXT("Kismet.Tabs.Palette"),
			TEXT("Kismet.Tabs.CompilerResults"),
			TEXT("Kismet.Status.Error"),
			TEXT("Kismet.Status.Warning"),
			TEXT("Kismet.Status.Good"),
			TEXT("Kismet.VariableList.TypeIcon"),
			TEXT("Kismet.VariableList.ArrayTypeIcon"),
			TEXT("BlueprintEditor.AddNewFunction"),
			TEXT("BlueprintEditor.AddNewMacroDeclaration"),
			TEXT("BlueprintEditor.AddNewEventGraph"),
			TEXT("BlueprintEditor.RefreshAllNodes"),
			TEXT("Icons.Lock"),
			TEXT("Icons.Unlock"),
			TEXT("Icons.Info"),
			TEXT("Icons.Warning"),
			TEXT("Icons.Error"),
			TEXT("Icons.Search"),
			TEXT("Icons.Cross"),
			TEXT("Icons.Plus"),
			TEXT("Icons.Download"),
			TEXT("Icons.Refresh"),
			TEXT("LevelEditor.Tabs.Debug"),
			TEXT("PlayWorld.StepInto"),
			TEXT("ClassIcon.Actor"),
			TEXT("ClassIcon.Pawn"),
			TEXT("ClassIcon.Character"),
			TEXT("ClassIcon.PointLight"),
			TEXT("ClassIcon.SoundCue"),
			TEXT("ClassIcon.Camera"),
			TEXT("ClassIcon.World"),
		};

		for (const TCHAR* Candidate : Candidates)
		{
			const FName Name(Candidate);
			if (FEditorStyle::GetOptionalBrush(Name, nullptr, nullptr) != nullptr)
			{
				Catalog.Add(Name);
			}
		}
	}
	return Catalog;
}
FText FNodeFactoryStyle::GetIconDisplayName(FName BrushName)
{
	FString Name = BrushName.ToString();
	int32 DotIndex = INDEX_NONE;
	if (Name.FindLastChar(TEXT('.'), DotIndex))
	{
		Name = Name.Mid(DotIndex + 1);
	}
	Name.RemoveFromEnd(TEXT("_16x"));
	Name.RemoveFromEnd(TEXT("_16"));
	return FText::FromString(FName::NameToDisplayString(Name, false));
}
#undef LOCTEXT_NAMESPACE
