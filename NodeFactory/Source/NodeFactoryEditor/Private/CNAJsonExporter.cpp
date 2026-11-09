// Copyright (c) NodeFactory. Export of a Custom Node Asset to one readable JSON file (spec 8.1).

#include "CNAJsonExporter.h"
#include "CNAGraphUtils.h"
#include "CNAJsonSchema.h"
#include "CNAMessages.h"
#include "CNAPinUtils.h"
#include "CustomNodeAsset.h"
#include "NodeFactoryModule.h"
#include "DesktopPlatformModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphUtilities.h"
#include "EditorDirectories.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDesktopPlatform.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNAJsonExporterLocal
{
	FString NameToString(FName Name)
	{
		return Name.IsNone() ? FString() : Name.ToString();
	}

	FString ObjectToPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	TSharedRef<FJsonObject> BuildAppearance(const UCustomNodeAsset* Asset)
	{
		using namespace CNAJsonFields;
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(UseHeaderColor, Asset->bUseHeaderColor);
		Object->SetField(HeaderColor, FCNAJsonSchema::ColorToJson(Asset->HeaderColor));
		Object->SetField(HeaderTextColor, FCNAJsonSchema::ColorToJson(Asset->HeaderTextColor));
		Object->SetField(PinLabelColor, FCNAJsonSchema::ColorToJson(Asset->PinLabelColor));
		Object->SetStringField(Icon, NameToString(Asset->Icon));
		Object->SetField(IconTint, FCNAJsonSchema::ColorToJson(Asset->IconTint));
		Object->SetStringField(IconTexture, Asset->IconTexture.IsNull() ? FString() : Asset->IconTexture.ToSoftObjectPath().ToString());
		Object->SetField(IconTextureTint, FCNAJsonSchema::ColorToJson(Asset->IconTextureTint));
		Object->SetStringField(CornerIcon, FCNAJsonSchema::CornerIconToString(Asset->CornerIcon));
		Object->SetStringField(CornerIconTexture, Asset->CornerIconTexture.IsNull() ? FString() : Asset->CornerIconTexture.ToSoftObjectPath().ToString());
		Object->SetField(CornerIconTextureTint, FCNAJsonSchema::ColorToJson(Asset->CornerIconTextureTint));
		Object->SetNumberField(CornerIconTextureSize, Asset->CornerIconTextureSize);

		TArray<TSharedPtr<FJsonValue>> BadgeValues;
		for (const FCNABadge& Badge : Asset->Badges)
		{
			TSharedRef<FJsonObject> BadgeObject = MakeShared<FJsonObject>();
			BadgeObject->SetField(Text, FCNAJsonSchema::TextToJson(Badge.Text));
			BadgeObject->SetField(Color, FCNAJsonSchema::ColorToJson(Badge.Color));
			BadgeObject->SetField(TextColor, FCNAJsonSchema::ColorToJson(Badge.TextColor));
			BadgeObject->SetStringField(Corner, FCNAJsonSchema::BadgeCornerToString(Badge.Corner));
			BadgeValues.Add(MakeShared<FJsonValueObject>(BadgeObject));
		}
		Object->SetArrayField(Badges, BadgeValues);

		Object->SetNumberField(MinWidth, Asset->MinWidth);
		Object->SetNumberField(MinHeight, Asset->MinHeight);

		TArray<TSharedPtr<FJsonValue>> WidgetValues;
		for (const FCNABodyWidget& Widget : Asset->BodyWidgets)
		{
			TSharedRef<FJsonObject> WidgetObject = MakeShared<FJsonObject>();
			WidgetObject->SetStringField(PinName, NameToString(Widget.PinName));
			WidgetObject->SetStringField(Type, FCNAJsonSchema::BodyWidgetTypeToString(Widget.Type));
			WidgetObject->SetArrayField(Options, FCNAJsonSchema::StringsToJson(Widget.Options));
			WidgetObject->SetField(Label, FCNAJsonSchema::TextToJson(Widget.Label));
			WidgetObject->SetNumberField(Width, Widget.Width);
			WidgetObject->SetNumberField(Height, Widget.Height);
			WidgetObject->SetNumberField(SliderMin, Widget.SliderMin);
			WidgetObject->SetNumberField(SliderMax, Widget.SliderMax);
			WidgetValues.Add(MakeShared<FJsonValueObject>(WidgetObject));
		}
		Object->SetArrayField(BodyWidgets, WidgetValues);

		Object->SetBoolField(ColorWires, Asset->bColorWires);
		Object->SetBoolField(Compact, Asset->bCompact);
		Object->SetField(CompactTitle, FCNAJsonSchema::TextToJson(Asset->CompactTitle));
		return Object;
	}

	TSharedRef<FJsonObject> BuildFlags(const UCustomNodeAsset* Asset)
	{
		using namespace CNAJsonFields;
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(Pure, Asset->bPure);
		Object->SetBoolField(DevelopmentOnly, Asset->bDevelopmentOnly);
		Object->SetBoolField(Deprecated, Asset->bDeprecated);
		Object->SetField(DeprecationMessage, FCNAJsonSchema::TextToJson(Asset->DeprecationMessage));
		Object->SetField(AllowedGraphs, FCNAJsonSchema::GraphKindsToJson(Asset->AllowedGraphs));
		Object->SetStringField(ContextClass, ObjectToPath(Asset->ContextClass.Get()));
		Object->SetBoolField(HiddenInMenu, Asset->bHiddenInMenu);
		Object->SetBoolField(AllowInstanceLabel, Asset->bAllowInstanceLabel);
		return Object;
	}

	TArray<TSharedPtr<FJsonValue>> BuildPinOverrides(const UCustomNodeAsset* Asset)
	{
		using namespace CNAJsonFields;
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FCNAPinOverride& Override : Asset->PinOverrides)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(PinName, NameToString(Override.PinName));
			Object->SetField(DisplayName, FCNAJsonSchema::TextToJson(Override.DisplayName));
			Object->SetField(Tooltip, FCNAJsonSchema::TextToJson(Override.Tooltip));
			Object->SetBoolField(Advanced, Override.bAdvanced);
			Object->SetBoolField(HiddenByDefault, Override.bHiddenByDefault);
			Object->SetBoolField(Required, Override.bRequired);
			Object->SetBoolField(DefaultToSelf, Override.bDefaultToSelf);
			Object->SetStringField(PassThroughOf, NameToString(Override.PassThroughOf));
			Object->SetBoolField(DynamicGroup, Override.bDynamicGroup);
			Object->SetNumberField(DynamicMin, Override.DynamicMin);
			Object->SetNumberField(DynamicMax, Override.DynamicMax);
			Object->SetField(DynamicNameFormat, FCNAJsonSchema::TextToJson(Override.DynamicNameFormat));
			Object->SetStringField(ConnectionRule, FCNAJsonSchema::ConnectionRuleToString(Override.ConnectionRule));
			Object->SetStringField(ClassFilter, ObjectToPath(Override.ClassFilter.Get()));
			Object->SetBoolField(OverridePinColor, Override.bOverridePinColor);
			Object->SetField(PinColor, FCNAJsonSchema::ColorToJson(Override.PinColor));
			Object->SetBoolField(OverrideLabelColor, Override.bOverrideLabelColor);
			Object->SetField(LabelColor, FCNAJsonSchema::ColorToJson(Override.LabelColor));
			Values.Add(MakeShared<FJsonValueObject>(Object));
		}
		return Values;
	}

	TArray<TSharedPtr<FJsonValue>> BuildPinRedirects(const UCustomNodeAsset* Asset)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FCNAPinRedirect& Redirect : Asset->PinRedirects)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(CNAJsonFields::OldName, NameToString(Redirect.OldName));
			Object->SetStringField(CNAJsonFields::NewName, NameToString(Redirect.NewName));
			Values.Add(MakeShared<FJsonValueObject>(Object));
		}
		return Values;
	}

	TArray<TSharedPtr<FJsonValue>> BuildLocalVariables(const UCustomNodeAsset* Asset)
	{
		using namespace CNAJsonFields;
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FCNALocalVariable& Variable : Asset->LocalVariables)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			// The GUID is part of the data: Get Local and Set Local nodes in the T3D refer to it (spec 4.8).
			Object->SetStringField(Guid, Variable.Guid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetStringField(Name, NameToString(Variable.Name));
			Object->SetObjectField(Type, FCNAJsonSchema::PinTypeToJson(Variable.Type));
			Object->SetStringField(DefaultValue, Variable.DefaultValue);
			Object->SetField(Tooltip, FCNAJsonSchema::TextToJson(Variable.Tooltip));
			Values.Add(MakeShared<FJsonValueObject>(Object));
		}
		return Values;
	}

	TSharedRef<FJsonObject> BuildMeta(const UCustomNodeAsset* Asset)
	{
		using namespace CNAJsonFields;
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(Author, Asset->Author);
		Object->SetStringField(Description, Asset->Description);
		Object->SetArrayField(Tags, FCNAJsonSchema::StringsToJson(Asset->Tags));
		Object->SetStringField(SourceUrl, Asset->SourceUrl);
		return Object;
	}

	void ShowNotification(const FText& Text, bool bSuccess)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		FNotificationInfo Info(Text);
		Info.ExpireDuration = bSuccess ? 4.0f : 8.0f;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

// Document

TSharedRef<FJsonObject> FCNAJsonExporter::BuildJson(const UCustomNodeAsset* Asset)
{
	using namespace CNAJsonFields;
	using namespace CNAJsonExporterLocal;

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	if (!Asset)
	{
		return Root;
	}

	Root->SetNumberField(SchemaVersion, static_cast<double>(FCNAJsonSchema::CurrentVersion));
	Root->SetStringField(EngineVersion, GetEngineVersionString());
	Root->SetStringField(PluginVersion, GetPluginVersionString());
	Root->SetStringField(Id, Asset->AssetGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Root->SetStringField(Name, Asset->GetName());

	Root->SetField(DisplayName, FCNAJsonSchema::TextToJson(Asset->DisplayName));
	Root->SetField(Subtitle, FCNAJsonSchema::TextToJson(Asset->Subtitle));
	Root->SetField(Tooltip, FCNAJsonSchema::TextToJson(Asset->Tooltip));
	Root->SetField(Category, FCNAJsonSchema::TextToJson(Asset->Category));
	Root->SetField(Keywords, FCNAJsonSchema::TextToJson(Asset->Keywords));
	Root->SetField(DefaultComment, FCNAJsonSchema::TextToJson(Asset->DefaultComment));

	Root->SetObjectField(Appearance, BuildAppearance(Asset));
	Root->SetObjectField(Flags, BuildFlags(Asset));
	Root->SetArrayField(PinOverrides, BuildPinOverrides(Asset));
	Root->SetArrayField(PinRedirects, BuildPinRedirects(Asset));
	Root->SetArrayField(LocalVariables, BuildLocalVariables(Asset));

	// Graphs: signature for readers and checks, T3D for the nodes (spec D8). The exporter does not change the
	// asset; PrepareForCopying is the call the Blueprint editor makes before a copy.
	UCustomNodeAsset* MutableAsset = const_cast<UCustomNodeAsset*>(Asset);
	TSharedRef<FJsonObject> GraphObject = MakeShared<FJsonObject>();
	GraphObject->SetArrayField(Signature, BuildSignatureJson(Asset));
	GraphObject->SetArrayField(T3D, FCNAJsonSchema::SplitLines(ExportGraphToT3D(MutableAsset->FindExecuteGraph())));
	Root->SetObjectField(Graph, GraphObject);

	TSharedRef<FJsonObject> BehaviorObject = MakeShared<FJsonObject>();
	BehaviorObject->SetArrayField(T3D, FCNAJsonSchema::SplitLines(ExportGraphToT3D(MutableAsset->FindBehaviorGraph())));
	Root->SetObjectField(Behavior, BehaviorObject);

	Root->SetArrayField(Textures, FCNAJsonSchema::StringsToJson(GetTexturePaths(Asset)));
	Root->SetObjectField(Meta, BuildMeta(Asset));
	return Root;
}

FString FCNAJsonExporter::ExportToString(const UCustomNodeAsset* Asset)
{
	return FCNAJsonSchema::Write(BuildJson(Asset));
}

bool FCNAJsonExporter::ExportToFile(const UCustomNodeAsset* Asset, const FString& FilePath, FText& OutError)
{
	if (!Asset)
	{
		OutError = LOCTEXT("Export.NoAsset", "No asset to export.");
		return false;
	}
	if (FilePath.IsEmpty())
	{
		OutError = LOCTEXT("Export.NoPath", "No file name given.");
		return false;
	}

	const FString Text = ExportToString(Asset);
	if (!FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FText::Format(LOCTEXT("Export.WriteFailed", "Could not write {0}. Check that the folder exists and the file is not read only."),
			FText::FromString(FilePath));
		return false;
	}

	UE_LOG(LogNodeFactory, Log, TEXT("%s exported to %s (%d characters)."), *Asset->GetName(), *FilePath, Text.Len());
	return true;
}

bool FCNAJsonExporter::PromptAndExport(UCustomNodeAsset* Asset)
{
	using namespace CNAJsonExporterLocal;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!Asset || !DesktopPlatform || !FSlateApplication::IsInitialized())
	{
		return false;
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const FString DefaultPath = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_EXPORT);
	TArray<FString> Files;
	const bool bPicked = DesktopPlatform->SaveFileDialog(
		ParentWindowHandle,
		FText::Format(LOCTEXT("Export.DialogTitle", "Export {0} to JSON"), FText::FromString(Asset->GetName())).ToString(),
		DefaultPath,
		GetDefaultFileName(Asset),
		TEXT("Custom Node JSON (*.json)|*.json"),
		EFileDialogFlags::None,
		Files);
	if (!bPicked || Files.Num() == 0)
	{
		return false;
	}

	FString FilePath = Files[0];
	if (!FilePath.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase))
	{
		FilePath += TEXT(".json");
	}
	FEditorDirectories::Get().SetLastDirectory(ELastDirectory::GENERIC_EXPORT, FPaths::GetPath(FilePath));

	FText Error;
	if (!ExportToFile(Asset, FilePath, Error))
	{
		FCNAMessages::ReportTextToLogWithSeverity(EMessageSeverity::Error, Error, Asset);
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return false;
	}

	const FText Done = FText::Format(LOCTEXT("Export.Done", "{0} exported to {1}"), FText::FromString(Asset->GetName()), FText::FromString(FilePath));
	FCNAMessages::LogInfo(Done, Asset);
	ShowNotification(Done, true);
	return true;
}

// Parts

FString FCNAJsonExporter::ExportGraphToT3D(UEdGraph* Graph)
{
	if (!Graph)
	{
		return FString();
	}

	// The set keeps the order of the graph, so the text is stable between exports.
	TSet<UObject*> Nodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			Node->PrepareForCopying();
			Nodes.Add(Node);
		}
	}
	if (Nodes.Num() == 0)
	{
		return FString();
	}

	FString Text;
	FEdGraphUtilities::ExportNodesToText(Nodes, Text);
	return Text.Replace(TEXT("\r\n"), TEXT("\n"));
}

TArray<TSharedPtr<FJsonValue>> FCNAJsonExporter::BuildSignatureJson(const UCustomNodeAsset* Asset)
{
	using namespace CNAJsonFields;
	TArray<TSharedPtr<FJsonValue>> Values;
	TArray<FCNASignaturePin> Pins;
	if (!FCNAGraphUtils::GetSignature(Asset, Pins))
	{
		return Values;
	}
	for (const FCNASignaturePin& Pin : Pins)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(Name, Pin.Name.ToString());
		Object->SetStringField(Direction, Pin.IsInput() ? TEXT("Input") : TEXT("Output"));
		Object->SetObjectField(Type, FCNAJsonSchema::PinTypeToJson(Pin.Type));
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}
	return Values;
}

FString FCNAJsonExporter::GetDefaultFileName(const UCustomNodeAsset* Asset)
{
	return Asset ? Asset->GetName() + TEXT(".json") : FString(TEXT("CustomNode.json"));
}

FString FCNAJsonExporter::GetEngineVersionString()
{
	const FEngineVersion& Version = FEngineVersion::Current();
	return FString::Printf(TEXT("%d.%d.%d"), Version.GetMajor(), Version.GetMinor(), Version.GetPatch());
}

FString FCNAJsonExporter::GetPluginVersionString()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeFactory"));
	return Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString(TEXT("unknown"));
}

TArray<FString> FCNAJsonExporter::GetTexturePaths(const UCustomNodeAsset* Asset)
{
	TArray<FString> Paths;
	if (Asset && !Asset->IconTexture.IsNull())
	{
		Paths.AddUnique(Asset->IconTexture.ToSoftObjectPath().ToString());
	}
	if (Asset && !Asset->CornerIconTexture.IsNull()) Paths.AddUnique(Asset->CornerIconTexture.ToSoftObjectPath().ToString());
	return Paths;
}

#undef LOCTEXT_NAMESPACE
