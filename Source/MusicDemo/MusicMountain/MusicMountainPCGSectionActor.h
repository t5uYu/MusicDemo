// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MusicMountainPCGSectionActor.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UPCGComponent;
class UPCGGraphInterface;
class USplineComponent;
class UStaticMesh;

UCLASS(BlueprintType)
class MUSICDEMO_API AMusicMountainPCGSectionActor : public AActor
{
	GENERATED_BODY()

public:
	AMusicMountainPCGSectionActor();

	void ConfigureSection(
		int32 InSectionIndex,
		const FString& InSectionName,
		const FString& InMood,
		const FString& InTerrain,
		const FString& InAudioStyle,
		float InEnergy,
		float InRouteStartDistance,
		float InRouteEndDistance,
		float InRouteRadius,
		const FLinearColor& InThemeColor,
		const FBox& InSectionBounds,
		const TArray<FVector>& InRouteLocations,
		const TArray<FVector>& InInnerScatterLocations,
		const TArray<FVector>& InOuterScatterLocations,
		float InRoadExclusionHalfWidth,
		float InScatterWidth,
		UPCGGraphInterface* InPCGGraph,
		int32 InSeed);

	void GeneratePreviewDecorationsFromPCG(float DensityMultiplier);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	TObjectPtr<UBoxComponent> BoundsComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	TObjectPtr<USplineComponent> RouteSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	TObjectPtr<USplineComponent> InnerScatterSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	TObjectPtr<USplineComponent> OuterScatterSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	TObjectPtr<UPCGComponent> PCGComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	int32 SectionIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	FString SectionName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	FString Mood;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	FString Terrain;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	FString AudioStyle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	float Energy = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	float RouteStartDistance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	float RouteEndDistance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	float RouteRadius = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	FLinearColor ThemeColor = FLinearColor::White;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	float RoadExclusionHalfWidth = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	float ScatterWidth = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	float FoliageDensity = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	float RockDensity = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Mountain|PCG")
	float LightDensity = 0.0f;

private:
	static void RebuildSpline(USplineComponent* Spline, const TArray<FVector>& WorldLocations);
	void ResolveDensityHints();
	void ClearPreviewDecorations();
	void SpawnPreviewDecoration(const FString& Label, UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, const FLinearColor& Color, const FRotator& Rotation = FRotator::ZeroRotator);

	UPROPERTY()
	TArray<TObjectPtr<AActor>> GeneratedPreviewActors;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseShapeMaterial;
};
