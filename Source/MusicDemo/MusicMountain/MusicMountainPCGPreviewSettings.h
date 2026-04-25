// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "MusicMountainPCGPreviewSettings.generated.h"

class FMusicMountainPCGPreviewElement;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class MUSICDEMO_API UPCGMusicMountainPreviewSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("MusicMountainPreviewDecorator"); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("MusicMountainPCG", "PreviewDecoratorTitle", "Music Mountain Preview Decorator"); }
	virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("MusicMountainPCG", "PreviewDecoratorTooltip", "Generates music-aware preview ecology from a Music Mountain PCG section actor."); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spawner; }
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ClampMin = "0.2", ClampMax = "3.0", UIMin = "0.2", UIMax = "3.0"))
	float DensityMultiplier = 1.0f;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return DefaultPointInputPinProperties(); }
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return DefaultPointOutputPinProperties(); }
	virtual FPCGElementPtr CreateElement() const override;
};

class FMusicMountainPCGPreviewElement : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
