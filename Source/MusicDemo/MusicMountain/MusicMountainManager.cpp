// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicMountainManager.h"

#include "MusicMountainRuntimeHUD.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MusicMountain
{
	constexpr float PlatformThickness = 40.0f;
	constexpr float BasePlatformLength = 520.0f;
	constexpr float StartPlatformLength = 900.0f;
	constexpr float SummitPlatformLength = 1200.0f;
	constexpr float RampThickness = 36.0f;
}

AMusicMountainManager::AMusicMountainManager()
{
	PrimaryActorTick.bCanEverTick = true;

	MusicComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("MusicComponent"));
	SetRootComponent(MusicComponent);
	MusicComponent->bAutoActivate = false;
	MusicComponent->bAllowSpatialization = false;
}

void AMusicMountainManager::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BaseShapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (DemoMusic)
	{
		SetMusicSound(DemoMusic);
	}

	MusicComponent->OnAudioFinished.AddDynamic(this, &AMusicMountainManager::HandleMusicFinished);

	if (bGenerateOnBeginPlay)
	{
		GenerateDemoMountain();
	}

	CreateRuntimeHud();
}

void AMusicMountainManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveRuntimeHud();
	Super::EndPlay(EndPlayReason);
}

void AMusicMountainManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn || Sections.Num() == 0)
	{
		return;
	}

	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		CheckDemoHotkeys(PlayerController);
	}

	const FVector PawnLocation = PlayerPawn->GetActorLocation();
	if (PawnLocation.Z < KillZ)
	{
		RespawnPlayerAtCheckpoint();
		return;
	}

	UpdateCurrentSection(PawnLocation);
	CheckFinishCondition(PawnLocation);

	if (GEngine && CurrentSectionIndex != INDEX_NONE)
	{
		const FMusicMountainSection& Section = Sections[CurrentSectionIndex];
		float Progress = 0.0f;
		float BestDistanceSq = TNumericLimits<float>::Max();
		for (const FMusicMountainRoutePoint& RoutePoint : RoutePoints)
		{
			const float DistanceSq = FVector::DistSquared2D(PawnLocation, RoutePoint.Location);
			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				Progress = RoutePoint.NormalizedProgress;
			}
		}
		CurrentAltitudeProgress = Progress;

		const FString HudText = FString::Printf(
			TEXT("Music Mountain | %s | Section: %s (%s, %.0f%% energy) | Altitude %.0f%% | Time %.1fs | Music %s | P Pause | R Restart"),
			*DisplayName,
			*Section.Name,
			*Section.Terrain,
			Section.Energy * 100.0f,
			Progress * 100.0f,
			GetWorld() ? GetWorld()->GetTimeSeconds() - DemoStartTimeSeconds : 0.0f,
			IsMusicPaused() ? TEXT("Paused") : (IsMusicPlaying() ? TEXT("Playing") : TEXT("Stopped")));
		GEngine->AddOnScreenDebugMessage(1001, 0.0f, FColor::Cyan, HudText);
	}
}

void AMusicMountainManager::GenerateDemoMountain()
{
	ClearGeneratedMountain();

	if (!LoadAnalysis())
	{
		BuildFallbackAnalysis();
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector PawnLocation = PlayerPawn ? PlayerPawn->GetActorLocation() : GetActorLocation();
	const float GroundZ = PawnLocation.Z - 96.0f;

	const FVector RequestedStartLocation(
		PawnLocation.X + RouteForwardOffset,
		PawnLocation.Y,
		GroundZ + MusicMountain::PlatformThickness);
	GenerateSpiralRoutePoints(RequestedStartLocation, GroundZ);
	SpawnMountainCore(MountainCenterLocation, GroundZ);

	if (RoutePoints.Num() < 2)
	{
		return;
	}

	const FMusicMountainRoutePoint& StartPoint = RoutePoints[0];
	const FMusicMountainRoutePoint& SummitPoint = RoutePoints.Last();
	const FRotator StartRotation = StartPoint.Tangent.Rotation();
	DemoStartLocation = StartPoint.Location + FVector(0.0f, 0.0f, 180.0f);
	SummitLocation = SummitPoint.Location;

	SpawnPlatform(
		TEXT("Start Platform"),
		StartPoint.Location - StartPoint.Tangent * (MusicMountain::StartPlatformLength * 0.5f) - FVector(0.0f, 0.0f, MusicMountain::PlatformThickness * 0.5f),
		FVector(MusicMountain::StartPlatformLength, RouteWidth * 1.35f, MusicMountain::PlatformThickness),
		FLinearColor(0.15f, 0.45f, 0.25f),
		StartRotation);

	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		GenerateSection(Index, Sections[Index]);
	}

	const FRotator SummitRotation = SummitPoint.Tangent.Rotation();

	SpawnPlatform(
		TEXT("Summit Platform"),
		SummitPoint.Location + SummitPoint.Tangent * (MusicMountain::SummitPlatformLength * 0.5f) - FVector(0.0f, 0.0f, MusicMountain::PlatformThickness * 0.5f),
		FVector(MusicMountain::SummitPlatformLength, RouteWidth * 1.8f, MusicMountain::PlatformThickness * 1.3f),
		FLinearColor(1.0f, 0.78f, 0.28f),
		SummitRotation);

	SpawnDecoration(
		TEXT("Summit Gate Left"),
		SummitPoint.Location + SummitPoint.Tangent * (MusicMountain::SummitPlatformLength * 0.82f) + SummitPoint.Inward * (RouteWidth * 0.8f) + FVector(0.0f, 0.0f, 180.0f),
		FVector(0.45f, 0.45f, 3.0f),
		FLinearColor(1.0f, 0.9f, 0.45f));
	SpawnDecoration(
		TEXT("Summit Gate Right"),
		SummitPoint.Location + SummitPoint.Tangent * (MusicMountain::SummitPlatformLength * 0.82f) + SummitPoint.Outward * (RouteWidth * 0.8f) + FVector(0.0f, 0.0f, 180.0f),
		FVector(0.45f, 0.45f, 3.0f),
		FLinearColor(1.0f, 0.9f, 0.45f));

	LastCheckpointLocation = Sections.Num() > 0 ? Sections[0].CheckpointLocation : PawnLocation;
	KillZ = GroundZ - KillZOffset;
	CurrentSectionIndex = INDEX_NONE;
	bDemoCompleted = false;
	DemoStartTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	DemoFinishTimeSeconds = 0.0f;
	CurrentAltitudeProgress = 0.0f;
	ResetPlayerToStart();

	if (GEngine)
	{
		const float TotalElevationMeters = Sections.Num() > 0
			? FMath::Max((SummitPoint.Location.Z - StartPoint.Location.Z) / 100.0f, 0.0f)
			: 0.0f;
		GEngine->AddOnScreenDebugMessage(
			-1,
			7.0f,
			FColor::Green,
			FString::Printf(TEXT("Music Mountain generated: %s | BPM %.0f | seed %d | %.1f turns | +%.0fm climb | Theme: %s"), *DisplayName, Bpm, GenerationSeed, TotalTurns, TotalElevationMeters, *Theme));
	}

	if (bAutoPlayMusicOnGenerate)
	{
		PlayMusic();
	}
}

void AMusicMountainManager::ClearGeneratedMountain()
{
	for (AActor* Actor : GeneratedActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}

	GeneratedActors.Reset();
	Sections.Reset();
	RoutePoints.Reset();
	CurrentSectionIndex = INDEX_NONE;
	TotalRouteDistance = 1.0f;
}

void AMusicMountainManager::SetMusicSound(USoundBase* InMusic)
{
	ActiveMusic = InMusic;
	if (MusicComponent)
	{
		MusicComponent->SetSound(ActiveMusic);
	}
	bMusicPaused = false;
}

void AMusicMountainManager::PlayMusic()
{
	if (!MusicComponent)
	{
		return;
	}

	if (!ActiveMusic && DemoMusic)
	{
		SetMusicSound(DemoMusic);
	}

	if (!ActiveMusic)
	{
		return;
	}

	MusicComponent->SetPaused(false);
	MusicComponent->Play(0.0f);
	bMusicPaused = false;
}

void AMusicMountainManager::PauseMusic()
{
	if (MusicComponent && MusicComponent->IsPlaying())
	{
		MusicComponent->SetPaused(true);
		bMusicPaused = true;
	}
}

void AMusicMountainManager::ResumeMusic()
{
	if (!MusicComponent)
	{
		return;
	}

	if (bMusicPaused)
	{
		MusicComponent->SetPaused(false);
		bMusicPaused = false;
		return;
	}

	if (!MusicComponent->IsPlaying())
	{
		PlayMusic();
	}
}

void AMusicMountainManager::StopMusic()
{
	if (MusicComponent)
	{
		MusicComponent->Stop();
	}
	bMusicPaused = false;
}

void AMusicMountainManager::ToggleMusicPaused()
{
	if (bMusicPaused)
	{
		ResumeMusic();
	}
	else
	{
		PauseMusic();
	}
}

bool AMusicMountainManager::IsMusicPlaying() const
{
	return MusicComponent && MusicComponent->IsPlaying() && !bMusicPaused;
}

bool AMusicMountainManager::IsMusicPaused() const
{
	return bMusicPaused;
}

void AMusicMountainManager::RestartDemo()
{
	GenerateDemoMountain();
}

FString AMusicMountainManager::GetSongDisplayName() const
{
	return DisplayName;
}

FString AMusicMountainManager::GetThemeText() const
{
	return Theme;
}

FString AMusicMountainManager::GetCurrentSectionName() const
{
	return Sections.IsValidIndex(CurrentSectionIndex) ? Sections[CurrentSectionIndex].Name : TEXT("-");
}

FString AMusicMountainManager::GetCurrentMood() const
{
	return Sections.IsValidIndex(CurrentSectionIndex) ? Sections[CurrentSectionIndex].Mood : TEXT("-");
}

FString AMusicMountainManager::GetCurrentTerrain() const
{
	return Sections.IsValidIndex(CurrentSectionIndex) ? Sections[CurrentSectionIndex].Terrain : TEXT("-");
}

float AMusicMountainManager::GetCurrentSectionEnergy() const
{
	return Sections.IsValidIndex(CurrentSectionIndex) ? Sections[CurrentSectionIndex].Energy : 0.0f;
}

float AMusicMountainManager::GetAltitudeProgress() const
{
	return CurrentAltitudeProgress;
}

float AMusicMountainManager::GetElapsedSeconds() const
{
	if (!GetWorld())
	{
		return 0.0f;
	}
	return (bDemoCompleted ? DemoFinishTimeSeconds : GetWorld()->GetTimeSeconds()) - DemoStartTimeSeconds;
}

float AMusicMountainManager::GetFinishElapsedSeconds() const
{
	return FMath::Max(DemoFinishTimeSeconds - DemoStartTimeSeconds, 0.0f);
}

float AMusicMountainManager::GetTotalElevationMeters() const
{
	return RoutePoints.Num() > 1
		? FMath::Max((RoutePoints.Last().Location.Z - RoutePoints[0].Location.Z) / 100.0f, 0.0f)
		: 0.0f;
}

int32 AMusicMountainManager::GetMusicSeed() const
{
	return GenerationSeed;
}

float AMusicMountainManager::GetMusicBpm() const
{
	return Bpm;
}

bool AMusicMountainManager::IsDemoCompleted() const
{
	return bDemoCompleted;
}

bool AMusicMountainManager::LoadAnalysis()
{
	const FString FullPath = FPaths::ProjectContentDir() / AnalysisRelativePath;
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *FullPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Music Mountain could not read analysis file: %s"), *FullPath);
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Music Mountain could not parse analysis file: %s"), *FullPath);
		return false;
	}

	TrackName = RootObject->GetStringField(TEXT("track"));
	DisplayName = RootObject->HasTypedField<EJson::String>(TEXT("display_name"))
		? RootObject->GetStringField(TEXT("display_name"))
		: TrackName;
	Bpm = static_cast<float>(RootObject->GetNumberField(TEXT("bpm")));
	Theme = RootObject->GetStringField(TEXT("theme"));
	AudioAssetPath = RootObject->HasTypedField<EJson::String>(TEXT("audio_asset_path"))
		? RootObject->GetStringField(TEXT("audio_asset_path"))
		: TEXT("");
	if (!AudioAssetPath.IsEmpty())
	{
		if (USoundBase* ImportedMusic = LoadObject<USoundBase>(nullptr, *AudioAssetPath))
		{
			SetMusicSound(ImportedMusic);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Music Mountain could not load audio asset: %s"), *AudioAssetPath);
		}
	}

	const TSharedPtr<FJsonObject>* MountainPlanObject = nullptr;
	if (RootObject->TryGetObjectField(TEXT("mountain_plan"), MountainPlanObject) && MountainPlanObject && MountainPlanObject->IsValid())
	{
		const TSharedPtr<FJsonObject>& MountainPlan = *MountainPlanObject;
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("generation_seed")))
		{
			GenerationSeed = MountainPlan->GetIntegerField(TEXT("generation_seed"));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("mountain_height")))
		{
			MountainHeight = static_cast<float>(MountainPlan->GetNumberField(TEXT("mountain_height")));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("base_path_radius")))
		{
			BasePathRadius = static_cast<float>(MountainPlan->GetNumberField(TEXT("base_path_radius")));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("top_path_radius")))
		{
			TopPathRadius = static_cast<float>(MountainPlan->GetNumberField(TEXT("top_path_radius")));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("total_turns")))
		{
			TotalTurns = static_cast<float>(MountainPlan->GetNumberField(TEXT("total_turns")));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("segments_per_turn")))
		{
			SegmentsPerTurn = MountainPlan->GetIntegerField(TEXT("segments_per_turn"));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("road_width")))
		{
			RoadWidth = static_cast<float>(MountainPlan->GetNumberField(TEXT("road_width")));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("outer_slope_width")))
		{
			OuterSlopeWidth = static_cast<float>(MountainPlan->GetNumberField(TEXT("outer_slope_width")));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("inner_wall_height")))
		{
			InnerWallHeight = static_cast<float>(MountainPlan->GetNumberField(TEXT("inner_wall_height")));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("elevation_gain_multiplier")))
		{
			ElevationGainMultiplier = static_cast<float>(MountainPlan->GetNumberField(TEXT("elevation_gain_multiplier")));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("max_ramp_pitch_degrees")))
		{
			MaxRampPitchDegrees = static_cast<float>(MountainPlan->GetNumberField(TEXT("max_ramp_pitch_degrees")));
		}
		if (MountainPlan->HasTypedField<EJson::Number>(TEXT("visibility_range_meters")))
		{
			VisibilityRangeMeters = static_cast<float>(MountainPlan->GetNumberField(TEXT("visibility_range_meters")));
		}

		const TSharedPtr<FJsonObject>* VariationObject = nullptr;
		if (MountainPlan->TryGetObjectField(TEXT("variation"), VariationObject) && VariationObject && VariationObject->IsValid())
		{
			const TSharedPtr<FJsonObject>& Variation = *VariationObject;
			if (Variation->HasTypedField<EJson::Number>(TEXT("radius")))
			{
				RadiusVariationStrength = static_cast<float>(Variation->GetNumberField(TEXT("radius")));
			}
			if (Variation->HasTypedField<EJson::Number>(TEXT("height")))
			{
				HeightVariationStrength = static_cast<float>(Variation->GetNumberField(TEXT("height")));
			}
			if (Variation->HasTypedField<EJson::Number>(TEXT("road_width")))
			{
				RoadWidthVariationStrength = static_cast<float>(Variation->GetNumberField(TEXT("road_width")));
			}
			if (Variation->HasTypedField<EJson::Number>(TEXT("core")))
			{
				CoreVariationStrength = static_cast<float>(Variation->GetNumberField(TEXT("core")));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* SectionValues = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("sections"), SectionValues))
	{
		return false;
	}

	Sections.Reset();
	for (const TSharedPtr<FJsonValue>& SectionValue : *SectionValues)
	{
		const TSharedPtr<FJsonObject> SectionObject = SectionValue->AsObject();
		if (!SectionObject.IsValid())
		{
			continue;
		}

		FMusicMountainSection Section;
		Section.Name = SectionObject->GetStringField(TEXT("name"));
		Section.StartTime = static_cast<float>(SectionObject->GetNumberField(TEXT("start")));
		Section.EndTime = static_cast<float>(SectionObject->GetNumberField(TEXT("end")));
		Section.Mood = SectionObject->GetStringField(TEXT("mood"));
		Section.Energy = FMath::Clamp(static_cast<float>(SectionObject->GetNumberField(TEXT("energy"))), 0.0f, 1.0f);
		Section.Terrain = SectionObject->GetStringField(TEXT("terrain"));
		Section.AudioStyle = SectionObject->HasTypedField<EJson::String>(TEXT("audio_style"))
			? SectionObject->GetStringField(TEXT("audio_style"))
			: TEXT("original mix");
		Section.ThemeColor = ResolveThemeColor(Section);
		Sections.Add(Section);
	}

	return Sections.Num() > 0;
}

void AMusicMountainManager::BuildFallbackAnalysis()
{
	TrackName = TEXT("fallback_track");
	DisplayName = TEXT("Fallback Music Mountain");
	Bpm = 128.0f;
	Theme = TEXT("cold, epic, lonely");

	const struct
	{
		const TCHAR* Name;
		const TCHAR* Mood;
		const TCHAR* Terrain;
		const TCHAR* AudioStyle;
		float Energy;
	} Defaults[] = {
		{ TEXT("intro"), TEXT("calm"), TEXT("forest_slope"), TEXT("soft low-pass, birds, light wind"), 0.25f },
		{ TEXT("verse"), TEXT("dark"), TEXT("cliff_path"), TEXT("darker EQ, distant rock fall, narrow stereo"), 0.45f },
		{ TEXT("chorus"), TEXT("epic"), TEXT("jump_ridge"), TEXT("open mix, stronger drums, high wind"), 0.85f },
		{ TEXT("bridge"), TEXT("tense"), TEXT("cave_bridge"), TEXT("cave reverb, low rumble, muted highs"), 0.55f },
		{ TEXT("final"), TEXT("uplifting"), TEXT("summit_run"), TEXT("full mix, bright air, summit swell"), 0.95f },
	};

	Sections.Reset();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Defaults); ++Index)
	{
		FMusicMountainSection Section;
		Section.Name = Defaults[Index].Name;
		Section.StartTime = Index * 30.0f;
		Section.EndTime = Section.StartTime + 30.0f;
		Section.Mood = Defaults[Index].Mood;
		Section.Terrain = Defaults[Index].Terrain;
		Section.AudioStyle = Defaults[Index].AudioStyle;
		Section.Energy = Defaults[Index].Energy;
		Section.ThemeColor = ResolveThemeColor(Section);
		Sections.Add(Section);
	}
}

void AMusicMountainManager::GenerateSpiralRoutePoints(const FVector& StartLocation, float GroundZ)
{
	RoutePoints.Reset();

	const int32 SectionCount = FMath::Max(Sections.Num(), 1);
	const int32 SegmentCount = FMath::Max(FMath::RoundToInt(FMath::Max(TotalTurns, 0.75f) * FMath::Max(SegmentsPerTurn, 16)), SectionCount * 8);
	const float StartAngleRadians = FMath::DegreesToRadians(-90.0f);
	const float ClampedBaseRadius = FMath::Max(BasePathRadius, 500.0f);
	const float ClampedTopRadius = FMath::Clamp(TopPathRadius, 300.0f, ClampedBaseRadius);
	const float AverageRadius = (ClampedBaseRadius + ClampedTopRadius) * 0.5f;
	const float ApproxHorizontalLength = AverageRadius * UE_TWO_PI * FMath::Max(TotalTurns, 0.75f);
	const float MaxHeightByPitch = ApproxHorizontalLength * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(MaxRampPitchDegrees, 8.0f, 32.0f)));
	const float FinalMountainHeight = FMath::Min(MountainHeight * ElevationGainMultiplier, MaxHeightByPitch);
	GeneratedMountainHeight = FinalMountainHeight;

	MountainCenterLocation = StartLocation - FVector(FMath::Cos(StartAngleRadians), FMath::Sin(StartAngleRadians), 0.0f) * ClampedBaseRadius;
	MountainCenterLocation.Z = GroundZ;

	TArray<FVector> Locations;
	Locations.Reserve(SegmentCount + 1);
	for (int32 PointIndex = 0; PointIndex <= SegmentCount; ++PointIndex)
	{
		const float Alpha = static_cast<float>(PointIndex) / static_cast<float>(SegmentCount);
		const float Angle = StartAngleRadians + Alpha * FMath::Max(TotalTurns, 0.75f) * UE_TWO_PI;
		const float RadiusNoise = FMath::PerlinNoise1D(Alpha * 5.0f + GetSeedOffset(11.0f));
		const float FineRadiusNoise = FMath::PerlinNoise1D(Alpha * 19.0f + GetSeedOffset(17.0f)) * 0.35f;
		const float RadiusVariation = 1.0f + (RadiusNoise + FineRadiusNoise) * RadiusVariationStrength;
		const float Radius = FMath::Lerp(ClampedBaseRadius, ClampedTopRadius, Alpha) * FMath::Clamp(RadiusVariation, 0.68f, 1.38f);
		const float HeightNoise = FMath::PerlinNoise1D(Alpha * 4.0f + GetSeedOffset(23.0f)) * HeightVariationStrength * FinalMountainHeight * Alpha * (1.0f - Alpha);
		const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		Locations.Add(MountainCenterLocation + Radial * Radius + FVector(0.0f, 0.0f, MusicMountain::PlatformThickness + Alpha * FinalMountainHeight + HeightNoise));
	}

	float AccumulatedDistance = 0.0f;
	RoutePoints.SetNum(Locations.Num());
	for (int32 PointIndex = 0; PointIndex < Locations.Num(); ++PointIndex)
	{
		if (PointIndex > 0)
		{
			AccumulatedDistance += FVector::Dist(Locations[PointIndex - 1], Locations[PointIndex]);
		}

		FVector Tangent = FVector::ForwardVector;
		if (PointIndex == 0)
		{
			Tangent = (Locations[1] - Locations[0]).GetSafeNormal();
		}
		else if (PointIndex == Locations.Num() - 1)
		{
			Tangent = (Locations.Last() - Locations[Locations.Num() - 2]).GetSafeNormal();
		}
		else
		{
			Tangent = (Locations[PointIndex + 1] - Locations[PointIndex - 1]).GetSafeNormal();
		}

		FVector Inward = (MountainCenterLocation - Locations[PointIndex]).GetSafeNormal2D();
		if (Inward.IsNearlyZero())
		{
			Inward = -FVector::RightVector;
		}

		FMusicMountainRoutePoint& RoutePoint = RoutePoints[PointIndex];
		RoutePoint.Location = Locations[PointIndex];
		RoutePoint.Tangent = Tangent;
		RoutePoint.Inward = Inward;
		RoutePoint.Outward = -Inward;
		RoutePoint.Distance = AccumulatedDistance;
		RoutePoint.RoadWidthScale = FMath::Clamp(1.0f + FMath::PerlinNoise1D(RoutePoint.NormalizedProgress * 13.0f + GetSeedOffset(31.0f)) * RoadWidthVariationStrength, 0.72f, 1.32f);
		RoutePoint.OuterSlopeScale = FMath::Clamp(1.0f + FMath::PerlinNoise1D(RoutePoint.NormalizedProgress * 9.0f + GetSeedOffset(37.0f)) * RoadWidthVariationStrength, 0.78f, 1.42f);
	}

	TotalRouteDistance = FMath::Max(AccumulatedDistance, 1.0f);
	for (FMusicMountainRoutePoint& RoutePoint : RoutePoints)
	{
		RoutePoint.NormalizedProgress = RoutePoint.Distance / TotalRouteDistance;
		RoutePoint.SectionIndex = FMath::Clamp(FMath::FloorToInt(RoutePoint.NormalizedProgress * SectionCount), 0, SectionCount - 1);
		RoutePoint.RoadWidthScale = FMath::Clamp(1.0f + FMath::PerlinNoise1D(RoutePoint.NormalizedProgress * 13.0f + GetSeedOffset(31.0f)) * RoadWidthVariationStrength, 0.72f, 1.32f);
		RoutePoint.OuterSlopeScale = FMath::Clamp(1.0f + FMath::PerlinNoise1D(RoutePoint.NormalizedProgress * 9.0f + GetSeedOffset(37.0f)) * RoadWidthVariationStrength, 0.78f, 1.42f);
	}
}

void AMusicMountainManager::SpawnMountainCore(const FVector& MountainCenter, float GroundZ)
{
	const int32 LayerCount = 9;
	for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
	{
		const float Alpha = static_cast<float>(LayerIndex) / static_cast<float>(LayerCount - 1);
		const float LayerNoise = FMath::PerlinNoise1D(Alpha * 4.0f + GetSeedOffset(43.0f));
		const float Radius = FMath::Lerp(BasePathRadius * 0.78f, TopPathRadius * 0.55f, Alpha) * (1.0f + LayerNoise * CoreVariationStrength);
		const float LayerZ = GroundZ + Alpha * GeneratedMountainHeight * 0.92f + Radius * 0.12f + LayerNoise * CoreVariationStrength * 180.0f;
		const FLinearColor LayerColor = FLinearColor::LerpUsingHSV(FLinearColor(0.18f, 0.22f, 0.18f), FLinearColor(0.55f, 0.58f, 0.52f), Alpha);
		const float XScaleNoise = 1.0f + FMath::PerlinNoise1D(Alpha * 7.0f + GetSeedOffset(47.0f)) * CoreVariationStrength;
		const float YScaleNoise = 1.0f + FMath::PerlinNoise1D(Alpha * 7.0f + GetSeedOffset(53.0f)) * CoreVariationStrength;
		SpawnDecoration(
			FString::Printf(TEXT("Mountain Core Layer %d"), LayerIndex + 1),
			FVector(MountainCenter.X, MountainCenter.Y, LayerZ),
			FVector(Radius / 55.0f * XScaleNoise, Radius / 55.0f * YScaleNoise, FMath::Lerp(5.0f, 2.5f, Alpha)),
			LayerColor);
	}
}

void AMusicMountainManager::GenerateSection(int32 SectionIndex, FMusicMountainSection& Section)
{
	Section.ThemeColor = ResolveThemeColor(Section);

	TArray<int32> PointIndices;
	for (int32 PointIndex = 0; PointIndex < RoutePoints.Num(); ++PointIndex)
	{
		if (RoutePoints[PointIndex].SectionIndex == SectionIndex)
		{
			PointIndices.Add(PointIndex);
		}
	}

	if (PointIndices.Num() < 2)
	{
		return;
	}

	const int32 FirstPointIndex = PointIndices[0];
	const int32 LastPointIndex = PointIndices.Last();
	const FMusicMountainRoutePoint& FirstPoint = RoutePoints[FirstPointIndex];
	const FMusicMountainRoutePoint& LastPoint = RoutePoints[LastPointIndex];
	const float SectionRoadWidth = FMath::Lerp(RoadWidth * 1.25f, RoadWidth * 0.82f, Section.Energy);
	const float SectionOuterSlopeWidth = FMath::Lerp(OuterSlopeWidth * 0.8f, OuterSlopeWidth * 1.35f, Section.Energy);
	const FLinearColor RoadColor = ResolveRoadColor(Section);
	const FLinearColor InnerWallColor = ResolveInnerWallColor(Section);
	const FLinearColor OuterSlopeColor = ResolveOuterSlopeColor(Section);
	const FLinearColor RockColor = ResolveRockColor(Section);
	FRandomStream SectionRandom(GenerationSeed + SectionIndex * 1009 + 73);

	Section.RouteStartDistance = FirstPoint.Distance;
	Section.RouteEndDistance = LastPoint.Distance;
	Section.CheckpointLocation = FirstPoint.Location + FirstPoint.Tangent * 180.0f + FVector(0.0f, 0.0f, 170.0f);
	Section.RouteCenter = (FirstPoint.Location + LastPoint.Location) * 0.5f;
	Section.RouteRadius = FVector::Dist2D(FirstPoint.Location, LastPoint.Location) * 0.5f + BasePathRadius;

	SpawnDecoration(
		FString::Printf(TEXT("Checkpoint %s"), *Section.Name),
		Section.CheckpointLocation + FirstPoint.Inward * (SectionRoadWidth * 0.55f),
		FVector(0.35f, 0.35f, 1.8f),
		RockColor);

	for (int32 LocalIndex = 0; LocalIndex < PointIndices.Num() - 1; ++LocalIndex)
	{
		const int32 StartIndex = PointIndices[LocalIndex];
		const int32 EndIndex = PointIndices[LocalIndex + 1];
		const FMusicMountainRoutePoint& StartPoint = RoutePoints[StartIndex];
		const FMusicMountainRoutePoint& EndPoint = RoutePoints[EndIndex];
		const float SegmentRoadWidth = SectionRoadWidth * ((StartPoint.RoadWidthScale + EndPoint.RoadWidthScale) * 0.5f);
		const float SegmentOuterSlopeWidth = SectionOuterSlopeWidth * ((StartPoint.OuterSlopeScale + EndPoint.OuterSlopeScale) * 0.5f);

		SpawnSlopedBlock(
			FString::Printf(TEXT("%s Road Surface %d"), *Section.Name, LocalIndex + 1),
			StartPoint.Location,
			EndPoint.Location,
			SegmentRoadWidth,
			MusicMountain::RampThickness,
			true,
			RoadColor);

		SpawnSlopedBlock(
			FString::Printf(TEXT("%s Inner Wall %d"), *Section.Name, LocalIndex + 1),
			StartPoint.Location + StartPoint.Inward * (SegmentRoadWidth * 0.58f) - FVector(0.0f, 0.0f, InnerWallHeight * 0.42f),
			EndPoint.Location + EndPoint.Inward * (SegmentRoadWidth * 0.58f) - FVector(0.0f, 0.0f, InnerWallHeight * 0.42f),
			SegmentRoadWidth * 0.55f,
			InnerWallHeight,
			false,
			InnerWallColor);

		SpawnSlopedBlock(
			FString::Printf(TEXT("%s Outer Slope %d"), *Section.Name, LocalIndex + 1),
			StartPoint.Location + StartPoint.Outward * (SegmentRoadWidth * 0.5f + SegmentOuterSlopeWidth * 0.5f) - FVector(0.0f, 0.0f, 140.0f),
			EndPoint.Location + EndPoint.Outward * (SegmentRoadWidth * 0.5f + SegmentOuterSlopeWidth * 0.5f) - FVector(0.0f, 0.0f, 140.0f),
			SegmentOuterSlopeWidth,
			260.0f,
			false,
			OuterSlopeColor);

		if (SectionRandom.FRand() < FMath::Lerp(0.22f, 0.48f, Section.Energy))
		{
			const float RockForwardOffset = SectionRandom.FRandRange(-90.0f, 90.0f);
			const float RockOutwardOffset = SectionRandom.FRandRange(0.15f, 0.75f);
			const float RockScale = SectionRandom.FRandRange(0.65f, 1.35f);
			SpawnDecoration(
				FString::Printf(TEXT("%s Edge Rock %d"), *Section.Name, LocalIndex + 1),
				StartPoint.Location + StartPoint.Tangent * RockForwardOffset + StartPoint.Outward * (SegmentRoadWidth * 0.62f + SegmentOuterSlopeWidth * RockOutwardOffset) + FVector(0.0f, 0.0f, 80.0f),
				FVector((0.8f + Section.Energy) * RockScale, (0.8f + Section.Energy) * SectionRandom.FRandRange(0.7f, 1.25f), (0.9f + Section.Energy * 1.6f) * RockScale),
				RockColor);
		}
	}
}

void AMusicMountainManager::SpawnPlatform(const FString& Label, const FVector& SurfaceCenter, const FVector& Size, const FLinearColor& Color, const FRotator& Rotation)
{
	if (!GetWorld() || !CubeMesh)
	{
		return;
	}

	AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), SurfaceCenter, Rotation);
	if (!Actor)
	{
		return;
	}

#if WITH_EDITOR
	Actor->SetActorLabel(Label);
#endif
	UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
	MeshComponent->SetStaticMesh(CubeMesh);
	MeshComponent->SetWorldScale3D(Size / 100.0f);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
	MeshComponent->SetCullDistance(GetClampedVisibilityRangeCm());

	if (BaseShapeMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseShapeMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		MeshComponent->SetMaterial(0, Material);
	}

	GeneratedActors.Add(Actor);
}

void AMusicMountainManager::SpawnSlopedBlock(const FString& Label, const FVector& StartSurface, const FVector& EndSurface, float Width, float Thickness, bool bEnableCollision, const FLinearColor& Color)
{
	if (!GetWorld() || !CubeMesh)
	{
		return;
	}

	const FVector Delta = EndSurface - StartSurface;
	const float HorizontalLength = FVector(Delta.X, Delta.Y, 0.0f).Size();
	const float Hypotenuse = Delta.Size();
	const float PitchDegrees = FMath::RadiansToDegrees(FMath::Atan2(Delta.Z, FMath::Max(HorizontalLength, 1.0f)));
	const float YawDegrees = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	const FVector Center = (StartSurface + EndSurface) * 0.5f - FVector(0.0f, 0.0f, Thickness * 0.5f);

	AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Center, FRotator(PitchDegrees, YawDegrees, 0.0f));
	if (!Actor)
	{
		return;
	}

#if WITH_EDITOR
	Actor->SetActorLabel(Label);
#endif
	UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
	MeshComponent->SetStaticMesh(CubeMesh);
	MeshComponent->SetWorldScale3D(FVector(Hypotenuse / 100.0f, Width / 100.0f, Thickness / 100.0f));
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionEnabled(bEnableCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	MeshComponent->SetCollisionProfileName(bEnableCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
	MeshComponent->SetCullDistance(GetClampedVisibilityRangeCm());

	if (BaseShapeMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseShapeMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		MeshComponent->SetMaterial(0, Material);
	}

	GeneratedActors.Add(Actor);
}

void AMusicMountainManager::SpawnDecoration(const FString& Label, const FVector& Location, const FVector& Scale, const FLinearColor& Color)
{
	if (!GetWorld())
	{
		return;
	}

	UStaticMesh* MeshToUse = SphereMesh ? SphereMesh : CubeMesh;
	if (!MeshToUse)
	{
		return;
	}

	AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, FRotator(0.0f, FMath::RandRange(-25.0f, 25.0f), 0.0f));
	if (!Actor)
	{
		return;
	}

#if WITH_EDITOR
	Actor->SetActorLabel(Label);
#endif
	UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
	MeshComponent->SetStaticMesh(MeshToUse);
	MeshComponent->SetWorldScale3D(Scale);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCullDistance(GetClampedVisibilityRangeCm());

	if (BaseShapeMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseShapeMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color * 1.25f);
		MeshComponent->SetMaterial(0, Material);
	}

	GeneratedActors.Add(Actor);
}

void AMusicMountainManager::UpdateCurrentSection(const FVector& PawnLocation)
{
	int32 BestRoutePointIndex = INDEX_NONE;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (int32 PointIndex = 0; PointIndex < RoutePoints.Num(); ++PointIndex)
	{
		const float DistanceSq = FVector::DistSquared2D(PawnLocation, RoutePoints[PointIndex].Location);
		if (DistanceSq < BestDistanceSq)
		{
			BestRoutePointIndex = PointIndex;
			BestDistanceSq = DistanceSq;
		}
	}

	if (BestRoutePointIndex == INDEX_NONE)
	{
		return;
	}

	const int32 BestIndex = RoutePoints[BestRoutePointIndex].SectionIndex;
	if (BestIndex != INDEX_NONE && CurrentSectionIndex != BestIndex)
	{
		CurrentSectionIndex = BestIndex;
		LastCheckpointLocation = Sections[BestIndex].CheckpointLocation;
		ShowSectionMessage(Sections[BestIndex]);
	}
}

void AMusicMountainManager::RespawnPlayerAtCheckpoint()
{
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		return;
	}

	PlayerPawn->SetActorLocation(LastCheckpointLocation, false, nullptr, ETeleportType::TeleportPhysics);
	if (ACharacter* Character = Cast<ACharacter>(PlayerPawn))
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, TEXT("Fell off the mountain. Respawned at the latest music checkpoint."));
	}
}

void AMusicMountainManager::ResetPlayerToStart()
{
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		return;
	}

	PlayerPawn->SetActorLocation(DemoStartLocation, false, nullptr, ETeleportType::TeleportPhysics);
	if (ACharacter* Character = Cast<ACharacter>(PlayerPawn))
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
	}

	LastCheckpointLocation = DemoStartLocation;
}

void AMusicMountainManager::CheckDemoHotkeys(APlayerController* PlayerController)
{
	const bool bTabKeyDown = PlayerController->IsInputKeyDown(EKeys::Tab);
	if (bTabKeyDown && !bPreviousTabKeyDown)
	{
		ToggleCursorMode(PlayerController);
	}
	bPreviousTabKeyDown = bTabKeyDown;

	const bool bPauseKeyDown = PlayerController->IsInputKeyDown(EKeys::P);
	if (bPauseKeyDown && !bPreviousPauseKeyDown)
	{
		ToggleMusicPaused();
	}
	bPreviousPauseKeyDown = bPauseKeyDown;

	const bool bRestartKeyDown = PlayerController->IsInputKeyDown(EKeys::R);
	if (bRestartKeyDown && !bPreviousRestartKeyDown)
	{
		RestartDemo();
	}
	bPreviousRestartKeyDown = bRestartKeyDown;
}

void AMusicMountainManager::ToggleCursorMode(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		return;
	}

	bCursorModeEnabled = !bCursorModeEnabled;
	PlayerController->bShowMouseCursor = bCursorModeEnabled;

	if (bCursorModeEnabled)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
	}
	else
	{
		FInputModeGameOnly InputMode;
		PlayerController->SetInputMode(InputMode);
	}
}

void AMusicMountainManager::CheckFinishCondition(const FVector& PawnLocation)
{
	if (bDemoCompleted || RoutePoints.Num() == 0)
	{
		return;
	}

	const float DistanceToSummit = FVector::Dist(PawnLocation, SummitLocation);
	if (DistanceToSummit <= FinishDistanceCm || PawnLocation.Z >= SummitLocation.Z - 120.0f)
	{
		CompleteDemo();
	}
}

void AMusicMountainManager::CompleteDemo()
{
	bDemoCompleted = true;
	DemoFinishTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : DemoStartTimeSeconds;
	const float ElapsedSeconds = FMath::Max(DemoFinishTimeSeconds - DemoStartTimeSeconds, 0.0f);
	const float TotalElevationMeters = GetTotalElevationMeters();

	if (GEngine)
	{
		const FString Message = FString::Printf(
			TEXT("Summit Complete!\nSong: %s | BPM %.0f | Seed %d\nTime: %.1fs | Climb: %.0fm | Theme: %s\nPress R to regenerate / restart."),
			*DisplayName,
			Bpm,
			GenerationSeed,
			ElapsedSeconds,
			TotalElevationMeters,
			*Theme);
		GEngine->AddOnScreenDebugMessage(2001, 12.0f, FColor::Green, Message);
	}
}

void AMusicMountainManager::CreateRuntimeHud()
{
	if (RuntimeHudWidget.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	RuntimeHudWidget = SNew(SMusicMountainRuntimeHUD)
		.Manager(TWeakObjectPtr<AMusicMountainManager>(this));
	GEngine->GameViewport->AddViewportWidgetContent(RuntimeHudWidget.ToSharedRef(), 10);
}

void AMusicMountainManager::RemoveRuntimeHud()
{
	if (RuntimeHudWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(RuntimeHudWidget.ToSharedRef());
	}
	RuntimeHudWidget.Reset();
}

void AMusicMountainManager::ShowSectionMessage(const FMusicMountainSection& Section)
{
	if (!GEngine)
	{
		return;
	}

	const FString Message = FString::Printf(
		TEXT("Now entering %s | mood: %s | terrain: %s | audio: %s"),
		*Section.Name,
		*Section.Mood,
		*Section.Terrain,
		*Section.AudioStyle);
	GEngine->AddOnScreenDebugMessage(-1, 6.0f, Section.ThemeColor.ToFColor(true), Message);
}

FLinearColor AMusicMountainManager::ResolveThemeColor(const FMusicMountainSection& Section) const
{
	const FString Mood = Section.Mood.ToLower();
	const FString Terrain = Section.Terrain.ToLower();

	if (Mood.Contains(TEXT("romantic")) || Terrain.Contains(TEXT("flower")))
	{
		return FLinearColor(1.0f, 0.48f, 0.62f);
	}
	if (Mood.Contains(TEXT("sweet")) || Terrain.Contains(TEXT("meadow")))
	{
		return FLinearColor(0.95f, 0.74f, 0.42f);
	}
	if (Mood.Contains(TEXT("melancholy")) || Terrain.Contains(TEXT("misty")))
	{
		return FLinearColor(0.42f, 0.55f, 0.72f);
	}
	if (Mood.Contains(TEXT("dreamy")) || Terrain.Contains(TEXT("cloud")))
	{
		return FLinearColor(0.62f, 0.58f, 1.0f);
	}
	if (Mood.Contains(TEXT("modern")) || Terrain.Contains(TEXT("glass")))
	{
		return FLinearColor(0.35f, 0.78f, 0.9f);
	}
	if (Mood.Contains(TEXT("pop")) || Terrain.Contains(TEXT("neon")))
	{
		return FLinearColor(1.0f, 0.34f, 0.88f);
	}
	if (Mood.Contains(TEXT("electronic")) || Terrain.Contains(TEXT("cyber")))
	{
		return FLinearColor(0.0f, 0.9f, 1.0f);
	}
	if (Mood.Contains(TEXT("rock")) || Terrain.Contains(TEXT("broken")))
	{
		return FLinearColor(0.75f, 0.28f, 0.16f);
	}
	if (Mood.Contains(TEXT("acoustic")) || Terrain.Contains(TEXT("woodland")))
	{
		return FLinearColor(0.52f, 0.68f, 0.28f);
	}
	if (Mood.Contains(TEXT("classical")) || Terrain.Contains(TEXT("marble")))
	{
		return FLinearColor(0.86f, 0.82f, 0.72f);
	}
	if (Mood.Contains(TEXT("calm")) || Terrain.Contains(TEXT("forest")))
	{
		return FLinearColor(0.18f, 0.65f, 0.32f);
	}
	if (Mood.Contains(TEXT("dark")) || Terrain.Contains(TEXT("cliff")))
	{
		return FLinearColor(0.22f, 0.28f, 0.42f);
	}
	if (Mood.Contains(TEXT("epic")) || Terrain.Contains(TEXT("jump")))
	{
		return FLinearColor(0.92f, 0.48f, 0.18f);
	}
	if (Mood.Contains(TEXT("tense")) || Terrain.Contains(TEXT("cave")))
	{
		return FLinearColor(0.34f, 0.23f, 0.42f);
	}
	if (Mood.Contains(TEXT("uplifting")) || Terrain.Contains(TEXT("summit")))
	{
		return FLinearColor(0.95f, 0.82f, 0.32f);
	}

	return FLinearColor(0.55f, 0.65f, 0.78f);
}

FLinearColor AMusicMountainManager::ResolveRoadColor(const FMusicMountainSection& Section) const
{
	return ResolveThemeColor(Section) * 1.18f + FLinearColor(0.08f, 0.08f, 0.08f);
}

FLinearColor AMusicMountainManager::ResolveInnerWallColor(const FMusicMountainSection& Section) const
{
	const FString Mood = Section.Mood.ToLower();
	if (Mood.Contains(TEXT("modern")) || Mood.Contains(TEXT("electronic")) || Mood.Contains(TEXT("pop")))
	{
		return ResolveThemeColor(Section) * 0.42f + FLinearColor(0.02f, 0.03f, 0.06f);
	}
	if (Mood.Contains(TEXT("romantic")) || Mood.Contains(TEXT("sweet")) || Mood.Contains(TEXT("dreamy")))
	{
		return ResolveThemeColor(Section) * 0.55f + FLinearColor(0.18f, 0.16f, 0.2f);
	}
	return ResolveThemeColor(Section) * 0.38f;
}

FLinearColor AMusicMountainManager::ResolveOuterSlopeColor(const FMusicMountainSection& Section) const
{
	const FString Mood = Section.Mood.ToLower();
	if (Mood.Contains(TEXT("romantic")) || Mood.Contains(TEXT("sweet")))
	{
		return FLinearColor(0.52f, 0.42f, 0.38f);
	}
	if (Mood.Contains(TEXT("dreamy")))
	{
		return FLinearColor(0.32f, 0.36f, 0.56f);
	}
	if (Mood.Contains(TEXT("modern")) || Mood.Contains(TEXT("electronic")) || Mood.Contains(TEXT("pop")))
	{
		return ResolveThemeColor(Section) * 0.24f;
	}
	return ResolveThemeColor(Section) * 0.32f;
}

FLinearColor AMusicMountainManager::ResolveRockColor(const FMusicMountainSection& Section) const
{
	const FString Mood = Section.Mood.ToLower();
	if (Mood.Contains(TEXT("romantic")) || Mood.Contains(TEXT("sweet")))
	{
		return FLinearColor(1.0f, 0.72f, 0.52f);
	}
	if (Mood.Contains(TEXT("modern")) || Mood.Contains(TEXT("pop")) || Mood.Contains(TEXT("electronic")))
	{
		return ResolveThemeColor(Section) * 1.35f;
	}
	if (Mood.Contains(TEXT("classical")))
	{
		return FLinearColor(0.95f, 0.92f, 0.82f);
	}
	return ResolveThemeColor(Section) * 1.08f;
}

float AMusicMountainManager::GetClampedVisibilityRangeCm() const
{
	return FMath::Clamp(VisibilityRangeMeters, 200.0f, 5000.0f) * 100.0f;
}

float AMusicMountainManager::GetSeedOffset(float Salt) const
{
	const int32 EffectiveSeed = GenerationSeed != 0 ? GenerationSeed : GetTypeHash(TrackName);
	return static_cast<float>((EffectiveSeed % 100000) + FMath::RoundToInt(Salt * 997.0f)) * 0.001f;
}

void AMusicMountainManager::HandleMusicFinished()
{
	if (bLoopMusic && !bMusicPaused && ActiveMusic && MusicComponent)
	{
		MusicComponent->Play(0.0f);
	}
}
