// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicMountainPCGSectionActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PCGComponent.h"
#include "PCGGraph.h"

AMusicMountainPCGSectionActor::AMusicMountainPCGSectionActor()
{
	PrimaryActorTick.bCanEverTick = false;

	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("MusicMountainPCGBounds"));
	SetRootComponent(BoundsComponent);
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsComponent->SetHiddenInGame(true);

	RouteSpline = CreateDefaultSubobject<USplineComponent>(TEXT("MusicMountainRouteSpline"));
	RouteSpline->SetupAttachment(BoundsComponent);
	RouteSpline->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	InnerScatterSpline = CreateDefaultSubobject<USplineComponent>(TEXT("MusicMountainInnerScatterSpline"));
	InnerScatterSpline->SetupAttachment(BoundsComponent);
	InnerScatterSpline->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	OuterScatterSpline = CreateDefaultSubobject<USplineComponent>(TEXT("MusicMountainOuterScatterSpline"));
	OuterScatterSpline->SetupAttachment(BoundsComponent);
	OuterScatterSpline->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PCGComponent = CreateDefaultSubobject<UPCGComponent>(TEXT("MusicMountainPCGComponent"));

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BaseShapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

void AMusicMountainPCGSectionActor::ConfigureSection(
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
	int32 InSeed)
{
	SectionIndex = InSectionIndex;
	SectionName = InSectionName;
	Mood = InMood;
	Terrain = InTerrain;
	AudioStyle = InAudioStyle;
	Energy = InEnergy;
	RouteStartDistance = InRouteStartDistance;
	RouteEndDistance = InRouteEndDistance;
	RouteRadius = InRouteRadius;
	ThemeColor = InThemeColor;
	RoadExclusionHalfWidth = InRoadExclusionHalfWidth;
	ScatterWidth = InScatterWidth;
	ResolveDensityHints();

#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("PCG Section %02d - %s"), SectionIndex + 1, *SectionName));
#endif

	Tags.Reset();
	Tags.AddUnique(TEXT("MusicMountain"));
	Tags.AddUnique(TEXT("MusicMountainPCG"));
	Tags.AddUnique(FName(*FString::Printf(TEXT("Section_%02d"), SectionIndex + 1)));
	Tags.AddUnique(FName(*Mood));
	Tags.AddUnique(FName(*Terrain));

	if (BoundsComponent && InSectionBounds.IsValid)
	{
		SetActorLocation(InSectionBounds.GetCenter());
		BoundsComponent->SetBoxExtent(InSectionBounds.GetExtent());
	}

	RebuildSpline(RouteSpline, InRouteLocations);
	RebuildSpline(InnerScatterSpline, InInnerScatterLocations);
	RebuildSpline(OuterScatterSpline, InOuterScatterLocations);

	if (PCGComponent && InPCGGraph)
	{
		PCGComponent->Seed = InSeed;
		PCGComponent->SetGraph(InPCGGraph);
		PCGComponent->Generate(true);
	}
}

void AMusicMountainPCGSectionActor::RebuildSpline(USplineComponent* Spline, const TArray<FVector>& WorldLocations)
{
	if (!Spline)
	{
		return;
	}

	Spline->ClearSplinePoints(false);
	for (const FVector& WorldLocation : WorldLocations)
	{
		Spline->AddSplinePoint(WorldLocation, ESplineCoordinateSpace::World, false);
	}
	Spline->SetClosedLoop(false, false);
	Spline->UpdateSpline();
}

void AMusicMountainPCGSectionActor::ResolveDensityHints()
{
	const FString MoodLower = Mood.ToLower();
	const FString TerrainLower = Terrain.ToLower();

	FoliageDensity = FMath::Lerp(0.25f, 0.85f, 1.0f - Energy);
	RockDensity = FMath::Lerp(0.35f, 1.0f, Energy);
	LightDensity = FMath::Lerp(0.18f, 0.65f, Energy);

	if (MoodLower.Contains(TEXT("romantic")) || MoodLower.Contains(TEXT("sweet")) || TerrainLower.Contains(TEXT("flower")) || TerrainLower.Contains(TEXT("meadow")))
	{
		FoliageDensity += 0.45f;
		LightDensity += 0.15f;
		RockDensity -= 0.12f;
	}

	if (MoodLower.Contains(TEXT("dark")) || MoodLower.Contains(TEXT("tense")) || TerrainLower.Contains(TEXT("cliff")) || TerrainLower.Contains(TEXT("cave")))
	{
		RockDensity += 0.35f;
		LightDensity -= 0.08f;
	}

	if (MoodLower.Contains(TEXT("epic")) || TerrainLower.Contains(TEXT("summit")) || TerrainLower.Contains(TEXT("ridge")))
	{
		RockDensity += 0.2f;
		LightDensity += 0.25f;
	}

	FoliageDensity = FMath::Clamp(FoliageDensity, 0.0f, 1.25f);
	RockDensity = FMath::Clamp(RockDensity, 0.0f, 1.25f);
	LightDensity = FMath::Clamp(LightDensity, 0.0f, 1.0f);
}

void AMusicMountainPCGSectionActor::GeneratePreviewDecorationsFromPCG(float DensityMultiplier)
{
	ClearPreviewDecorations();

	if (!GetWorld() || !RouteSpline || !InnerScatterSpline || !OuterScatterSpline || !SphereMesh || !CubeMesh)
	{
		return;
	}

	const int32 PointCount = RouteSpline->GetNumberOfSplinePoints();
	if (PointCount == 0)
	{
		return;
	}

	const FString MoodLower = Mood.ToLower();
	const FString TerrainLower = Terrain.ToLower();
	const float DensityScale = FMath::Clamp(DensityMultiplier, 0.2f, 3.0f);
	const int32 Stride = FMath::Max(2, FMath::RoundToInt(FMath::Lerp(7.0f, 3.0f, Energy) / DensityScale));
	const FLinearColor FoliageColor = (MoodLower.Contains(TEXT("romantic")) || MoodLower.Contains(TEXT("sweet")))
		? FLinearColor(0.95f, 0.35f, 0.62f)
		: FLinearColor(0.18f, 0.46f, 0.22f);
	const FLinearColor RockColor = FLinearColor::LerpUsingHSV(ThemeColor, FLinearColor(0.28f, 0.27f, 0.24f), 0.65f);
	const FLinearColor LightColor = FLinearColor::LerpUsingHSV(ThemeColor, FLinearColor(1.0f, 0.86f, 0.38f), 0.45f);
	FRandomStream PreviewRandom(SectionIndex * 1709 + FMath::RoundToInt(Energy * 1000.0f) + 401);

	for (int32 PointIndex = 0; PointIndex < PointCount; PointIndex += Stride)
	{
		const FVector RouteLocation = RouteSpline->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
		const bool bUseOuterSide = PreviewRandom.FRand() > 0.35f;
		USplineComponent* ScatterSpline = bUseOuterSide ? OuterScatterSpline : InnerScatterSpline;
		const int32 ScatterPointIndex = FMath::Clamp(PointIndex, 0, ScatterSpline->GetNumberOfSplinePoints() - 1);
		const FVector ScatterLocation = ScatterSpline->GetLocationAtSplinePoint(ScatterPointIndex, ESplineCoordinateSpace::World);
		const FVector SideDirection = (ScatterLocation - RouteLocation).GetSafeNormal();
		const FVector Tangent = RouteSpline->GetTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::World).GetSafeNormal();
		const FVector BaseLocation = ScatterLocation
			+ SideDirection * PreviewRandom.FRandRange(0.0f, ScatterWidth * 0.45f)
			+ Tangent * PreviewRandom.FRandRange(-90.0f, 90.0f);

		if (PreviewRandom.FRand() < FoliageDensity)
		{
			const bool bFlowerPatch = MoodLower.Contains(TEXT("romantic")) || MoodLower.Contains(TEXT("sweet")) || TerrainLower.Contains(TEXT("flower")) || TerrainLower.Contains(TEXT("meadow"));
			if (bFlowerPatch)
			{
				const int32 FlowerCount = PreviewRandom.RandRange(3, 6);
				for (int32 FlowerIndex = 0; FlowerIndex < FlowerCount; ++FlowerIndex)
				{
					SpawnPreviewDecoration(
						FString::Printf(TEXT("%s PCG Flower %d-%d"), *SectionName, PointIndex, FlowerIndex),
						SphereMesh,
						BaseLocation + FVector(PreviewRandom.FRandRange(-90.0f, 90.0f), PreviewRandom.FRandRange(-90.0f, 90.0f), 34.0f),
						FVector(0.13f, 0.13f, 0.08f),
						FoliageColor);
				}
			}
			else
			{
				SpawnPreviewDecoration(
					FString::Printf(TEXT("%s PCG Tree Trunk %d"), *SectionName, PointIndex),
					CubeMesh,
					BaseLocation + FVector(0.0f, 0.0f, 80.0f),
					FVector(0.16f, 0.16f, 1.25f),
					FLinearColor(0.23f, 0.13f, 0.07f),
					FRotator(0.0f, PreviewRandom.FRandRange(-35.0f, 35.0f), 0.0f));
				SpawnPreviewDecoration(
					FString::Printf(TEXT("%s PCG Tree Crown %d"), *SectionName, PointIndex),
					SphereMesh,
					BaseLocation + FVector(0.0f, 0.0f, 190.0f),
					FVector(0.75f, 0.75f, 0.62f) * PreviewRandom.FRandRange(0.75f, 1.25f),
					FoliageColor);
			}
		}

		if (PreviewRandom.FRand() < RockDensity)
		{
			SpawnPreviewDecoration(
				FString::Printf(TEXT("%s PCG Rock %d"), *SectionName, PointIndex),
				SphereMesh,
				BaseLocation + SideDirection * PreviewRandom.FRandRange(90.0f, 220.0f) + FVector(0.0f, 0.0f, 60.0f),
				FVector(0.45f, 0.62f, 0.38f) * PreviewRandom.FRandRange(0.75f, 1.45f),
				RockColor);
		}

		if (PreviewRandom.FRand() < LightDensity)
		{
			const FVector LightBaseLocation = RouteLocation + SideDirection * (RoadExclusionHalfWidth + 85.0f) + FVector(0.0f, 0.0f, 80.0f);
			SpawnPreviewDecoration(
				FString::Printf(TEXT("%s PCG Light Pole %d"), *SectionName, PointIndex),
				CubeMesh,
				LightBaseLocation,
				FVector(0.1f, 0.1f, 1.2f),
				FLinearColor(0.08f, 0.07f, 0.1f));
			SpawnPreviewDecoration(
				FString::Printf(TEXT("%s PCG Light Orb %d"), *SectionName, PointIndex),
				SphereMesh,
				LightBaseLocation + FVector(0.0f, 0.0f, 95.0f),
				FVector(0.22f, 0.22f, 0.22f),
				LightColor);
		}
	}
}

void AMusicMountainPCGSectionActor::ClearPreviewDecorations()
{
	for (AActor* Actor : GeneratedPreviewActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	GeneratedPreviewActors.Reset();
}

void AMusicMountainPCGSectionActor::SpawnPreviewDecoration(const FString& Label, UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, const FLinearColor& Color, const FRotator& Rotation)
{
	if (!GetWorld() || !Mesh)
	{
		return;
	}

	AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation);
	if (!Actor)
	{
		return;
	}

#if WITH_EDITOR
	Actor->SetActorLabel(Label);
#endif
	UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetStaticMesh(Mesh);
	MeshComponent->SetWorldScale3D(Scale);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCollisionProfileName(TEXT("NoCollision"));

	if (BaseShapeMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseShapeMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color * 1.25f);
		MeshComponent->SetMaterial(0, Material);
	}

	GeneratedPreviewActors.Add(Actor);
}
