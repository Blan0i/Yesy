// Copyright (c) NodeFactory. Curve graph of a Curve body widget (Timeline-like track on the node body).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Curves/CurveBase.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

class UCustomNodeAsset;
struct FCNACurveSettings;
struct FRichCurve;

/**
 * Draws the curve of a Curve body widget: background, optional grid, one line per channel (R, G, B, A colors for
 * Vector and LinearColor, a light line for Float) and the keys. The settings are read from the asset on every paint,
 * so edits show without a rebuild; the Curve asset is loaded once in Construct (never while painting).
 */
class NODEFACTORYEDITOR_API SCNACurveGraph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCNACurveGraph)
		: _Asset(nullptr)
		, _GraphSize(FVector2D(160.0f, 80.0f))
	{}
		/** Asset that owns the body widget. */
		SLATE_ARGUMENT(const UCustomNodeAsset*, Asset)
		/** WidgetGuid of the body widget. */
		SLATE_ARGUMENT(FGuid, WidgetGuid)
		/** PinName of the body widget, used when the Guid does not match (older data). */
		SLATE_ARGUMENT(FName, WidgetName)
		SLATE_ARGUMENT(FVector2D, GraphSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	struct FChannel
	{
		const FRichCurve* Curve = nullptr;
		FLinearColor Color = FLinearColor::White;
	};

	/** Settings of the widget in the asset, nullptr when the asset or the widget is gone. */
	const FCNACurveSettings* FindSettings() const;

	/** Channels to draw: the own curve (or its ExternalCurve) or the loaded Curve asset. */
	void GatherChannels(const FCNACurveSettings& Settings, TArray<FChannel>& OutChannels) const;

	TWeakObjectPtr<UCustomNodeAsset> Asset;
	FGuid WidgetGuid;
	FName WidgetName;
	FVector2D GraphSize;

	/** The Curve asset loaded in Construct; held while the node widget lives, like the icon textures. */
	TStrongObjectPtr<UCurveBase> LoadedCurve;
	FSoftObjectPath LoadedCurvePath;
};
