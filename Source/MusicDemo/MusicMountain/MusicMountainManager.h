// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MusicMountainManager.generated.h"

class UAudioComponent;
class UMaterialInterface;
class USoundBase;
class UStaticMesh;
class SWidget;

struct FMusicMountainRoutePoint
{
	FVector Location = FVector::ZeroVector;
	FVector Tangent = FVector::ForwardVector;
	FVector Inward = -FVector::RightVector;
	FVector Outward = FVector::RightVector;
	float Distance = 0.0f;
	float NormalizedProgress = 0.0f;
	float RoadWidthScale = 1.0f;
	float OuterSlopeScale = 1.0f;
	int32 SectionIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct FMusicMountainSection
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Music Mountain")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Music Mountain")
	float StartTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Music Mountain")
	float EndTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Music Mountain")
	FString Mood;

	UPROPERTY(BlueprintReadOnly, Category = "Music Mountain")
	float Energy = 0.5f;

	UPROPERTY(BlueprintReadOnly, Category = "Music Mountain")
	FString Terrain;

	UPROPERTY(BlueprintReadOnly, Category = "Music Mountain")
	FString AudioStyle;

	float RouteStartDistance = 0.0f;
	float RouteEndDistance = 0.0f;
	FVector RouteCenter = FVector::ZeroVector;
	float RouteRadius = 0.0f;
	FVector CheckpointLocation = FVector::ZeroVector;
	FLinearColor ThemeColor = FLinearColor::White;
};

UCLASS()
class MUSICDEMO_API AMusicMountainManager : public AActor
{
	GENERATED_BODY()

public:
	AMusicMountainManager();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Music Mountain")
	void GenerateDemoMountain();

	UFUNCTION(BlueprintCallable, Category = "Music Mountain")
	void ClearGeneratedMountain();

	UFUNCTION(BlueprintCallable, Category = "Music Mountain|Audio")
	void SetMusicSound(USoundBase* InMusic);

	UFUNCTION(BlueprintCallable, Category = "Music Mountain|Audio")
	void PlayMusic();

	UFUNCTION(BlueprintCallable, Category = "Music Mountain|Audio")
	void PauseMusic();

	UFUNCTION(BlueprintCallable, Category = "Music Mountain|Audio")
	void ResumeMusic();

	UFUNCTION(BlueprintCallable, Category = "Music Mountain|Audio")
	void StopMusic();

	UFUNCTION(BlueprintCallable, Category = "Music Mountain|Audio")
	void ToggleMusicPaused();

	UFUNCTION(BlueprintPure, Category = "Music Mountain|Audio")
	bool IsMusicPlaying() const;

	UFUNCTION(BlueprintPure, Category = "Music Mountain|Audio")
	bool IsMusicPaused() const;

	UFUNCTION(BlueprintCallable, Category = "Music Mountain|Demo")
	void RestartDemo();

	FString GetSongDisplayName() const;
	FString GetThemeText() const;
	FString GetCurrentSectionName() const;
	FString GetCurrentMood() const;
	FString GetCurrentTerrain() const;
	float GetCurrentSectionEnergy() const;
	float GetAltitudeProgress() const;
	float GetElapsedSeconds() const;
	float GetFinishElapsedSeconds() const;
	float GetTotalElevationMeters() const;
	int32 GetMusicSeed() const;
	float GetMusicBpm() const;
	bool IsDemoCompleted() const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain")
	bool bGenerateOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain")
	FString AnalysisRelativePath = TEXT("MusicMountain/Data/MusicAnalysisDemo.json");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain")
	float KillZOffset = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain")
	float RouteForwardOffset = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain")
	float RouteWidth = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Route", meta = (ClampMin = "0.5", ClampMax = "3.0", UIMin = "0.5", UIMax = "3.0"))
	float ElevationGainMultiplier = 1.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Route", meta = (ClampMin = "8.0", ClampMax = "32.0", UIMin = "8.0", UIMax = "32.0", Units = "deg"))
	float MaxRampPitchDegrees = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Route", meta = (ClampMin = "0.0", ClampMax = "55.0", UIMin = "0.0", UIMax = "55.0", Units = "deg"))
	float TurnStrengthDegrees = 34.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Spiral", meta = (ClampMin = "1000.0", ClampMax = "12000.0", UIMin = "1000.0", UIMax = "12000.0", Units = "cm"))
	float MountainHeight = 6200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Spiral", meta = (ClampMin = "800.0", ClampMax = "8000.0", UIMin = "800.0", UIMax = "8000.0", Units = "cm"))
	float BasePathRadius = 2600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Spiral", meta = (ClampMin = "500.0", ClampMax = "6000.0", UIMin = "500.0", UIMax = "6000.0", Units = "cm"))
	float TopPathRadius = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Spiral", meta = (ClampMin = "0.75", ClampMax = "4.0", UIMin = "0.75", UIMax = "4.0"))
	float TotalTurns = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Spiral", meta = (ClampMin = "16", ClampMax = "72", UIMin = "16", UIMax = "72"))
	int32 SegmentsPerTurn = 36;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Spiral", meta = (ClampMin = "120.0", ClampMax = "900.0", UIMin = "120.0", UIMax = "900.0", Units = "cm"))
	float RoadWidth = 430.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Spiral", meta = (ClampMin = "150.0", ClampMax = "1200.0", UIMin = "150.0", UIMax = "1200.0", Units = "cm"))
	float OuterSlopeWidth = 520.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Spiral", meta = (ClampMin = "200.0", ClampMax = "2000.0", UIMin = "200.0", UIMax = "2000.0", Units = "cm"))
	float InnerWallHeight = 760.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Variation")
	int32 GenerationSeed = 20260425;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Variation", meta = (ClampMin = "0.0", ClampMax = "0.45", UIMin = "0.0", UIMax = "0.45"))
	float RadiusVariationStrength = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Variation", meta = (ClampMin = "0.0", ClampMax = "0.25", UIMin = "0.0", UIMax = "0.25"))
	float HeightVariationStrength = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Variation", meta = (ClampMin = "0.0", ClampMax = "0.45", UIMin = "0.0", UIMax = "0.45"))
	float RoadWidthVariationStrength = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Variation", meta = (ClampMin = "0.0", ClampMax = "0.5", UIMin = "0.0", UIMax = "0.5"))
	float CoreVariationStrength = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Performance", meta = (ClampMin = "200.0", ClampMax = "5000.0", UIMin = "200.0", UIMax = "5000.0", Units = "m"))
	float VisibilityRangeMeters = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Audio")
	USoundBase* DemoMusic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Audio")
	bool bAutoPlayMusicOnGenerate = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Audio")
	bool bLoopMusic = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music Mountain|Demo")
	float FinishDistanceCm = 520.0f;

private:
	bool LoadAnalysis();
	void BuildFallbackAnalysis();
	void GenerateSpiralRoutePoints(const FVector& StartLocation, float GroundZ);
	void SpawnMountainCore(const FVector& MountainCenter, float GroundZ);
	void GenerateSection(int32 SectionIndex, FMusicMountainSection& Section);
	void SpawnPlatform(const FString& Label, const FVector& SurfaceCenter, const FVector& Size, const FLinearColor& Color, const FRotator& Rotation = FRotator::ZeroRotator);
	void SpawnSlopedBlock(const FString& Label, const FVector& StartSurface, const FVector& EndSurface, float Width, float Thickness, bool bEnableCollision, const FLinearColor& Color);
	void SpawnDecoration(const FString& Label, const FVector& Location, const FVector& Scale, const FLinearColor& Color);
	void UpdateCurrentSection(const FVector& PawnLocation);
	void RespawnPlayerAtCheckpoint();
	void ResetPlayerToStart();
	void CheckDemoHotkeys(APlayerController* PlayerController);
	void ToggleCursorMode(APlayerController* PlayerController);
	void CheckFinishCondition(const FVector& PawnLocation);
	void CompleteDemo();
	void CreateRuntimeHud();
	void RemoveRuntimeHud();
	void ShowSectionMessage(const FMusicMountainSection& Section);
	FLinearColor ResolveThemeColor(const FMusicMountainSection& Section) const;
	FLinearColor ResolveRoadColor(const FMusicMountainSection& Section) const;
	FLinearColor ResolveInnerWallColor(const FMusicMountainSection& Section) const;
	FLinearColor ResolveOuterSlopeColor(const FMusicMountainSection& Section) const;
	FLinearColor ResolveRockColor(const FMusicMountainSection& Section) const;
	float GetClampedVisibilityRangeCm() const;
	float GetSeedOffset(float Salt) const;

	UFUNCTION()
	void HandleMusicFinished();

	UPROPERTY()
	TArray<AActor*> GeneratedActors;

	UPROPERTY()
	UStaticMesh* CubeMesh = nullptr;

	UPROPERTY()
	UStaticMesh* SphereMesh = nullptr;

	UPROPERTY()
	UMaterialInterface* BaseShapeMaterial = nullptr;

	UPROPERTY()
	UAudioComponent* MusicComponent = nullptr;

	UPROPERTY()
	USoundBase* ActiveMusic = nullptr;

	TArray<FMusicMountainSection> Sections;
	TArray<FMusicMountainRoutePoint> RoutePoints;
	FString TrackName = TEXT("demo_song");
	FString DisplayName = TEXT("Demo Song - Music Mountain");
	FString AudioAssetPath;
	float Bpm = 128.0f;
	FString Theme = TEXT("cold, epic, lonely");
	int32 CurrentSectionIndex = INDEX_NONE;
	FVector MountainCenterLocation = FVector::ZeroVector;
	FVector LastCheckpointLocation = FVector::ZeroVector;
	float KillZ = -1000.0f;
	float TotalRouteDistance = 1.0f;
	float GeneratedMountainHeight = 6200.0f;
	bool bMusicPaused = false;
	bool bDemoCompleted = false;
	bool bCursorModeEnabled = false;
	bool bPreviousTabKeyDown = false;
	bool bPreviousPauseKeyDown = false;
	bool bPreviousRestartKeyDown = false;
	float DemoStartTimeSeconds = 0.0f;
	float DemoFinishTimeSeconds = 0.0f;
	float CurrentAltitudeProgress = 0.0f;
	FVector DemoStartLocation = FVector::ZeroVector;
	FVector SummitLocation = FVector::ZeroVector;
	TSharedPtr<SWidget> RuntimeHudWidget;
};
