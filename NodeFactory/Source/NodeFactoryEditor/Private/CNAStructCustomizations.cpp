// Copyright (c) NodeFactory. Details of the structs of a Custom Node Asset (visibility by choice, plugin language).

#include "CNAStructCustomizations.h"
#include "CNAGraphUtils.h"
#include "CNAPinUtils.h"
#include "CNATypes.h"
#include "CustomNodeAsset.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "EdGraphSchema_K2.h"
#include "EditorStyleSet.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

// Texts. Every LOCTEXT stays literal so the GatherText commandlet finds it; the tables are built inside
// FCNALocalization::FScopedDetailsLanguage, which makes them Russian in the mode Russian & English.

namespace CNADetails
{
	void GetAssetTexts(FPropertyTextMap& Out)
	{
		// Node
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, DisplayName), FPropertyText(LOCTEXT("Prop.Asset.DisplayName", "Display Name"), LOCTEXT("Prop.Asset.DisplayName.Tip", "Title of the node. Empty: the asset name without the CNA_ prefix.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Subtitle), FPropertyText(LOCTEXT("Prop.Asset.Subtitle", "Subtitle"), LOCTEXT("Prop.Asset.Subtitle.Tip", "Second line of the title in a smaller font.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Tooltip), FPropertyText(LOCTEXT("Prop.Asset.Tooltip", "Tooltip"), LOCTEXT("Prop.Asset.Tooltip.Tip", "Tooltip of the node in the graph and in the action menu.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Category), FPropertyText(LOCTEXT("Prop.Asset.Category", "Category"), LOCTEXT("Prop.Asset.Category.Tip", "Menu category; nesting with a vertical bar: \"Math|Vector\".")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Keywords), FPropertyText(LOCTEXT("Prop.Asset.Keywords", "Keywords"), LOCTEXT("Prop.Asset.Keywords.Tip", "Search words of the action menu.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bHiddenInMenu), FPropertyText(LOCTEXT("Prop.Asset.bHiddenInMenu", "Hidden In Menu"), LOCTEXT("Prop.Asset.bHiddenInMenu.Tip", "The node is not offered by the menu but keeps working in existing graphs.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, DefaultComment), FPropertyText(LOCTEXT("Prop.Asset.DefaultComment", "Default Comment"), LOCTEXT("Prop.Asset.DefaultComment.Tip", "Comment that appears when the node is placed.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bAllowInstanceLabel), FPropertyText(LOCTEXT("Prop.Asset.bAllowInstanceLabel", "Allow Instance Label"), LOCTEXT("Prop.Asset.bAllowInstanceLabel.Tip", "Allows renaming the instance: the label is shown as the second line.")));
		// Appearance
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, HeaderColor), FPropertyText(LOCTEXT("Prop.Asset.HeaderColor", "Header Color"), LOCTEXT("Prop.Asset.HeaderColor.Tip", "Color of the title bar; the checkbox on the left turns it on. Without it the colors of the Graph Editor settings or of the style are used.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, HeaderTextColor), FPropertyText(LOCTEXT("Prop.Asset.HeaderTextColor", "Header Text Color"), LOCTEXT("Prop.Asset.HeaderTextColor.Tip", "Color of the title and subtitle text.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinLabelColor), FPropertyText(LOCTEXT("Prop.Asset.PinLabelColor", "Pin Label Color"), LOCTEXT("Prop.Asset.PinLabelColor.Tip", "Default color of all pin labels; per pin through Label Color of the pin override.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bColorWires), FPropertyText(LOCTEXT("Prop.Asset.bColorWires", "Color Wires"), LOCTEXT("Prop.Asset.bColorWires.Tip", "Wires connected to a pin with its own Pin Color get that color. Shown once a pin override sets a pin color.")));
		// Icon
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Icon), FPropertyText(LOCTEXT("Prop.Asset.Icon", "Icon"), LOCTEXT("Prop.Asset.Icon.Tip", "Brush of the editor style in front of the title, like Branch or Function. Hidden while an Icon Texture is set.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, IconTint), FPropertyText(LOCTEXT("Prop.Asset.IconTint", "Icon Tint"), LOCTEXT("Prop.Asset.IconTint.Tip", "Color of the icon.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, IconTexture), FPropertyText(LOCTEXT("Prop.Asset.IconTexture", "Icon Texture"), LOCTEXT("Prop.Asset.IconTexture.Tip", "Texture shown instead of Icon; the UI texture group without mips is recommended.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, IconTextureTint), FPropertyText(LOCTEXT("Prop.Asset.IconTextureTint", "Icon Texture Tint"), LOCTEXT("Prop.Asset.IconTextureTint.Tip", "Color multiplied with the icon texture.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIconTexture), FPropertyText(LOCTEXT("Prop.Asset.CornerIconTexture", "Corner Icon Texture"), LOCTEXT("Prop.Asset.CornerIconTexture.Tip", "Optional texture replacing the corner icon, including the latent icon.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIconTextureTint), FPropertyText(LOCTEXT("Prop.Asset.CornerIconTextureTint", "Corner Icon Texture Tint"), LOCTEXT("Prop.Asset.CornerIconTextureTint.Tip", "Color multiplied with the corner icon texture.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIconTextureSize), FPropertyText(LOCTEXT("Prop.Asset.CornerIconTextureSize", "Corner Icon Texture Size"), LOCTEXT("Prop.Asset.CornerIconTextureSize.Tip", "Size of the corner icon texture in slate units.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CornerIcon), FPropertyText(LOCTEXT("Prop.Asset.CornerIcon", "Corner Icon"), LOCTEXT("Prop.Asset.CornerIcon.Tip", "Default corner icon; a latent subgraph shows Latent. Hidden while a Corner Icon Texture is set.")));
		// Compact
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bCompact), FPropertyText(LOCTEXT("Prop.Asset.bCompact", "Compact"), LOCTEXT("Prop.Asset.bCompact.Tip", "Compact look without a title bar, like the math operators. Only for a pure node (Flags, Pure).")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, CompactTitle), FPropertyText(LOCTEXT("Prop.Asset.CompactTitle", "Compact Title"), LOCTEXT("Prop.Asset.CompactTitle.Tip", "Large text of the compact node, for example \"+\" or \"SUM\".")));
		// Widgets
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Badges), FPropertyText(LOCTEXT("Prop.Asset.Badges", "Badges"), LOCTEXT("Prop.Asset.Badges.Tip", "Text badges in the corners of the node.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, MinWidth), FPropertyText(LOCTEXT("Prop.Asset.MinWidth", "Min Width"), LOCTEXT("Prop.Asset.MinWidth.Tip", "Minimum width of the node in slate units, 0 for automatic.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, MinHeight), FPropertyText(LOCTEXT("Prop.Asset.MinHeight", "Min Height"), LOCTEXT("Prop.Asset.MinHeight.Tip", "Minimum height of the node in slate units, 0 for automatic.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, BodyWidgets), FPropertyText(LOCTEXT("Prop.Asset.BodyWidgets", "Body Widgets"), LOCTEXT("Prop.Asset.BodyWidgets.Tip", "Widgets on the body of the node that edit the values of input pins, and curve graphs.")));
		// Style
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, NodeStyle), FPropertyText(LOCTEXT("Prop.Asset.NodeStyle", "Node Style"), LOCTEXT("Prop.Asset.NodeStyle.Tip", "Visual family of the node. The pins keep working as Blueprint pins in every family.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, AnimStyle), FPropertyText(LOCTEXT("Prop.Asset.AnimStyle", "Anim Style"), LOCTEXT("Prop.Asset.AnimStyle.Tip", "Kind of Animation Blueprint node to imitate.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bExecPinsAsPosePins), FPropertyText(LOCTEXT("Prop.Asset.bExecPinsAsPosePins", "Exec Pins As Pose Pins"), LOCTEXT("Prop.Asset.bExecPinsAsPosePins.Tip", "Exec pins are drawn as pose pins, like the pose links of an AnimGraph.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bShowFastPathIcon), FPropertyText(LOCTEXT("Prop.Asset.bShowFastPathIcon", "Show Fast Path Icon"), LOCTEXT("Prop.Asset.bShowFastPathIcon.Tip", "Lightning \"fast path\" mark in the corner, like anim nodes that use the fast path.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, NiagaraStyle), FPropertyText(LOCTEXT("Prop.Asset.NiagaraStyle", "Niagara Style"), LOCTEXT("Prop.Asset.NiagaraStyle.Tip", "Kind of Niagara script node to imitate.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bNiagaraAddPinRows), FPropertyText(LOCTEXT("Prop.Asset.bNiagaraAddPinRows", "Niagara Add Pin Rows"), LOCTEXT("Prop.Asset.bNiagaraAddPinRows.Tip", "\"Add Pin\" rows of Map Get / Map Set for the dynamic pins of the node.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bNiagaraNamespaceLabels), FPropertyText(LOCTEXT("Prop.Asset.bNiagaraNamespaceLabels", "Niagara Namespace Labels"), LOCTEXT("Prop.Asset.bNiagaraNamespaceLabels.Tip", "Pin names like \"Module.Speed\" show the namespace dimmed, as in Niagara.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bExecPinsAsParameterMap), FPropertyText(LOCTEXT("Prop.Asset.bExecPinsAsParameterMap", "Exec Pins As Parameter Map"), LOCTEXT("Prop.Asset.bExecPinsAsParameterMap.Tip", "Exec pins are drawn as Parameter Map pins.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, NiagaraBodyText), FPropertyText(LOCTEXT("Prop.Asset.NiagaraBodyText", "Niagara Body Text"), LOCTEXT("Prop.Asset.NiagaraBodyText.Tip", "Text shown in the text area of a Custom HLSL node (a note, read-only on instances).")));
		// Flags
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bPure), FPropertyText(LOCTEXT("Prop.Asset.bPure", "Pure"), LOCTEXT("Prop.Asset.bPure.Tip", "Node without exec pins; values are computed on every access. The subgraph must not execute nodes (CNA004). Pure nodes can use the compact look.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bDevelopmentOnly), FPropertyText(LOCTEXT("Prop.Asset.bDevelopmentOnly", "Development Only"), LOCTEXT("Prop.Asset.bDevelopmentOnly.Tip", "The node gets the standard Development Only state.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, bDeprecated), FPropertyText(LOCTEXT("Prop.Asset.bDeprecated", "Deprecated"), LOCTEXT("Prop.Asset.bDeprecated.Tip", "Compiling a Blueprint that uses the node produces the warning CNA009.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, DeprecationMessage), FPropertyText(LOCTEXT("Prop.Asset.DeprecationMessage", "Deprecation Message"), LOCTEXT("Prop.Asset.DeprecationMessage.Tip", "Text of the warning CNA009.")));
		// Placement
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, AllowedGraphs), FPropertyText(LOCTEXT("Prop.Asset.AllowedGraphs", "Allowed Graphs"), LOCTEXT("Prop.Asset.AllowedGraphs.Tip", "Graphs where the node can be placed.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, ContextClass), FPropertyText(LOCTEXT("Prop.Asset.ContextClass", "Context Class"), LOCTEXT("Prop.Asset.ContextClass.Tip", "Class whose Blueprints may use the node (CNA012); also the output type of Get Caller.")));
		// Pins
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinOverrides), FPropertyText(LOCTEXT("Prop.Asset.PinOverrides", "Pin Overrides"), LOCTEXT("Prop.Asset.PinOverrides.Tip", "Settings of pins of the macro Execute, matched by pin name.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, PinRedirects), FPropertyText(LOCTEXT("Prop.Asset.PinRedirects", "Pin Redirects"), LOCTEXT("Prop.Asset.PinRedirects.Tip", "Old pin name -> new pin name: keeps the wires of placed nodes after a pin was renamed. Renaming a pin adds them.")));
		// Metadata
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Author), FPropertyText(LOCTEXT("Prop.Asset.Author", "Author"), LOCTEXT("Prop.Asset.Author.Tip", "Author of the node.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Description), FPropertyText(LOCTEXT("Prop.Asset.Description", "Description"), LOCTEXT("Prop.Asset.Description.Tip", "Description of the node for the Node Catalog and the Content Browser.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, Tags), FPropertyText(LOCTEXT("Prop.Asset.Tags", "Tags"), LOCTEXT("Prop.Asset.Tags.Tip", "Tags of the Node Catalog.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, SourceUrl), FPropertyText(LOCTEXT("Prop.Asset.SourceUrl", "Source Url"), LOCTEXT("Prop.Asset.SourceUrl.Tip", "Address of the documentation, shown as text in the tooltip.")));
		// Internal
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, AssetVersion), FPropertyText(LOCTEXT("Prop.Asset.AssetVersion", "Asset Version"), LOCTEXT("Prop.Asset.AssetVersion.Tip", "Grows when the pin structure changes; compared with the version of the placed nodes (CNA006).")));
		Out.Add(GET_MEMBER_NAME_CHECKED(UCustomNodeAsset, AssetGuid), FPropertyText(LOCTEXT("Prop.Asset.AssetGuid", "Asset Guid"), LOCTEXT("Prop.Asset.AssetGuid.Tip", "Identity of the asset, exported as \"id\" in JSON.")));
	}

	void GetPinOverrideTexts(FPropertyTextMap& Out)
	{
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, PinName), FPropertyText(LOCTEXT("Prop.PinOverride.PinName", "Pin Name"), LOCTEXT("Prop.PinOverride.PinName.Tip", "Pin of the macro Execute this override applies to; for a dynamic group the array input. Pick it from the list to avoid typos.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, DisplayName), FPropertyText(LOCTEXT("Prop.PinOverride.DisplayName", "Display Name"), LOCTEXT("Prop.PinOverride.DisplayName.Tip", "Display name of the pin. Empty: the pin name.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, Tooltip), FPropertyText(LOCTEXT("Prop.PinOverride.Tooltip", "Tooltip"), LOCTEXT("Prop.PinOverride.Tooltip.Tip", "Tooltip of the pin.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, bAdvanced), FPropertyText(LOCTEXT("Prop.PinOverride.bAdvanced", "Advanced"), LOCTEXT("Prop.PinOverride.bAdvanced.Tip", "The pin goes to the Advanced section behind the arrow of the node.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, bHiddenByDefault), FPropertyText(LOCTEXT("Prop.PinOverride.bHiddenByDefault", "Hidden By Default"), LOCTEXT("Prop.PinOverride.bHiddenByDefault.Tip", "The pin is hidden when the node is created; the Behavior can show it.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, bRequired), FPropertyText(LOCTEXT("Prop.PinOverride.bRequired", "Required"), LOCTEXT("Prop.PinOverride.bRequired.Tip", "Input only: without a wire and without a value the compilation fails with CNA008.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, bDefaultToSelf), FPropertyText(LOCTEXT("Prop.PinOverride.bDefaultToSelf", "Default To Self"), LOCTEXT("Prop.PinOverride.bDefaultToSelf.Tip", "Object input only: when no wire is connected the expansion connects Self.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, PassThroughOf), FPropertyText(LOCTEXT("Prop.PinOverride.PassThroughOf", "Pass Through Of"), LOCTEXT("Prop.PinOverride.PassThroughOf.Tip", "Output only: returns the value of the input with this name without computation (CNA015).")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, bDynamicGroup), FPropertyText(LOCTEXT("Prop.PinOverride.bDynamicGroup", "Dynamic Group"), LOCTEXT("Prop.PinOverride.bDynamicGroup.Tip", "Array input only: the node shows a set of element pins instead of the array.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, DynamicMin), FPropertyText(LOCTEXT("Prop.PinOverride.DynamicMin", "Dynamic Min"), LOCTEXT("Prop.PinOverride.DynamicMin.Tip", "Lower bound and initial number of element pins.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, DynamicMax), FPropertyText(LOCTEXT("Prop.PinOverride.DynamicMax", "Dynamic Max"), LOCTEXT("Prop.PinOverride.DynamicMax.Tip", "Upper bound of element pins.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, DynamicNameFormat), FPropertyText(LOCTEXT("Prop.PinOverride.DynamicNameFormat", "Dynamic Name Format"), LOCTEXT("Prop.PinOverride.DynamicNameFormat.Tip", "Display name of an element pin; {0} is the index starting at 0, {1} the index starting at 1. Empty: \"Name 0\".")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, ConnectionRule), FPropertyText(LOCTEXT("Prop.PinOverride.ConnectionRule", "Connection Rule"), LOCTEXT("Prop.PinOverride.ConnectionRule.Tip", "Restriction of the wires the pin accepts.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, ClassFilter), FPropertyText(LOCTEXT("Prop.PinOverride.ClassFilter", "Class Filter"), LOCTEXT("Prop.PinOverride.ClassFilter.Tip", "Class of the Class Filter rule: only object or class pins of this class or its children.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, PinColor), FPropertyText(LOCTEXT("Prop.PinOverride.PinColor", "Pin Color"), LOCTEXT("Prop.PinOverride.PinColor.Tip", "Any color of the pin instead of the color of its type; the checkbox on the left turns it on. Also colors the wires when Color Wires is set.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, LabelColor), FPropertyText(LOCTEXT("Prop.PinOverride.LabelColor", "Label Color"), LOCTEXT("Prop.PinOverride.LabelColor.Tip", "Color of the label of the pin; the checkbox on the left turns it on. Without it Pin Label Color of the asset is used.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinOverride, bDrawAsPosePin), FPropertyText(LOCTEXT("Prop.PinOverride.bDrawAsPosePin", "Draw As Pose Pin"), LOCTEXT("Prop.PinOverride.bDrawAsPosePin.Tip", "Draw the pin like a pose pin of the Animation Blueprint (person icon). Visual only.")));
	}

	void GetPinRedirectTexts(FPropertyTextMap& Out)
	{
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinRedirect, OldName), FPropertyText(LOCTEXT("Prop.PinRedirect.OldName", "Old Name"), LOCTEXT("Prop.PinRedirect.OldName.Tip", "Name the placed nodes may still use.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNAPinRedirect, NewName), FPropertyText(LOCTEXT("Prop.PinRedirect.NewName", "New Name"), LOCTEXT("Prop.PinRedirect.NewName.Tip", "Current name of the pin in the macro Execute.")));
	}

	void GetBadgeTexts(FPropertyTextMap& Out)
	{
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABadge, Text), FPropertyText(LOCTEXT("Prop.Badge.Text", "Text"), LOCTEXT("Prop.Badge.Text.Tip", "Text of the badge, for example PURE or ASYNC.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABadge, Color), FPropertyText(LOCTEXT("Prop.Badge.Color", "Color"), LOCTEXT("Prop.Badge.Color.Tip", "Background of the badge.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABadge, TextColor), FPropertyText(LOCTEXT("Prop.Badge.TextColor", "Text Color"), LOCTEXT("Prop.Badge.TextColor.Tip", "Color of the text of the badge.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABadge, Corner), FPropertyText(LOCTEXT("Prop.Badge.Corner", "Corner"), LOCTEXT("Prop.Badge.Corner.Tip", "Corner of the node the badge is drawn in.")));
	}

	void GetBodyWidgetTexts(FPropertyTextMap& Out)
	{
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, PinName), FPropertyText(LOCTEXT("Prop.BodyWidget.PinName", "Pin Name"), LOCTEXT("Prop.BodyWidget.PinName.Tip", "Input pin whose default value the widget edits.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Type), FPropertyText(LOCTEXT("Prop.BodyWidget.Type", "Type"), LOCTEXT("Prop.BodyWidget.Type.Tip", "Kind of widget. A Curve draws a graph on the node and creates pins of its own.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Options), FPropertyText(LOCTEXT("Prop.BodyWidget.Options", "Options"), LOCTEXT("Prop.BodyWidget.Options.Tip", "Entries of a Combo Box or a Scroll List.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Label), FPropertyText(LOCTEXT("Prop.BodyWidget.Label", "Label"), LOCTEXT("Prop.BodyWidget.Label.Tip", "Text left of the widget. Empty: the display name of the pin.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Width), FPropertyText(LOCTEXT("Prop.BodyWidget.Width", "Width"), LOCTEXT("Prop.BodyWidget.Width.Tip", "Width of the widget in slate units, 0 for automatic.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Height), FPropertyText(LOCTEXT("Prop.BodyWidget.Height", "Height"), LOCTEXT("Prop.BodyWidget.Height.Tip", "Height of a Scroll List or of the curve graph in slate units, 0 for the default.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, SliderMin), FPropertyText(LOCTEXT("Prop.BodyWidget.SliderMin", "Slider Min"), LOCTEXT("Prop.BodyWidget.SliderMin.Tip", "Lower bound of the slider.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, SliderMax), FPropertyText(LOCTEXT("Prop.BodyWidget.SliderMax", "Slider Max"), LOCTEXT("Prop.BodyWidget.SliderMax.Tip", "Upper bound of the slider; must be greater than Slider Min.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Curve), FPropertyText(LOCTEXT("Prop.BodyWidget.Curve", "Curve"), LOCTEXT("Prop.BodyWidget.Curve.Tip", "Settings of the curve: source, value type, mode and the names of the created pins.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNABodyWidget, WidgetGuid), FPropertyText(LOCTEXT("Prop.BodyWidget.WidgetGuid", "Widget Guid"), LOCTEXT("Prop.BodyWidget.WidgetGuid.Tip", "Stable identity; the Curve Value node and the curve pins reference the widget by it.")));
	}

	void GetCurveSettingsTexts(FPropertyTextMap& Out)
	{
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, ValueType), FPropertyText(LOCTEXT("Prop.CurveSettings.ValueType", "Value Type"), LOCTEXT("Prop.CurveSettings.ValueType.Tip", "Type of the value inside the macro Execute: Float (one curve), Vector (X, Y, Z) or Linear Color (R, G, B, A).")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, Mode), FPropertyText(LOCTEXT("Prop.CurveSettings.Mode", "Mode"), LOCTEXT("Prop.CurveSettings.Mode.Tip", "Evaluate At Time: the node gets a Time input. Playback: Play, Stop, Update and Finished pins like a Timeline (Event Graph only).")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bUseCurveAsset), FPropertyText(LOCTEXT("Prop.CurveSettings.bUseCurveAsset", "Use Curve Asset"), LOCTEXT("Prop.CurveSettings.bUseCurveAsset.Tip", "Use a Curve asset of the project instead of the own curve.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, CurveAsset), FPropertyText(LOCTEXT("Prop.CurveSettings.CurveAsset", "Curve Asset"), LOCTEXT("Prop.CurveSettings.CurveAsset.Tip", "Curve Float, Curve Vector or Curve Linear Color matching the value type.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, BakeSamples), FPropertyText(LOCTEXT("Prop.CurveSettings.BakeSamples", "Bake Samples"), LOCTEXT("Prop.CurveSettings.BakeSamples.Tip", "Own curve: number of values baked into the compiled Blueprint (linear interpolation between them).")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, Length), FPropertyText(LOCTEXT("Prop.CurveSettings.Length", "Length"), LOCTEXT("Prop.CurveSettings.Length.Tip", "Playback length in seconds; 0 uses the time of the last key.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bLoop), FPropertyText(LOCTEXT("Prop.CurveSettings.bLoop", "Loop"), LOCTEXT("Prop.CurveSettings.bLoop.Tip", "At the end the playback starts again from the beginning.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, PlayRate), FPropertyText(LOCTEXT("Prop.CurveSettings.PlayRate", "Play Rate"), LOCTEXT("Prop.CurveSettings.PlayRate.Tip", "Speed of the playback, 1 is normal speed.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bShowPlayFromStart), FPropertyText(LOCTEXT("Prop.CurveSettings.bShowPlayFromStart", "Show Play From Start"), LOCTEXT("Prop.CurveSettings.bShowPlayFromStart.Tip", "Create the Play from Start input.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bShowReverse), FPropertyText(LOCTEXT("Prop.CurveSettings.bShowReverse", "Show Reverse"), LOCTEXT("Prop.CurveSettings.bShowReverse.Tip", "Create the Reverse input.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, TimeLabel), FPropertyText(LOCTEXT("Prop.CurveSettings.TimeLabel", "Time Label"), LOCTEXT("Prop.CurveSettings.TimeLabel.Tip", "Name of the Time input on the node; empty uses \"Time\". Renaming keeps the wires.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, PlayLabel), FPropertyText(LOCTEXT("Prop.CurveSettings.PlayLabel", "Play Label"), LOCTEXT("Prop.CurveSettings.PlayLabel.Tip", "Name of the Play input on the node; empty uses \"Play\". Renaming keeps the wires.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, PlayFromStartLabel), FPropertyText(LOCTEXT("Prop.CurveSettings.PlayFromStartLabel", "Play From Start Label"), LOCTEXT("Prop.CurveSettings.PlayFromStartLabel.Tip", "Name of the Play from Start input; empty uses \"Play from Start\". Renaming keeps the wires.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, StopLabel), FPropertyText(LOCTEXT("Prop.CurveSettings.StopLabel", "Stop Label"), LOCTEXT("Prop.CurveSettings.StopLabel.Tip", "Name of the Stop input; empty uses \"Stop\". Renaming keeps the wires.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, ReverseLabel), FPropertyText(LOCTEXT("Prop.CurveSettings.ReverseLabel", "Reverse Label"), LOCTEXT("Prop.CurveSettings.ReverseLabel.Tip", "Name of the Reverse input; empty uses \"Reverse\". Renaming keeps the wires.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, UpdateLabel), FPropertyText(LOCTEXT("Prop.CurveSettings.UpdateLabel", "Update Label"), LOCTEXT("Prop.CurveSettings.UpdateLabel.Tip", "Name of the Update output; empty uses \"Update\". Renaming keeps the wires.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, FinishedLabel), FPropertyText(LOCTEXT("Prop.CurveSettings.FinishedLabel", "Finished Label"), LOCTEXT("Prop.CurveSettings.FinishedLabel.Tip", "Name of the Finished output; empty uses \"Finished\". Renaming keeps the wires.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, GraphBackgroundColor), FPropertyText(LOCTEXT("Prop.CurveSettings.GraphBackgroundColor", "Graph Background Color"), LOCTEXT("Prop.CurveSettings.GraphBackgroundColor.Tip", "Background of the graph drawn on the node.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bShowKeys), FPropertyText(LOCTEXT("Prop.CurveSettings.bShowKeys", "Show Keys"), LOCTEXT("Prop.CurveSettings.bShowKeys.Tip", "Draw the keys of the curve on the graph.")));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bShowGrid), FPropertyText(LOCTEXT("Prop.CurveSettings.bShowGrid", "Show Grid"), LOCTEXT("Prop.CurveSettings.bShowGrid.Tip", "Draw a grid behind the curve.")));

		// Own curve channels; the names follow the value type.
		const FText ChannelTip = LOCTEXT("Prop.CurveSettings.Channel.Tip", "Channel of the own curve: edit the keys in the graph. The compiled Blueprint gets Bake Samples values of it.");
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, CurveX), FPropertyText(LOCTEXT("Prop.CurveSettings.CurveX", "Curve X"), ChannelTip));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, CurveY), FPropertyText(LOCTEXT("Prop.CurveSettings.CurveY", "Curve Y"), ChannelTip));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, CurveZ), FPropertyText(LOCTEXT("Prop.CurveSettings.CurveZ", "Curve Z"), ChannelTip));
		Out.Add(GET_MEMBER_NAME_CHECKED(FCNACurveSettings, CurveW), FPropertyText(LOCTEXT("Prop.CurveSettings.CurveW", "Curve W"), ChannelTip));
	}

	/** Channel names by value type: Float "Curve"; Vector X, Y, Z; Linear Color R, G, B, A. */
	FText GetChannelName(ECNACurveValueType ValueType, int32 Channel)
	{
		if (ValueType == ECNACurveValueType::Float)
		{
			return LOCTEXT("Prop.CurveSettings.CurveFloat", "Curve");
		}
		if (ValueType == ECNACurveValueType::LinearColor)
		{
			switch (Channel)
			{
			case 0:  return LOCTEXT("Prop.CurveSettings.CurveR", "Curve R (Red)");
			case 1:  return LOCTEXT("Prop.CurveSettings.CurveG", "Curve G (Green)");
			case 2:  return LOCTEXT("Prop.CurveSettings.CurveB", "Curve B (Blue)");
			default: return LOCTEXT("Prop.CurveSettings.CurveA", "Curve A (Alpha)");
			}
		}
		switch (Channel)
		{
		case 0:  return LOCTEXT("Prop.CurveSettings.CurveX", "Curve X");
		case 1:  return LOCTEXT("Prop.CurveSettings.CurveY", "Curve Y");
		case 2:  return LOCTEXT("Prop.CurveSettings.CurveZ", "Curve Z");
		default: return LOCTEXT("Prop.CurveSettings.CurveW", "Curve W");
		}
	}

	FText GetEnumValueText(const UEnum* Enum, int64 Value)
	{
		if (!Enum)
		{
			return FText::GetEmpty();
		}
		static const FName CornerIconName(TEXT("ECNACornerIcon"));
		static const FName ConnectionRuleName(TEXT("ECNAConnectionRule"));
		static const FName BadgeCornerName(TEXT("ECNABadgeCorner"));
		static const FName BodyWidgetTypeName(TEXT("ECNABodyWidgetType"));
		static const FName NodeStyleName(TEXT("ECNANodeStyle"));
		static const FName AnimNodeStyleName(TEXT("ECNAAnimNodeStyle"));
		static const FName NiagaraNodeStyleName(TEXT("ECNANiagaraNodeStyle"));
		static const FName CurveValueTypeName(TEXT("ECNACurveValueType"));
		static const FName CurveModeName(TEXT("ECNACurveMode"));
		const FName EnumName = Enum->GetFName();

		if (EnumName == CornerIconName)
		{
			switch (static_cast<ECNACornerIcon>(Value))
			{
			case ECNACornerIcon::None:      return LOCTEXT("Enum.CornerIcon.None", "None");
			case ECNACornerIcon::Latent:    return LOCTEXT("Enum.CornerIcon.Latent", "Latent");
			case ECNACornerIcon::Server:    return LOCTEXT("Enum.CornerIcon.Server", "Server");
			case ECNACornerIcon::Client:    return LOCTEXT("Enum.CornerIcon.Client", "Client");
			case ECNACornerIcon::Multicast: return LOCTEXT("Enum.CornerIcon.Multicast", "Multicast");
			case ECNACornerIcon::Message:   return LOCTEXT("Enum.CornerIcon.Message", "Message");
			default: break;
			}
		}
		else if (EnumName == ConnectionRuleName)
		{
			switch (static_cast<ECNAConnectionRule>(Value))
			{
			case ECNAConnectionRule::None:         return LOCTEXT("Enum.ConnectionRule.None", "None");
			case ECNAConnectionRule::SameTypeOnly: return LOCTEXT("Enum.ConnectionRule.SameTypeOnly", "Same Type Only");
			case ECNAConnectionRule::ClassFilter:  return LOCTEXT("Enum.ConnectionRule.ClassFilter", "Class Filter");
			default: break;
			}
		}
		else if (EnumName == BadgeCornerName)
		{
			switch (static_cast<ECNABadgeCorner>(Value))
			{
			case ECNABadgeCorner::TopLeft:     return LOCTEXT("Enum.BadgeCorner.TopLeft", "Top Left");
			case ECNABadgeCorner::TopRight:    return LOCTEXT("Enum.BadgeCorner.TopRight", "Top Right");
			case ECNABadgeCorner::BottomLeft:  return LOCTEXT("Enum.BadgeCorner.BottomLeft", "Bottom Left");
			case ECNABadgeCorner::BottomRight: return LOCTEXT("Enum.BadgeCorner.BottomRight", "Bottom Right");
			default: break;
			}
		}
		else if (EnumName == BodyWidgetTypeName)
		{
			switch (static_cast<ECNABodyWidgetType>(Value))
			{
			case ECNABodyWidgetType::CheckBox:   return LOCTEXT("Enum.BodyWidgetType.CheckBox", "Check Box");
			case ECNABodyWidgetType::ComboBox:   return LOCTEXT("Enum.BodyWidgetType.ComboBox", "Combo Box");
			case ECNABodyWidgetType::TextBox:    return LOCTEXT("Enum.BodyWidgetType.TextBox", "Text Box");
			case ECNABodyWidgetType::ScrollList: return LOCTEXT("Enum.BodyWidgetType.ScrollList", "Scroll List");
			case ECNABodyWidgetType::Slider:     return LOCTEXT("Enum.BodyWidgetType.Slider", "Slider");
			case ECNABodyWidgetType::Curve:      return LOCTEXT("Enum.BodyWidgetType.Curve", "Curve");
			default: break;
			}
		}
		else if (EnumName == NodeStyleName)
		{
			switch (static_cast<ECNANodeStyle>(Value))
			{
			case ECNANodeStyle::Blueprint: return LOCTEXT("Enum.NodeStyle.Blueprint", "Blueprint");
			case ECNANodeStyle::Animation: return LOCTEXT("Enum.NodeStyle.Animation", "Animation");
			case ECNANodeStyle::Niagara:   return LOCTEXT("Enum.NodeStyle.Niagara", "Niagara");
			default: break;
			}
		}
		else if (EnumName == AnimNodeStyleName)
		{
			switch (static_cast<ECNAAnimNodeStyle>(Value))
			{
			case ECNAAnimNodeStyle::AnimNode:        return LOCTEXT("Enum.AnimStyle.AnimNode", "Anim Node");
			case ECNAAnimNodeStyle::AssetPlayer:     return LOCTEXT("Enum.AnimStyle.AssetPlayer", "Asset Player");
			case ECNAAnimNodeStyle::Blend:           return LOCTEXT("Enum.AnimStyle.Blend", "Blend");
			case ECNAAnimNodeStyle::BlendSimple:     return LOCTEXT("Enum.AnimStyle.BlendSimple", "Blend Simple");
			case ECNAAnimNodeStyle::SkeletalControl: return LOCTEXT("Enum.AnimStyle.SkeletalControl", "Skeletal Control");
			case ECNAAnimNodeStyle::SpaceConversion: return LOCTEXT("Enum.AnimStyle.SpaceConversion", "Space Conversion");
			case ECNAAnimNodeStyle::RefPose:         return LOCTEXT("Enum.AnimStyle.RefPose", "Ref Pose");
			case ECNAAnimNodeStyle::StateMachine:    return LOCTEXT("Enum.AnimStyle.StateMachine", "State Machine");
			case ECNAAnimNodeStyle::Result:          return LOCTEXT("Enum.AnimStyle.Result", "Result");
			case ECNAAnimNodeStyle::State:           return LOCTEXT("Enum.AnimStyle.State", "State");
			case ECNAAnimNodeStyle::Conduit:         return LOCTEXT("Enum.AnimStyle.Conduit", "Conduit");
			case ECNAAnimNodeStyle::Transition:      return LOCTEXT("Enum.AnimStyle.Transition", "Transition");
			case ECNAAnimNodeStyle::Entry:           return LOCTEXT("Enum.AnimStyle.Entry", "Entry");
			default: break;
			}
		}
		else if (EnumName == NiagaraNodeStyleName)
		{
			switch (static_cast<ECNANiagaraNodeStyle>(Value))
			{
			case ECNANiagaraNodeStyle::FunctionCall: return LOCTEXT("Enum.NiagaraStyle.FunctionCall", "Function Call");
			case ECNANiagaraNodeStyle::MapGet:       return LOCTEXT("Enum.NiagaraStyle.MapGet", "Map Get");
			case ECNANiagaraNodeStyle::MapSet:       return LOCTEXT("Enum.NiagaraStyle.MapSet", "Map Set");
			case ECNANiagaraNodeStyle::Op:           return LOCTEXT("Enum.NiagaraStyle.Op", "Op");
			case ECNANiagaraNodeStyle::Input:        return LOCTEXT("Enum.NiagaraStyle.Input", "Input");
			case ECNANiagaraNodeStyle::Output:       return LOCTEXT("Enum.NiagaraStyle.Output", "Output");
			case ECNANiagaraNodeStyle::CustomHlsl:   return LOCTEXT("Enum.NiagaraStyle.CustomHlsl", "Custom Hlsl");
			default: break;
			}
		}
		else if (EnumName == CurveValueTypeName)
		{
			switch (static_cast<ECNACurveValueType>(Value))
			{
			case ECNACurveValueType::Float:       return LOCTEXT("Enum.CurveValueType.Float", "Float");
			case ECNACurveValueType::Vector:      return LOCTEXT("Enum.CurveValueType.Vector", "Vector");
			case ECNACurveValueType::LinearColor: return LOCTEXT("Enum.CurveValueType.LinearColor", "Linear Color");
			default: break;
			}
		}
		else if (EnumName == CurveModeName)
		{
			switch (static_cast<ECNACurveMode>(Value))
			{
			case ECNACurveMode::EvaluateAtTime: return LOCTEXT("Enum.CurveMode.EvaluateAtTime", "Evaluate At Time");
			case ECNACurveMode::Playback:       return LOCTEXT("Enum.CurveMode.Playback", "Playback");
			default: break;
			}
		}
		return Enum->GetDisplayNameTextByValue(Value);
	}

	// Rows

	void LocalizeRow(IDetailPropertyRow& Row, const FPropertyTextMap& Texts, FName PropertyName)
	{
		const FPropertyText* Text = Texts.Find(PropertyName);
		if (Text && FCNALocalization::IsDetailsRussian())
		{
			Row.DisplayName(Text->Name);
			Row.ToolTip(Text->ToolTip);
		}
	}

	FText GetNameOverride(const FPropertyTextMap& Texts, FName PropertyName)
	{
		const FPropertyText* Text = Texts.Find(PropertyName);
		return (Text && FCNALocalization::IsDetailsRussian()) ? Text->Name : FText::GetEmpty();
	}

	FText GetToolTipOverride(const FPropertyTextMap& Texts, FName PropertyName)
	{
		const FPropertyText* Text = Texts.Find(PropertyName);
		return (Text && FCNALocalization::IsDetailsRussian()) ? Text->ToolTip : FText::GetEmpty();
	}

	UCustomNodeAsset* GetAsset(const TSharedPtr<IPropertyHandle>& Handle)
	{
		if (!Handle.IsValid())
		{
			return nullptr;
		}
		TArray<UObject*> Outers;
		Handle->GetOuterObjects(Outers);
		for (UObject* Outer : Outers)
		{
			if (UCustomNodeAsset* Asset = Cast<UCustomNodeAsset>(Outer))
			{
				return Asset;
			}
		}
		return nullptr;
	}

	bool ReadBool(const TSharedPtr<IPropertyHandle>& Handle, bool& OutValue)
	{
		return Handle.IsValid() && Handle->IsValidHandle() && Handle->GetValue(OutValue) == FPropertyAccess::Success;
	}

	bool ReadByte(const TSharedPtr<IPropertyHandle>& Handle, uint8& OutValue)
	{
		// V-check: GetValue(uint8&) reads enum class properties with a uint8 underlying type in 4.20.3.
		return Handle.IsValid() && Handle->IsValidHandle() && Handle->GetValue(OutValue) == FPropertyAccess::Success;
	}

	bool ReadName(const TSharedPtr<IPropertyHandle>& Handle, FName& OutValue)
	{
		return Handle.IsValid() && Handle->IsValidHandle() && Handle->GetValue(OutValue) == FPropertyAccess::Success;
	}

	bool ReadObjectSet(const TSharedPtr<IPropertyHandle>& Handle, bool& OutValue)
	{
		FString Path;
		// The formatted string never loads a soft reference.
		if (!Handle.IsValid() || !Handle->IsValidHandle() || Handle->GetValueAsFormattedString(Path) != FPropertyAccess::Success)
		{
			return false;
		}
		Path.TrimStartAndEndInline();
		OutValue = !Path.IsEmpty() && !Path.Equals(TEXT("None"), ESearchCase::IgnoreCase);
		return true;
	}

	TAttribute<EVisibility> MakeVisibility(TFunction<bool()> Predicate)
	{
		return TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([Predicate]()
		{
			return Predicate() ? EVisibility::Visible : EVisibility::Collapsed;
		}));
	}

	TSharedPtr<IPropertyHandle> Child(const TSharedRef<IPropertyHandle>& StructHandle, FName MemberName)
	{
		return StructHandle->GetChildHandle(MemberName);
	}

	UEnum* GetPropertyEnum(const TSharedPtr<IPropertyHandle>& Handle)
	{
		UProperty* Property = Handle.IsValid() ? Handle->GetProperty() : nullptr;
		if (UEnumProperty* EnumProperty = Cast<UEnumProperty>(Property))
		{
			return EnumProperty->GetEnum();
		}
		if (UByteProperty* ByteProperty = Cast<UByteProperty>(Property))
		{
			return ByteProperty->Enum;
		}
		return nullptr;
	}

	TSharedRef<SWidget> MakeEnumValueWidget(const TSharedRef<IPropertyHandle>& Handle)
	{
		UEnum* Enum = GetPropertyEnum(Handle);
		TWeakObjectPtr<UEnum> WeakEnum = Enum;
		return SNew(SComboButton)
			.ContentPadding(FMargin(2.0f, 1.0f))
			.OnGetMenuContent_Lambda([Handle, WeakEnum]() -> TSharedRef<SWidget>
			{
				FCNALocalization::FScopedDetailsLanguage Scope;
				FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
				const UEnum* MenuEnum = WeakEnum.Get();
				// The last entry of a reflected enum is the generated _MAX value.
				const int32 Count = MenuEnum ? MenuEnum->NumEnums() - 1 : 0;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					if (MenuEnum->HasMetaData(TEXT("Hidden"), Index))
					{
						continue;
					}
					const int64 Value = MenuEnum->GetValueByIndex(Index);
					const FString FullName = MenuEnum->GetNameByIndex(Index).ToString();
					MenuBuilder.AddMenuEntry(
						GetEnumValueText(MenuEnum, Value),
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([Handle, FullName]()
							{
								// The full name (EnumType::Value) is what the property imports.
								Handle->SetValueFromFormattedString(FullName);
							}),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda([Handle, Value]()
							{
								uint8 Current = 0;
								return ReadByte(Handle, Current) && static_cast<int64>(Current) == Value;
							})),
						NAME_None,
						EUserInterfaceActionType::RadioButton);
				}
				return MenuBuilder.MakeWidget();
			})
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text_Lambda([Handle, WeakEnum]()
				{
					FCNALocalization::FScopedDetailsLanguage Scope;
					uint8 Current = 0;
					if (!ReadByte(Handle, Current))
					{
						return LOCTEXT("Details.MultipleValues", "Multiple Values");
					}
					return GetEnumValueText(WeakEnum.Get(), Current);
				})
			];
	}

	bool FindSignaturePin(const UCustomNodeAsset* Asset, FName PinName, FCNASignaturePin& OutPin)
	{
		TArray<FCNASignaturePin> Pins;
		if (!Asset || PinName.IsNone() || !FCNAGraphUtils::GetSignature(Asset, Pins))
		{
			return false;
		}
		const FCNASignaturePin* Pin = FCNAGraphUtils::FindSignaturePin(Pins, PinName);
		if (!Pin)
		{
			return false;
		}
		OutPin = *Pin;
		return true;
	}

	TSharedRef<SWidget> MakePinNameValueWidget(const TSharedRef<IPropertyHandle>& Handle, EPinFilter Filter, TAttribute<bool> bIsPinReference, bool bResolveRedirects)
	{
		const TWeakObjectPtr<UCustomNodeAsset> WeakAsset = GetAsset(Handle);

		// Problem text, empty when the name is fine or cannot be checked.
		auto GetProblem = [Handle, WeakAsset, Filter, bIsPinReference, bResolveRedirects]() -> FText
		{
			FName Name;
			const UCustomNodeAsset* Asset = WeakAsset.Get();
			TArray<FCNASignaturePin> Pins;
			if (!bIsPinReference.Get() || !ReadName(Handle, Name) || Name.IsNone() || !Asset || !FCNAGraphUtils::GetSignature(Asset, Pins))
			{
				return FText::GetEmpty();
			}
			const FName Target = bResolveRedirects ? Asset->ResolvePinRedirect(Name) : Name;
			const FCNASignaturePin* Pin = FCNAGraphUtils::FindSignaturePin(Pins, Target);
			FCNALocalization::FScopedDetailsLanguage Scope;
			if (!Pin)
			{
				return FText::Format(LOCTEXT("Details.PinMissing", "The macro Execute has no pin named {0} (CNA014). Pick a pin from the list, or use Stale References in the Pins category."),
					FText::FromName(Target));
			}
			if ((Filter == EPinFilter::Inputs && !Pin->IsInput()) || (Filter == EPinFilter::Outputs && Pin->IsInput()))
			{
				return FText::Format(Filter == EPinFilter::Inputs
					? LOCTEXT("Details.PinNotInput", "{0} is an output; an input pin is needed here.")
					: LOCTEXT("Details.PinNotOutput", "{0} is an input; an output pin is needed here."), FText::FromName(Target));
			}
			return FText::GetEmpty();
		};

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				Handle->CreatePropertyValueWidget(/*bDisplayDefaultPropertyButtons*/ false)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Icons.Warning"))
				.ToolTipText_Lambda(GetProblem)
				.Visibility_Lambda([GetProblem]() { return GetProblem().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.HasDownArrow(true)
				.ContentPadding(FMargin(2.0f, 0.0f))
				.Visibility_Lambda([bIsPinReference]() { return bIsPinReference.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
				.ToolTipText_Lambda([]()
				{
					FCNALocalization::FScopedDetailsLanguage Scope;
					return LOCTEXT("Details.PickPinTip", "Pick a pin of the macro Execute.");
				})
				.OnGetMenuContent_Lambda([Handle, WeakAsset, Filter]() -> TSharedRef<SWidget>
				{
					FCNALocalization::FScopedDetailsLanguage Scope;
					FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
					TArray<FCNASignaturePin> Pins;
					if (WeakAsset.IsValid())
					{
						FCNAGraphUtils::GetSignature(WeakAsset.Get(), Pins);
					}
					int32 Added = 0;
					for (const FCNASignaturePin& Pin : Pins)
					{
						if ((Filter == EPinFilter::Inputs && !Pin.IsInput()) || (Filter == EPinFilter::Outputs && Pin.IsInput()))
						{
							continue;
						}
						const FName PinName = Pin.Name;
						const FText Label = FText::Format(Pin.IsInput()
							? LOCTEXT("Details.PinEntryInput", "{0}  (input, {1})")
							: LOCTEXT("Details.PinEntryOutput", "{0}  (output, {1})"), FText::FromName(PinName), FCNAPinUtils::GetTypeText(Pin.Type));
						MenuBuilder.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([Handle, PinName]() { Handle->SetValue(PinName); }),
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([Handle, PinName]()
								{
									FName Current;
									return ReadName(Handle, Current) && Current == PinName;
								})),
							NAME_None, EUserInterfaceActionType::RadioButton);
						++Added;
					}
					if (Added == 0)
					{
						MenuBuilder.AddMenuEntry(LOCTEXT("Details.NoPins", "The macro Execute has no matching pins."), FText::GetEmpty(), FSlateIcon(),
							FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
					}
					return MenuBuilder.MakeWidget();
				})
			];
	}

	bool IsInlineToggle(const TSharedPtr<IPropertyHandle>& Handle)
	{
		const UProperty* Property = Handle.IsValid() ? Handle->GetProperty() : nullptr;
		return Property && Property->HasMetaData(TEXT("InlineEditConditionToggle"));
	}

	IDetailPropertyRow& AddChildRow(IDetailChildrenBuilder& Children, const TSharedRef<IPropertyHandle>& Handle,
		const FPropertyTextMap& Texts, const TAttribute<EVisibility>& Visibility)
	{
		const FName Name = Handle->GetProperty() ? Handle->GetProperty()->GetFName() : NAME_None;
		IDetailPropertyRow& Row = Children.AddProperty(Handle);
		LocalizeRow(Row, Texts, Name);
		Row.Visibility(Visibility);
		// Enum values in Russian need a combo of our own; English keeps the engine combo.
		if (FCNALocalization::IsDetailsRussian() && GetPropertyEnum(Handle) && !Handle->IsEditConst())
		{
			Row.CustomWidget()
				.NameContent()
				[
					Handle->CreatePropertyNameWidget(GetNameOverride(Texts, Name), GetToolTipOverride(Texts, Name))
				]
				.ValueContent()
				.MinDesiredWidth(150.0f)
				[
					MakeEnumValueWidget(Handle)
				];
		}
		return Row;
	}

	/** Every child of a struct in declaration order. */
	void GetChildren(const TSharedRef<IPropertyHandle>& StructHandle, TArray<TSharedRef<IPropertyHandle>>& OutChildren)
	{
		uint32 Count = 0;
		StructHandle->GetNumChildren(Count);
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			TSharedPtr<IPropertyHandle> ChildHandle = StructHandle->GetChildHandle(Index);
			if (ChildHandle.IsValid() && ChildHandle->GetProperty())
			{
				OutChildren.Add(ChildHandle.ToSharedRef());
			}
		}
	}

	/** Name row with a text that follows the value (array elements show what they configure). */
	TSharedRef<SWidget> MakeHeaderName(TFunction<FText()> GetText)
	{
		return SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
			.Text_Lambda([GetText]()
			{
				FCNALocalization::FScopedDetailsLanguage Scope;
				return GetText();
			});
	}

	void RequestRefreshOnChange(const TSharedPtr<IPropertyHandle>& Handle, IPropertyTypeCustomizationUtils& Utils)
	{
		if (!Handle.IsValid())
		{
			return;
		}
		TWeakPtr<IPropertyUtilities> Utilities = Utils.GetPropertyUtilities();
		Handle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utilities]()
		{
			if (TSharedPtr<IPropertyUtilities> Pinned = Utilities.Pin())
			{
				Pinned->RequestRefresh();
			}
		}));
	}
}

// FCNAPinOverrideDetails

TSharedRef<IPropertyTypeCustomization> FCNAPinOverrideDetails::MakeInstance()
{
	return MakeShareable(new FCNAPinOverrideDetails);
}

void FCNAPinOverrideDetails::CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils)
{
	using namespace CNADetails;
	const TSharedPtr<IPropertyHandle> PinNameHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNAPinOverride, PinName));
	const TWeakObjectPtr<UCustomNodeAsset> WeakAsset = GetAsset(Handle);
	Header
		.NameContent()
		[
			MakeHeaderName([PinNameHandle]()
			{
				FName Name;
				if (!ReadName(PinNameHandle, Name))
				{
					return LOCTEXT("Details.MultipleValues", "Multiple Values");
				}
				return Name.IsNone() ? LOCTEXT("Details.NoPinName", "(no pin name)") : FText::FromName(Name);
			})
		]
		.ValueContent()
		.MinDesiredWidth(200.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Icons.Warning"))
				.ToolTipText_Lambda([]()
				{
					FCNALocalization::FScopedDetailsLanguage Scope;
					return LOCTEXT("Details.OverrideStale", "No pin of the macro Execute has this name (CNA014). Fix it in Pin Name or with Stale References.");
				})
				.Visibility_Lambda([PinNameHandle, WeakAsset]()
				{
					FName Name;
					TArray<FCNASignaturePin> Pins;
					const bool bCheckable = ReadName(PinNameHandle, Name) && WeakAsset.IsValid() && FCNAGraphUtils::GetSignature(WeakAsset.Get(), Pins);
					return (bCheckable && !FCNAGraphUtils::FindSignaturePin(Pins, Name)) ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				Handle->CreatePropertyValueWidget()
			]
		];
}

void FCNAPinOverrideDetails::CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils)
{
	using namespace CNADetails;
	FCNALocalization::FScopedDetailsLanguage Scope;
	FPropertyTextMap Texts;
	GetPinOverrideTexts(Texts);

	const TWeakObjectPtr<UCustomNodeAsset> WeakAsset = GetAsset(Handle);
	const TSharedPtr<IPropertyHandle> PinNameHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNAPinOverride, PinName));
	const TSharedPtr<IPropertyHandle> DynamicHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNAPinOverride, bDynamicGroup));
	const TSharedPtr<IPropertyHandle> RuleHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNAPinOverride, ConnectionRule));

	// The pin of the override; false when it cannot be found (then the settings stay visible).
	auto FindPin = [WeakAsset, PinNameHandle](FCNASignaturePin& OutPin) -> bool
	{
		FName Name;
		return ReadName(PinNameHandle, Name) && FindSignaturePin(WeakAsset.Get(), Name, OutPin);
	};
	// A setting that is on stays visible, so a value that no longer applies can still be seen and turned off.
	auto IsOn = [](const TSharedPtr<IPropertyHandle>& ValueHandle) -> bool
	{
		bool bValue = false;
		return ReadBool(ValueHandle, bValue) && bValue;
	};

	TArray<TSharedRef<IPropertyHandle>> ChildHandles;
	GetChildren(Handle, ChildHandles);
	for (const TSharedRef<IPropertyHandle>& ChildHandle : ChildHandles)
	{
		if (IsInlineToggle(ChildHandle))
		{
			continue;
		}
		const FName Name = ChildHandle->GetProperty()->GetFName();
		const TSharedPtr<IPropertyHandle> ValueHandle = ChildHandle;
		TAttribute<EVisibility> Visibility = EVisibility::Visible;

		if (Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, bRequired))
		{
			Visibility = MakeVisibility([FindPin, IsOn, ValueHandle]()
			{
				FCNASignaturePin Pin;
				return !FindPin(Pin) || (Pin.IsInput() && !Pin.IsExec()) || IsOn(ValueHandle);
			});
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, bDefaultToSelf))
		{
			Visibility = MakeVisibility([FindPin, IsOn, ValueHandle]()
			{
				FCNASignaturePin Pin;
				if (!FindPin(Pin) || IsOn(ValueHandle))
				{
					return true;
				}
				const FName Category = Pin.Type.PinCategory;
				return Pin.IsInput() && !Pin.Type.IsContainer()
					&& (Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Interface);
			});
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, PassThroughOf))
		{
			Visibility = MakeVisibility([FindPin, ValueHandle]()
			{
				FCNASignaturePin Pin;
				FName Value;
				return !FindPin(Pin) || !Pin.IsInput() || !ReadName(ValueHandle, Value) || !Value.IsNone();
			});
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, bDynamicGroup))
		{
			Visibility = MakeVisibility([FindPin, IsOn, ValueHandle]()
			{
				FCNASignaturePin Pin;
				return !FindPin(Pin) || (Pin.IsInput() && Pin.Type.IsArray()) || IsOn(ValueHandle);
			});
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, DynamicMin)
			|| Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, DynamicMax)
			|| Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, DynamicNameFormat))
		{
			Visibility = MakeVisibility([DynamicHandle]()
			{
				bool bDynamic = false;
				return !ReadBool(DynamicHandle, bDynamic) || bDynamic;
			});
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, ConnectionRule))
		{
			Visibility = MakeVisibility([FindPin, ValueHandle]()
			{
				FCNASignaturePin Pin;
				uint8 Rule = 0;
				return !FindPin(Pin) || !Pin.IsExec() || !ReadByte(ValueHandle, Rule) || Rule != static_cast<uint8>(ECNAConnectionRule::None);
			});
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, ClassFilter))
		{
			Visibility = MakeVisibility([RuleHandle]()
			{
				uint8 Rule = 0;
				return !ReadByte(RuleHandle, Rule) || Rule == static_cast<uint8>(ECNAConnectionRule::ClassFilter);
			});
		}

		if (Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, PinName) || Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, PassThroughOf))
		{
			const bool bPassThrough = Name == GET_MEMBER_NAME_CHECKED(FCNAPinOverride, PassThroughOf);
			IDetailPropertyRow& Row = Children.AddProperty(ChildHandle);
			LocalizeRow(Row, Texts, Name);
			Row.Visibility(Visibility);
			Row.CustomWidget()
				.NameContent()
				[
					ChildHandle->CreatePropertyNameWidget(GetNameOverride(Texts, Name), GetToolTipOverride(Texts, Name))
				]
				.ValueContent()
				.MinDesiredWidth(220.0f)
				[
					MakePinNameValueWidget(ChildHandle, bPassThrough ? EPinFilter::Inputs : EPinFilter::Any, true)
				];
			continue;
		}
		AddChildRow(Children, ChildHandle, Texts, Visibility);
	}
}

// FCNAPinRedirectDetails

TSharedRef<IPropertyTypeCustomization> FCNAPinRedirectDetails::MakeInstance()
{
	return MakeShareable(new FCNAPinRedirectDetails);
}

void FCNAPinRedirectDetails::CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils)
{
	using namespace CNADetails;
	const TSharedPtr<IPropertyHandle> OldHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNAPinRedirect, OldName));
	const TSharedPtr<IPropertyHandle> NewHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNAPinRedirect, NewName));
	Header
		.NameContent()
		[
			MakeHeaderName([OldHandle, NewHandle]()
			{
				FName OldName;
				FName NewName;
				if (!ReadName(OldHandle, OldName) || !ReadName(NewHandle, NewName))
				{
					return LOCTEXT("Details.MultipleValues", "Multiple Values");
				}
				return FText::Format(LOCTEXT("Details.RedirectHeader", "{0} -> {1}"), FText::FromName(OldName), FText::FromName(NewName));
			})
		]
		.ValueContent()
		[
			Handle->CreatePropertyValueWidget()
		];
}

void FCNAPinRedirectDetails::CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils)
{
	using namespace CNADetails;
	FCNALocalization::FScopedDetailsLanguage Scope;
	FPropertyTextMap Texts;
	GetPinRedirectTexts(Texts);

	TArray<TSharedRef<IPropertyHandle>> ChildHandles;
	GetChildren(Handle, ChildHandles);
	for (const TSharedRef<IPropertyHandle>& ChildHandle : ChildHandles)
	{
		const FName Name = ChildHandle->GetProperty()->GetFName();
		if (Name == GET_MEMBER_NAME_CHECKED(FCNAPinRedirect, NewName))
		{
			IDetailPropertyRow& Row = Children.AddProperty(ChildHandle);
			LocalizeRow(Row, Texts, Name);
			Row.CustomWidget()
				.NameContent()
				[
					ChildHandle->CreatePropertyNameWidget(GetNameOverride(Texts, Name), GetToolTipOverride(Texts, Name))
				]
				.ValueContent()
				.MinDesiredWidth(220.0f)
				[
					MakePinNameValueWidget(ChildHandle, EPinFilter::Any, true, /*bResolveRedirects*/ true)
				];
			continue;
		}
		AddChildRow(Children, ChildHandle, Texts, EVisibility::Visible);
	}
}

// FCNABadgeDetails

TSharedRef<IPropertyTypeCustomization> FCNABadgeDetails::MakeInstance()
{
	return MakeShareable(new FCNABadgeDetails);
}

void FCNABadgeDetails::CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils)
{
	using namespace CNADetails;
	const TSharedPtr<IPropertyHandle> TextHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNABadge, Text));
	Header
		.NameContent()
		[
			MakeHeaderName([TextHandle]()
			{
				FText Text;
				if (!TextHandle.IsValid() || TextHandle->GetValue(Text) != FPropertyAccess::Success)
				{
					return LOCTEXT("Details.MultipleValues", "Multiple Values");
				}
				return Text.IsEmpty() ? LOCTEXT("Details.EmptyBadge", "(empty badge)") : Text;
			})
		]
		.ValueContent()
		[
			Handle->CreatePropertyValueWidget()
		];
}

void FCNABadgeDetails::CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils)
{
	using namespace CNADetails;
	FCNALocalization::FScopedDetailsLanguage Scope;
	FPropertyTextMap Texts;
	GetBadgeTexts(Texts);

	TArray<TSharedRef<IPropertyHandle>> ChildHandles;
	GetChildren(Handle, ChildHandles);
	for (const TSharedRef<IPropertyHandle>& ChildHandle : ChildHandles)
	{
		AddChildRow(Children, ChildHandle, Texts, EVisibility::Visible);
	}
}

// FCNABodyWidgetDetails

TSharedRef<IPropertyTypeCustomization> FCNABodyWidgetDetails::MakeInstance()
{
	return MakeShareable(new FCNABodyWidgetDetails);
}

void FCNABodyWidgetDetails::CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils)
{
	using namespace CNADetails;
	const TSharedPtr<IPropertyHandle> PinNameHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNABodyWidget, PinName));
	const TSharedPtr<IPropertyHandle> TypeHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Type));
	const TWeakObjectPtr<UEnum> TypeEnum = GetPropertyEnum(TypeHandle);
	Header
		.NameContent()
		[
			MakeHeaderName([PinNameHandle, TypeHandle, TypeEnum]()
			{
				FName Name;
				uint8 Type = 0;
				if (!ReadName(PinNameHandle, Name) || !ReadByte(TypeHandle, Type))
				{
					return LOCTEXT("Details.MultipleValues", "Multiple Values");
				}
				return FText::Format(LOCTEXT("Details.BodyWidgetHeader", "{0}: {1}"), GetEnumValueText(TypeEnum.Get(), Type),
					Name.IsNone() ? LOCTEXT("Details.NoPinName", "(no pin name)") : FText::FromName(Name));
			})
		]
		.ValueContent()
		[
			Handle->CreatePropertyValueWidget()
		];
}

void FCNABodyWidgetDetails::CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils)
{
	using namespace CNADetails;
	FCNALocalization::FScopedDetailsLanguage Scope;
	FPropertyTextMap Texts;
	GetBodyWidgetTexts(Texts);

	const TSharedPtr<IPropertyHandle> TypeHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Type));
	// Shown while the type is one of the given ones, or several widgets with different types are selected.
	auto ForTypes = [TypeHandle](TArray<ECNABodyWidgetType> Types)
	{
		return MakeVisibility([TypeHandle, Types]()
		{
			uint8 Type = 0;
			return !ReadByte(TypeHandle, Type) || Types.Contains(static_cast<ECNABodyWidgetType>(Type));
		});
	};
	auto IsCurve = [TypeHandle]()
	{
		uint8 Type = 0;
		return ReadByte(TypeHandle, Type) && static_cast<ECNABodyWidgetType>(Type) == ECNABodyWidgetType::Curve;
	};

	TArray<TSharedRef<IPropertyHandle>> ChildHandles;
	GetChildren(Handle, ChildHandles);
	for (const TSharedRef<IPropertyHandle>& ChildHandle : ChildHandles)
	{
		const FName Name = ChildHandle->GetProperty()->GetFName();
		TAttribute<EVisibility> Visibility = EVisibility::Visible;
		if (Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Options))
		{
			Visibility = ForTypes({ ECNABodyWidgetType::ComboBox, ECNABodyWidgetType::ScrollList });
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Height))
		{
			Visibility = ForTypes({ ECNABodyWidgetType::ScrollList, ECNABodyWidgetType::Curve });
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, SliderMin) || Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, SliderMax))
		{
			Visibility = ForTypes({ ECNABodyWidgetType::Slider });
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Curve) || Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, WidgetGuid))
		{
			Visibility = ForTypes({ ECNABodyWidgetType::Curve });
		}

		if (Name == GET_MEMBER_NAME_CHECKED(FCNABodyWidget, PinName))
		{
			// For a Curve the name is only the name of the curve: no pin list, no CNA014 warning.
			const FText CurveName = LOCTEXT("Prop.BodyWidget.CurveName", "Curve Name");
			const FText CurveTip = LOCTEXT("Prop.BodyWidget.CurveName.Tip", "Name of the curve; the Curve Value node of the macro Execute finds the curve by its Guid and by this name.");
			const bool bRussian = FCNALocalization::IsDetailsRussian();
			const FPropertyText* PinTexts = Texts.Find(Name);
			const FText PinName = (bRussian && PinTexts) ? PinTexts->Name : ChildHandle->GetPropertyDisplayName();
			const FText PinTip = (bRussian && PinTexts) ? PinTexts->ToolTip : ChildHandle->GetProperty()->GetToolTipText();

			IDetailPropertyRow& Row = Children.AddProperty(ChildHandle);
			LocalizeRow(Row, Texts, Name);
			Row.CustomWidget()
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([IsCurve, CurveName, PinName]() { return IsCurve() ? CurveName : PinName; })
					.ToolTipText_Lambda([IsCurve, CurveTip, PinTip]() { return IsCurve() ? CurveTip : PinTip; })
				]
				.ValueContent()
				.MinDesiredWidth(220.0f)
				[
					MakePinNameValueWidget(ChildHandle, EPinFilter::Inputs, TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([IsCurve]() { return !IsCurve(); })))
				];
			continue;
		}
		AddChildRow(Children, ChildHandle, Texts, Visibility);
	}
}

// FCNACurveSettingsDetails

TSharedRef<IPropertyTypeCustomization> FCNACurveSettingsDetails::MakeInstance()
{
	return MakeShareable(new FCNACurveSettingsDetails);
}

void FCNACurveSettingsDetails::CustomizeHeader(TSharedRef<IPropertyHandle> Handle, FDetailWidgetRow& Header, IPropertyTypeCustomizationUtils& Utils)
{
	using namespace CNADetails;
	FPropertyTextMap BodyTexts;
	{
		FCNALocalization::FScopedDetailsLanguage Scope;
		GetBodyWidgetTexts(BodyTexts);
	}
	const FName CurveMember = GET_MEMBER_NAME_CHECKED(FCNABodyWidget, Curve);
	const TSharedPtr<IPropertyHandle> ValueTypeHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNACurveSettings, ValueType));
	const TSharedPtr<IPropertyHandle> ModeHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNACurveSettings, Mode));
	const TSharedPtr<IPropertyHandle> UseAssetHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bUseCurveAsset));
	const TWeakObjectPtr<UEnum> ValueTypeEnum = GetPropertyEnum(ValueTypeHandle);
	const TWeakObjectPtr<UEnum> ModeEnum = GetPropertyEnum(ModeHandle);

	Header
		.NameContent()
		[
			Handle->CreatePropertyNameWidget(GetNameOverride(BodyTexts, CurveMember), GetToolTipOverride(BodyTexts, CurveMember))
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda([ValueTypeHandle, ModeHandle, UseAssetHandle, ValueTypeEnum, ModeEnum]()
			{
				FCNALocalization::FScopedDetailsLanguage Scope;
				uint8 ValueType = 0;
				uint8 Mode = 0;
				bool bUseAsset = false;
				if (!ReadByte(ValueTypeHandle, ValueType) || !ReadByte(ModeHandle, Mode) || !ReadBool(UseAssetHandle, bUseAsset))
				{
					return LOCTEXT("Details.MultipleValues", "Multiple Values");
				}
				return FText::Format(LOCTEXT("Details.CurveSummary", "{0}, {1}, {2}"),
					GetEnumValueText(ValueTypeEnum.Get(), ValueType),
					GetEnumValueText(ModeEnum.Get(), Mode),
					bUseAsset ? LOCTEXT("Details.CurveSourceAsset", "Curve asset") : LOCTEXT("Details.CurveSourceOwn", "own curve"));
			})
		];
}

void FCNACurveSettingsDetails::CustomizeChildren(TSharedRef<IPropertyHandle> Handle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils)
{
	using namespace CNADetails;
	FCNALocalization::FScopedDetailsLanguage Scope;
	FPropertyTextMap Texts;
	GetCurveSettingsTexts(Texts);

	const TSharedPtr<IPropertyHandle> ValueTypeHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNACurveSettings, ValueType));
	const TSharedPtr<IPropertyHandle> ModeHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNACurveSettings, Mode));
	const TSharedPtr<IPropertyHandle> UseAssetHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bUseCurveAsset));
	const TSharedPtr<IPropertyHandle> PlayFromStartHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bShowPlayFromStart));
	const TSharedPtr<IPropertyHandle> ReverseHandle = Child(Handle, GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bShowReverse));

	// The channel names depend on the value type: rebuild the rows when it changes.
	RequestRefreshOnChange(ValueTypeHandle, Utils);

	uint8 RawValueType = 0;
	const bool bKnownValueType = ReadByte(ValueTypeHandle, RawValueType);
	const ECNACurveValueType ValueType = static_cast<ECNACurveValueType>(RawValueType);

	// Unknown (several values) counts as "yes": nothing is hidden for a mixed selection.
	auto UsesAsset = [UseAssetHandle](bool bExpected)
	{
		bool bValue = false;
		return !ReadBool(UseAssetHandle, bValue) || bValue == bExpected;
	};
	auto IsMode = [ModeHandle](ECNACurveMode Expected)
	{
		uint8 Mode = 0;
		return !ReadByte(ModeHandle, Mode) || static_cast<ECNACurveMode>(Mode) == Expected;
	};
	auto IsOn = [](const TSharedPtr<IPropertyHandle>& Toggle)
	{
		bool bValue = false;
		return !ReadBool(Toggle, bValue) || bValue;
	};
	auto UsesChannel = [ValueTypeHandle](int32 Channel)
	{
		uint8 Type = 0;
		if (!ReadByte(ValueTypeHandle, Type))
		{
			return true;
		}
		switch (static_cast<ECNACurveValueType>(Type))
		{
		case ECNACurveValueType::Vector:      return Channel < 3;
		case ECNACurveValueType::LinearColor: return Channel < 4;
		case ECNACurveValueType::Float:
		default:                              return Channel < 1;
		}
	};

	TArray<TSharedRef<IPropertyHandle>> ChildHandles;
	GetChildren(Handle, ChildHandles);
	for (const TSharedRef<IPropertyHandle>& ChildHandle : ChildHandles)
	{
		const FName Name = ChildHandle->GetProperty()->GetFName();
		TAttribute<EVisibility> Visibility = EVisibility::Visible;

		int32 Channel = INDEX_NONE;
		if (Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, CurveX)) { Channel = 0; }
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, CurveY)) { Channel = 1; }
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, CurveZ)) { Channel = 2; }
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, CurveW)) { Channel = 3; }

		if (Channel != INDEX_NONE)
		{
			Visibility = MakeVisibility([UsesAsset, UsesChannel, Channel]() { return UsesAsset(false) && UsesChannel(Channel); });
			// The row keeps the engine customization of FRuntimeFloatCurve (curve editor in the Details).
			IDetailPropertyRow& Row = Children.AddProperty(ChildHandle);
			Row.Visibility(Visibility);
			if (bKnownValueType)
			{
				const FPropertyText* ChannelTexts = Texts.Find(Name);
				Row.DisplayName(GetChannelName(ValueType, Channel));
				if (ChannelTexts && FCNALocalization::IsDetailsRussian())
				{
					Row.ToolTip(ChannelTexts->ToolTip);
				}
			}
			else
			{
				LocalizeRow(Row, Texts, Name);
			}
			continue;
		}

		if (Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, CurveAsset))
		{
			Visibility = MakeVisibility([UsesAsset]() { return UsesAsset(true); });
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, BakeSamples))
		{
			Visibility = MakeVisibility([UsesAsset]() { return UsesAsset(false); });
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, TimeLabel))
		{
			Visibility = MakeVisibility([IsMode]() { return IsMode(ECNACurveMode::EvaluateAtTime); });
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, PlayFromStartLabel))
		{
			Visibility = MakeVisibility([IsMode, IsOn, PlayFromStartHandle]() { return IsMode(ECNACurveMode::Playback) && IsOn(PlayFromStartHandle); });
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, ReverseLabel))
		{
			Visibility = MakeVisibility([IsMode, IsOn, ReverseHandle]() { return IsMode(ECNACurveMode::Playback) && IsOn(ReverseHandle); });
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, Length)
			|| Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bLoop)
			|| Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, PlayRate)
			|| Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bShowPlayFromStart)
			|| Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, bShowReverse)
			|| Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, PlayLabel)
			|| Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, StopLabel)
			|| Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, UpdateLabel)
			|| Name == GET_MEMBER_NAME_CHECKED(FCNACurveSettings, FinishedLabel))
		{
			Visibility = MakeVisibility([IsMode]() { return IsMode(ECNACurveMode::Playback); });
		}
		AddChildRow(Children, ChildHandle, Texts, Visibility);
	}
}

#undef LOCTEXT_NAMESPACE
