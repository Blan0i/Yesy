// Copyright (c) NodeFactory. Curve graph of a Curve body widget (Timeline-like track on the node body).

#include "SCNACurveGraph.h"
#include "CNATypes.h"
#include "CustomNodeAsset.h"

#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "Curves/RichCurve.h"
#include "EditorStyleSet.h"
#include "Fonts/SlateFontInfo.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "CNALocalization.h"

#define LOCTEXT_NAMESPACE "NodeFactory"

namespace CNACurveGraphLocal
{
	/** Inner margin between the frame and the curve area. */
	const float Margin = 4.0f;

	/** Float curves are drawn in a light color, the channels of Vector and LinearColor in R, G, B, A. */
	const FLinearColor FloatColor(0.85f, 0.85f, 0.85f, 1.0f);
	const FLinearColor ChannelColors[4] =
	{
		FLinearColor(1.0f, 0.25f, 0.25f, 1.0f),
		FLinearColor(0.25f, 1.0f, 0.25f, 1.0f),
		FLinearColor(0.3f, 0.5f, 1.0f, 1.0f),
		FLinearColor(0.8f, 0.8f, 0.8f, 1.0f)
	};

	const FLinearColor FrameColor(0.3f, 0.3f, 0.3f, 1.0f);
	const FLinearColor GridColor(1.0f, 1.0f, 1.0f, 0.07f);
	const FLinearColor ZeroLineColor(1.0f, 1.0f, 1.0f, 0.18f);

	FLinearColor Tint(const FLinearColor& Color, const FWidgetStyle& Style)
	{
		return Color * Style.GetColorAndOpacityTint();
	}
}

void SCNACurveGraph::Construct(const FArguments& InArgs)
{
	Asset = const_cast<UCustomNodeAsset*>(InArgs._Asset);
	WidgetGuid = InArgs._WidgetGuid;
	WidgetName = InArgs._WidgetName;
	GraphSize = FVector2D(FMath::Max(InArgs._GraphSize.X, 24.0f), FMath::Max(InArgs._GraphSize.Y, 16.0f));

	// The Curve asset is loaded here once; painting never loads. A changed asset rebuilds the node widget
	// (OnAssetChanged -> UpdateGraphNode), which constructs this widget again.
	const FCNACurveSettings* Settings = FindSettings();
	if (Settings && Settings->bUseCurveAsset && !Settings->CurveAsset.IsNull())
	{
		UCurveBase* Curve = Settings->CurveAsset.Get();
		if (!Curve)
		{
			Curve = Settings->CurveAsset.LoadSynchronous();
		}
		if (Curve)
		{
			LoadedCurve.Reset(Curve);
			LoadedCurvePath = Settings->CurveAsset.ToSoftObjectPath();
		}
	}
}

FVector2D SCNACurveGraph::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return GraphSize;
}

const FCNACurveSettings* SCNACurveGraph::FindSettings() const
{
	const UCustomNodeAsset* CurrentAsset = Asset.Get();
	if (!CurrentAsset)
	{
		return nullptr;
	}
	const FCNABodyWidget* Widget = CurrentAsset->FindCurveWidget(WidgetGuid, WidgetName);
	return (Widget && Widget->IsCurve()) ? &Widget->Curve : nullptr;
}

void SCNACurveGraph::GatherChannels(const FCNACurveSettings& Settings, TArray<FChannel>& OutChannels) const
{
	using namespace CNACurveGraphLocal;

	auto AddChannel = [&OutChannels](const FRichCurve* Curve, const FLinearColor& Color)
	{
		FChannel& Channel = OutChannels[OutChannels.AddDefaulted()];
		Channel.Curve = Curve;
		Channel.Color = Color;
	};

	if (Settings.bUseCurveAsset)
	{
		// Only the asset loaded in Construct, or one that is already in memory: no loading while painting.
		UCurveBase* Curve = nullptr;
		if (LoadedCurve.IsValid() && LoadedCurvePath == Settings.CurveAsset.ToSoftObjectPath())
		{
			Curve = LoadedCurve.Get();
		}
		else
		{
			Curve = Settings.CurveAsset.Get();
		}

		if (const UCurveFloat* FloatCurve = Cast<UCurveFloat>(Curve))
		{
			AddChannel(&FloatCurve->FloatCurve, FloatColor);
		}
		else if (const UCurveVector* VectorCurve = Cast<UCurveVector>(Curve))
		{
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AddChannel(&VectorCurve->FloatCurves[Index], ChannelColors[Index]);
			}
		}
		else if (const UCurveLinearColor* ColorCurve = Cast<UCurveLinearColor>(Curve))
		{
			for (int32 Index = 0; Index < 4; ++Index)
			{
				AddChannel(&ColorCurve->FloatCurves[Index], ChannelColors[Index]);
			}
		}
		return;
	}

	const int32 NumChannels = Settings.GetChannelCount();
	for (int32 Index = 0; Index < NumChannels; ++Index)
	{
		const FRuntimeFloatCurve* Runtime = Settings.GetChannel(Index);
		if (!Runtime)
		{
			continue;
		}
		// FRuntimeFloatCurve::ExternalCurve replaces the inline keys when it is set (as GetRichCurveConst does).
		const FRichCurve* Curve = Runtime->ExternalCurve ? &Runtime->ExternalCurve->FloatCurve : &Runtime->EditorCurveData;
		AddChannel(Curve, NumChannels == 1 ? FloatColor : ChannelColors[Index]);
	}
}

int32 SCNACurveGraph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace CNACurveGraphLocal;

	const ESlateDrawEffect DrawEffect = (bParentEnabled && IsEnabled()) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FSlateBrush* WhiteBrush = FEditorStyle::GetBrush(TEXT("WhiteBrush"));
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FCNACurveSettings* Settings = FindSettings();

	// Background and frame.
	const FLinearColor Background = Settings ? Settings->GraphBackgroundColor : FLinearColor(0.02f, 0.02f, 0.02f, 1.0f);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WhiteBrush, DrawEffect, Tint(Background, InWidgetStyle));
	{
		TArray<FVector2D> Frame;
		Frame.Add(FVector2D(0.0f, 0.0f));
		Frame.Add(FVector2D(Size.X, 0.0f));
		Frame.Add(FVector2D(Size.X, Size.Y));
		Frame.Add(FVector2D(0.0f, Size.Y));
		Frame.Add(FVector2D(0.0f, 0.0f));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Frame, DrawEffect, Tint(FrameColor, InWidgetStyle), false, 1.0f);
	}

	const FVector2D AreaMin(Margin, Margin);
	const FVector2D AreaSize(FMath::Max(Size.X - 2.0f * Margin, 1.0f), FMath::Max(Size.Y - 2.0f * Margin, 1.0f));

	if (Settings && Settings->bShowGrid)
	{
		const int32 Columns = 4;
		const int32 RowsCount = 4;
		for (int32 Index = 1; Index < Columns; ++Index)
		{
			const float X = AreaMin.X + AreaSize.X * Index / Columns;
			TArray<FVector2D> Line;
			Line.Add(FVector2D(X, AreaMin.Y));
			Line.Add(FVector2D(X, AreaMin.Y + AreaSize.Y));
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Line, DrawEffect, Tint(GridColor, InWidgetStyle), false, 1.0f);
		}
		for (int32 Index = 1; Index < RowsCount; ++Index)
		{
			const float Y = AreaMin.Y + AreaSize.Y * Index / RowsCount;
			TArray<FVector2D> Line;
			Line.Add(FVector2D(AreaMin.X, Y));
			Line.Add(FVector2D(AreaMin.X + AreaSize.X, Y));
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Line, DrawEffect, Tint(GridColor, InWidgetStyle), false, 1.0f);
		}
	}

	TArray<FChannel> Channels;
	if (Settings)
	{
		GatherChannels(*Settings, Channels);
	}

	// Time range over the keys of all channels; value range from samples (tangents may overshoot the keys).
	bool bHasKeys = false;
	float TimeMin = 0.0f;
	float TimeMax = 1.0f;
	for (const FChannel& Channel : Channels)
	{
		if (!Channel.Curve || Channel.Curve->GetNumKeys() == 0)
		{
			continue;
		}
		float Min = 0.0f;
		float Max = 0.0f;
		Channel.Curve->GetTimeRange(Min, Max);
		TimeMin = bHasKeys ? FMath::Min(TimeMin, Min) : Min;
		TimeMax = bHasKeys ? FMath::Max(TimeMax, Max) : Max;
		bHasKeys = true;
	}

	if (!bHasKeys)
	{
		const FText Message = (Settings && Settings->bUseCurveAsset)
			? (Settings->CurveAsset.IsNull() ? LOCTEXT("CurveGraph.NoAsset", "No curve asset") : LOCTEXT("CurveGraph.AssetNotLoaded", "Curve asset not loaded or empty"))
			: LOCTEXT("CurveGraph.NoKeys", "No keys");
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(AreaMin, AreaSize), Message,
			FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8), DrawEffect, Tint(FLinearColor(1.0f, 1.0f, 1.0f, 0.4f), InWidgetStyle));
		return LayerId + 2;
	}

	if (TimeMax - TimeMin < KINDA_SMALL_NUMBER)
	{
		TimeMax = TimeMin + 1.0f;
	}

	const int32 NumSamples = FMath::Clamp(FMath::RoundToInt(AreaSize.X / 2.0f), 8, 256);
	float ValueMin = 0.0f;
	float ValueMax = 0.0f;
	bool bHasValue = false;
	for (const FChannel& Channel : Channels)
	{
		if (!Channel.Curve || Channel.Curve->GetNumKeys() == 0)
		{
			continue;
		}
		for (int32 Sample = 0; Sample <= NumSamples; ++Sample)
		{
			const float Value = Channel.Curve->Eval(FMath::Lerp(TimeMin, TimeMax, static_cast<float>(Sample) / NumSamples));
			ValueMin = bHasValue ? FMath::Min(ValueMin, Value) : Value;
			ValueMax = bHasValue ? FMath::Max(ValueMax, Value) : Value;
			bHasValue = true;
		}
	}
	if (ValueMax - ValueMin < KINDA_SMALL_NUMBER)
	{
		ValueMin -= 0.5f;
		ValueMax += 0.5f;
	}
	const float ValuePadding = (ValueMax - ValueMin) * 0.08f;
	ValueMin -= ValuePadding;
	ValueMax += ValuePadding;

	auto ToLocal = [&](float Time, float Value) -> FVector2D
	{
		const float U = (Time - TimeMin) / (TimeMax - TimeMin);
		const float V = (Value - ValueMin) / (ValueMax - ValueMin);
		return FVector2D(AreaMin.X + U * AreaSize.X, AreaMin.Y + (1.0f - V) * AreaSize.Y);
	};

	if (Settings->bShowGrid && ValueMin < 0.0f && ValueMax > 0.0f)
	{
		TArray<FVector2D> ZeroLine;
		ZeroLine.Add(ToLocal(TimeMin, 0.0f));
		ZeroLine.Add(ToLocal(TimeMax, 0.0f));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), ZeroLine, DrawEffect, Tint(ZeroLineColor, InWidgetStyle), false, 1.0f);
	}

	for (const FChannel& Channel : Channels)
	{
		if (!Channel.Curve || Channel.Curve->GetNumKeys() == 0)
		{
			continue;
		}
		TArray<FVector2D> Points;
		Points.Reserve(NumSamples + 1);
		for (int32 Sample = 0; Sample <= NumSamples; ++Sample)
		{
			const float Time = FMath::Lerp(TimeMin, TimeMax, static_cast<float>(Sample) / NumSamples);
			Points.Add(ToLocal(Time, Channel.Curve->Eval(Time)));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points, DrawEffect, Tint(Channel.Color, InWidgetStyle), true, 1.5f);
	}

	if (Settings->bShowKeys)
	{
		const FVector2D KeySize(5.0f, 5.0f);
		for (const FChannel& Channel : Channels)
		{
			if (!Channel.Curve)
			{
				continue;
			}
			for (auto It = Channel.Curve->GetKeyIterator(); It; ++It)
			{
				const FVector2D Center = ToLocal(It->Time, It->Value);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(Center - KeySize * 0.5f, KeySize), WhiteBrush, DrawEffect, Tint(Channel.Color, InWidgetStyle));
			}
		}
	}
	return LayerId + 3;
}

#undef LOCTEXT_NAMESPACE
