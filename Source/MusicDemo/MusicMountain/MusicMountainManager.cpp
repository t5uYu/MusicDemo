// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicMountainManager.h"

#include "MusicMountainPCGSectionActor.h"
#include "MusicMountainPCGPreviewSettings.h"
#include "MusicMountainRuntimeHUD.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "InputCoreTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PCGCommon.h"
#include "PCGGraph.h"
#include "PCGInputOutputSettings.h"
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
	LoadClientLLMSettings();

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

	if (IsMusicPlaying())
	{
		MusicPlaybackSeconds += DeltaSeconds;
	}

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

	if (bUseLoadedAnalysisForNextGenerate)
	{
		bUseLoadedAnalysisForNextGenerate = false;
	}
	else if (!LoadAnalysis())
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
	if (!bUseLoadedAnalysisForNextGenerate)
	{
		Sections.Reset();
	}
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
	MusicPlaybackSeconds = 0.0f;
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
	MusicPlaybackSeconds = 0.0f;
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

FString AMusicMountainManager::GetCurrentSubtitleSpeaker() const
{
	if (const FMusicMountainLyricLine* LyricLine = FindCurrentLyricLine())
	{
		return LyricLine->Speaker;
	}
	return TEXT("");
}

FString AMusicMountainManager::GetCurrentSubtitleText() const
{
	const FMusicMountainLyricLine* LyricLine = FindCurrentLyricLine();
	if (!LyricLine)
	{
		return TEXT("");
	}

	const float Duration = FMath::Max(LyricLine->EndTime - LyricLine->StartTime, 0.1f);
	const float RevealAlpha = FMath::Clamp((GetMusicPlaybackSeconds() - LyricLine->StartTime) / Duration, 0.0f, 1.0f);
	const int32 CharacterCount = FMath::Clamp(FMath::CeilToInt(LyricLine->Text.Len() * RevealAlpha), 1, LyricLine->Text.Len());
	return LyricLine->Text.Left(CharacterCount);
}

FString AMusicMountainManager::GetCurrentSubtitleMood() const
{
	if (const FMusicMountainLyricLine* LyricLine = FindCurrentLyricLine())
	{
		return LyricLine->Mood;
	}
	return TEXT("");
}

bool AMusicMountainManager::HasActiveSubtitle() const
{
	return FindCurrentLyricLine() != nullptr;
}

FString AMusicMountainManager::GetClientLLMProvider() const
{
	return ClientLLMProvider;
}

FString AMusicMountainManager::GetClientLLMEndpoint() const
{
	return ClientLLMEndpoint;
}

FString AMusicMountainManager::GetClientLLMModel() const
{
	return ClientLLMModel;
}

FString AMusicMountainManager::GetClientLLMApiKey() const
{
	return ClientLLMApiKey;
}

void AMusicMountainManager::SetClientLLMProvider(const FString& Provider)
{
	ClientLLMProvider = Provider.TrimStartAndEnd().ToLower();
	if (ClientLLMProvider == TEXT("deepseek"))
	{
		ClientLLMEndpoint = TEXT("https://api.deepseek.com/chat/completions");
		ClientLLMModel = TEXT("deepseek-chat");
	}
	else if (ClientLLMProvider == TEXT("gpt"))
	{
		ClientLLMEndpoint = TEXT("https://api.openai.com/v1/chat/completions");
		ClientLLMModel = TEXT("gpt-4o-mini");
	}
	else if (ClientLLMProvider == TEXT("gemini"))
	{
		ClientLLMEndpoint = TEXT("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions");
		ClientLLMModel = TEXT("gemini-2.0-flash");
	}
	else if (ClientLLMProvider == TEXT("claude"))
	{
		ClientLLMEndpoint = TEXT("https://api.anthropic.com/v1/messages");
		ClientLLMModel = TEXT("claude-3-5-haiku-latest");
	}
}

void AMusicMountainManager::SetClientLLMSettings(const FString& Provider, const FString& Endpoint, const FString& Model, const FString& ApiKey)
{
	ClientLLMProvider = Provider.TrimStartAndEnd().ToLower();
	ClientLLMEndpoint = Endpoint.TrimStartAndEnd();
	ClientLLMModel = Model.TrimStartAndEnd();
	ClientLLMApiKey = ApiKey.TrimStartAndEnd();
}

bool AMusicMountainManager::SaveClientLLMSettings() const
{
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("provider"), ClientLLMProvider);
	RootObject->SetStringField(TEXT("endpoint"), ClientLLMEndpoint);
	RootObject->SetStringField(TEXT("model"), ClientLLMModel);
	RootObject->SetStringField(TEXT("api_key"), ClientLLMApiKey);
	RootObject->SetStringField(TEXT("warning"), TEXT("BYOK demo setting. The API key is stored locally in plain text."));

	FString OutputText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputText);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		return false;
	}

	const FString SettingsPath = GetClientLLMSettingsPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SettingsPath), true);
	return FFileHelper::SaveStringToFile(OutputText + LINE_TERMINATOR, *SettingsPath);
}

void AMusicMountainManager::RequestClientLLMDirector()
{
	if (bClientLLMRequestInFlight)
	{
		ClientLLMStatusText = TEXT("Client LLM request is already running.");
		return;
	}
	if (ClientLLMEndpoint.IsEmpty() || ClientLLMModel.IsEmpty() || ClientLLMApiKey.IsEmpty())
	{
		ClientLLMStatusText = TEXT("Client LLM settings are incomplete. Fill Endpoint, Model, and API Key first.");
		return;
	}
	if (Sections.Num() == 0)
	{
		ClientLLMStatusText = TEXT("No music analysis is loaded yet.");
		return;
	}

	const FString Prompt = BuildClientLLMDirectorPrompt();
	TSharedRef<FJsonObject> RequestRoot = MakeShared<FJsonObject>();
	RequestRoot->SetStringField(TEXT("model"), ClientLLMModel);
	RequestRoot->SetNumberField(TEXT("temperature"), 0.2);

	if (ClientLLMProvider == TEXT("claude"))
	{
		RequestRoot->SetNumberField(TEXT("max_tokens"), 2048);
		RequestRoot->SetStringField(TEXT("system"), TEXT("You are the Music Mountain LLM Director. Return one valid JSON object only."));

		TArray<TSharedPtr<FJsonValue>> Messages;
		TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
		UserMessage->SetStringField(TEXT("role"), TEXT("user"));
		UserMessage->SetStringField(TEXT("content"), Prompt);
		Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
		RequestRoot->SetArrayField(TEXT("messages"), Messages);
	}
	else
	{
		TArray<TSharedPtr<FJsonValue>> Messages;
		TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
		SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
		SystemMessage->SetStringField(TEXT("content"), TEXT("You are the Music Mountain LLM Director. Return one valid JSON object only."));
		Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));

		TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
		UserMessage->SetStringField(TEXT("role"), TEXT("user"));
		UserMessage->SetStringField(TEXT("content"), Prompt);
		Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
		RequestRoot->SetArrayField(TEXT("messages"), Messages);
	}

	FString RequestBody;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	if (!FJsonSerializer::Serialize(RequestRoot, Writer))
	{
		ClientLLMStatusText = TEXT("Failed to serialize client LLM request.");
		return;
	}

	bClientLLMRequestInFlight = true;
	ClientLLMStatusText = TEXT("Client LLM request running...");

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ClientLLMEndpoint);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (ClientLLMProvider == TEXT("claude"))
	{
		Request->SetHeader(TEXT("x-api-key"), ClientLLMApiKey);
		Request->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
	}
	else
	{
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ClientLLMApiKey));
	}
	Request->SetContentAsString(RequestBody);
	Request->OnProcessRequestComplete().BindLambda(
		[this](FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			bClientLLMRequestInFlight = false;
			if (!bWasSuccessful || !Response.IsValid())
			{
				ClientLLMStatusText = TEXT("Client LLM request failed: no response.");
				return;
			}

			const int32 ResponseCode = Response->GetResponseCode();
			const FString ResponseText = Response->GetContentAsString();
			if (ResponseCode < 200 || ResponseCode >= 300)
			{
				ClientLLMStatusText = FString::Printf(TEXT("Client LLM HTTP %d: %s"), ResponseCode, *ResponseText.Left(512));
				return;
			}

			if (!ApplyClientLLMDirectorJson(ResponseText))
			{
				ClientLLMStatusText = TEXT("Client LLM returned JSON, but Director plan could not be applied.");
				return;
			}

			ClientLLMStatusText = TEXT("Client LLM Director applied. Mountain regenerated.");
			bUseLoadedAnalysisForNextGenerate = true;
			GenerateDemoMountain();
		});
	Request->ProcessRequest();
}

FString AMusicMountainManager::GetClientLLMStatusText() const
{
	return ClientLLMStatusText;
}

FString AMusicMountainManager::GetLyricsLookupStatusText() const
{
	FString Status = FString::Printf(TEXT("Lyrics: %s | lines: %d"), *LyricsSourceStatus, Lyrics.Num());
	if (!LyricsSourceName.IsEmpty())
	{
		Status += FString::Printf(TEXT(" | source: %s"), *LyricsSourceName);
	}
	if (!LyricsSourceType.IsEmpty())
	{
		Status += FString::Printf(TEXT(" | type: %s"), *LyricsSourceType);
	}
	if (!LyricsSourceQuery.IsEmpty())
	{
		Status += FString::Printf(TEXT(" | query: %s"), *LyricsSourceQuery);
	}
	return Status;
}

bool AMusicMountainManager::IsClientLLMRequestInFlight() const
{
	return bClientLLMRequestInFlight;
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

	LyricsSourceStatus = TEXT("missing");
	LyricsSourceName = TEXT("");
	LyricsSourceType = TEXT("");
	LyricsSourceQuery = TEXT("");
	const TSharedPtr<FJsonObject>* LyricsSourceObject = nullptr;
	if (RootObject->TryGetObjectField(TEXT("lyrics_source"), LyricsSourceObject) && LyricsSourceObject && LyricsSourceObject->IsValid())
	{
		const TSharedPtr<FJsonObject>& SourceObject = *LyricsSourceObject;
		LyricsSourceStatus = SourceObject->HasTypedField<EJson::String>(TEXT("status")) ? SourceObject->GetStringField(TEXT("status")) : LyricsSourceStatus;
		LyricsSourceName = SourceObject->HasTypedField<EJson::String>(TEXT("source")) ? SourceObject->GetStringField(TEXT("source")) : TEXT("");
		LyricsSourceType = SourceObject->HasTypedField<EJson::String>(TEXT("source_type")) ? SourceObject->GetStringField(TEXT("source_type")) : TEXT("");

		const TSharedPtr<FJsonObject>* QueryObject = nullptr;
		if (SourceObject->TryGetObjectField(TEXT("query"), QueryObject) && QueryObject && QueryObject->IsValid())
		{
			FString QueryJson;
			const TSharedRef<TJsonWriter<>> QueryWriter = TJsonWriterFactory<>::Create(&QueryJson);
			FJsonSerializer::Serialize(QueryObject->ToSharedRef(), QueryWriter);
			LyricsSourceQuery = QueryJson;
		}
	}

	Lyrics.Reset();
	const TArray<TSharedPtr<FJsonValue>>* LyricValues = nullptr;
	if (RootObject->TryGetArrayField(TEXT("lyrics"), LyricValues) && LyricValues)
	{
		for (const TSharedPtr<FJsonValue>& LyricValue : *LyricValues)
		{
			const TSharedPtr<FJsonObject> LyricObject = LyricValue->AsObject();
			if (!LyricObject.IsValid())
			{
				continue;
			}

			FMusicMountainLyricLine LyricLine;
			LyricLine.StartTime = LyricObject->HasTypedField<EJson::Number>(TEXT("start")) ? static_cast<float>(LyricObject->GetNumberField(TEXT("start"))) : 0.0f;
			LyricLine.EndTime = LyricObject->HasTypedField<EJson::Number>(TEXT("end")) ? static_cast<float>(LyricObject->GetNumberField(TEXT("end"))) : LyricLine.StartTime + 3.0f;
			LyricLine.Speaker = LyricObject->HasTypedField<EJson::String>(TEXT("speaker")) ? LyricObject->GetStringField(TEXT("speaker")) : TEXT("Song");
			LyricLine.Text = LyricObject->HasTypedField<EJson::String>(TEXT("text")) ? LyricObject->GetStringField(TEXT("text")) : TEXT("");
			LyricLine.Mood = LyricObject->HasTypedField<EJson::String>(TEXT("mood")) ? LyricObject->GetStringField(TEXT("mood")) : TEXT("");
			if (!LyricLine.Text.IsEmpty())
			{
				Lyrics.Add(LyricLine);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Music Mountain %s"), *GetLyricsLookupStatusText());

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
	Lyrics.Reset();
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

	Lyrics.Add({ 0.0f, 4.0f, TEXT("测试字幕"), TEXT("测试歌词 1：字幕系统正在显示。"), TEXT("calm") });
	Lyrics.Add({ 30.0f, 34.0f, TEXT("测试字幕"), TEXT("测试歌词 2：客户端 LLM 流程后仍应显示。"), TEXT("dark") });
	Lyrics.Add({ 60.0f, 64.0f, TEXT("测试字幕"), TEXT("测试歌词 3：米塔风格字幕效果检查。"), TEXT("epic") });
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

	SpawnPCGSectionVolume(SectionIndex, Section, PointIndices, SectionRoadWidth, SectionOuterSlopeWidth);
}

void AMusicMountainManager::SpawnPCGSectionVolume(int32 SectionIndex, const FMusicMountainSection& Section, const TArray<int32>& PointIndices, float SectionRoadWidth, float SectionOuterSlopeWidth)
{
	UPCGGraphInterface* GraphToUse = GetOrCreatePCGGraph();
	if (!bEnablePCGSectionVolumes || !GraphToUse || !GetWorld() || PointIndices.Num() == 0)
	{
		return;
	}

	FBox SectionBounds(ForceInit);
	const float HorizontalPadding = FMath::Max(PCGSectionBoundsPadding, 0.0f) + SectionRoadWidth + SectionOuterSlopeWidth;
	const float VerticalExtent = FMath::Max(PCGSectionBoundsHeight * 0.5f, 50.0f);
	for (const int32 PointIndex : PointIndices)
	{
		if (!RoutePoints.IsValidIndex(PointIndex))
		{
			continue;
		}

		const FMusicMountainRoutePoint& RoutePoint = RoutePoints[PointIndex];
		SectionBounds += RoutePoint.Location + RoutePoint.Inward * HorizontalPadding + FVector(0.0f, 0.0f, VerticalExtent);
		SectionBounds += RoutePoint.Location + RoutePoint.Outward * HorizontalPadding - FVector(0.0f, 0.0f, VerticalExtent);
	}

	if (!SectionBounds.IsValid)
	{
		return;
	}

	TArray<FVector> RouteLocations;
	TArray<FVector> InnerScatterLocations;
	TArray<FVector> OuterScatterLocations;
	RouteLocations.Reserve(PointIndices.Num());
	InnerScatterLocations.Reserve(PointIndices.Num());
	OuterScatterLocations.Reserve(PointIndices.Num());

	const float RoadExclusionHalfWidth = SectionRoadWidth * 0.62f;
	const float ScatterWidth = FMath::Max(SectionOuterSlopeWidth, 150.0f);
	const float InnerScatterOffset = RoadExclusionHalfWidth + ScatterWidth * 0.35f;
	const float OuterScatterOffset = RoadExclusionHalfWidth + ScatterWidth * 0.65f;
	for (const int32 PointIndex : PointIndices)
	{
		if (RoutePoints.IsValidIndex(PointIndex))
		{
			const FMusicMountainRoutePoint& RoutePoint = RoutePoints[PointIndex];
			RouteLocations.Add(RoutePoint.Location);
			InnerScatterLocations.Add(RoutePoint.Location + RoutePoint.Inward * InnerScatterOffset);
			OuterScatterLocations.Add(RoutePoint.Location + RoutePoint.Outward * OuterScatterOffset);
		}
	}

	AMusicMountainPCGSectionActor* PCGActor = GetWorld()->SpawnActor<AMusicMountainPCGSectionActor>(AMusicMountainPCGSectionActor::StaticClass(), SectionBounds.GetCenter(), FRotator::ZeroRotator);
	if (!PCGActor)
	{
		return;
	}

	PCGActor->ConfigureSection(
		SectionIndex,
		Section.Name,
		Section.Mood,
		Section.Terrain,
		Section.AudioStyle,
		Section.Energy,
		Section.RouteStartDistance,
		Section.RouteEndDistance,
		Section.RouteRadius,
		Section.ThemeColor,
		SectionBounds,
		RouteLocations,
		InnerScatterLocations,
		OuterScatterLocations,
		RoadExclusionHalfWidth,
		ScatterWidth,
		GraphToUse,
		GenerationSeed + SectionIndex * 1009);

	GeneratedActors.Add(PCGActor);
}

UPCGGraphInterface* AMusicMountainManager::GetOrCreatePCGGraph()
{
	if (SectionPCGGraph)
	{
		return SectionPCGGraph;
	}

	if (!bGeneratePCGPreviewDecorations)
	{
		return nullptr;
	}

	if (RuntimePreviewPCGGraph)
	{
		return RuntimePreviewPCGGraph;
	}

	RuntimePreviewPCGGraph = NewObject<UPCGGraph>(this, TEXT("MusicMountainRuntimePreviewPCGGraph"), RF_Transient);
	if (!RuntimePreviewPCGGraph)
	{
		return nullptr;
	}

	UPCGSettings* PreviewSettings = nullptr;
	UPCGNode* PreviewNode = RuntimePreviewPCGGraph->AddNodeOfType(UPCGMusicMountainPreviewSettings::StaticClass(), PreviewSettings);
	if (UPCGMusicMountainPreviewSettings* MusicPreviewSettings = Cast<UPCGMusicMountainPreviewSettings>(PreviewSettings))
	{
		MusicPreviewSettings->DensityMultiplier = PCGPreviewDensityMultiplier;
	}

	if (PreviewNode)
	{
		RuntimePreviewPCGGraph->AddEdge(RuntimePreviewPCGGraph->GetInputNode(), PCGInputOutputConstants::DefaultInputLabel, PreviewNode, PCGPinConstants::DefaultInputLabel);
		RuntimePreviewPCGGraph->AddEdge(PreviewNode, PCGPinConstants::DefaultOutputLabel, RuntimePreviewPCGGraph->GetOutputNode(), PCGInputOutputConstants::DefaultInputLabel);
#if WITH_EDITOR
		RuntimePreviewPCGGraph->ForceNotificationForEditor(EPCGChangeType::Structural);
#endif
	}

	return RuntimePreviewPCGGraph;
}

void AMusicMountainManager::SpawnPCGPreviewDecorations(int32 SectionIndex, const FMusicMountainSection& Section, const TArray<int32>& PointIndices, float SectionRoadWidth, float SectionOuterSlopeWidth)
{
	if (!bGeneratePCGPreviewDecorations || SectionPCGGraph || !GetWorld() || PointIndices.Num() == 0 || !SphereMesh || !CubeMesh)
	{
		return;
	}

	const FString MoodLower = Section.Mood.ToLower();
	const FString TerrainLower = Section.Terrain.ToLower();
	const float FoliageDensity = FMath::Clamp(FMath::Lerp(0.25f, 0.9f, 1.0f - Section.Energy) + ((MoodLower.Contains(TEXT("romantic")) || MoodLower.Contains(TEXT("sweet")) || TerrainLower.Contains(TEXT("flower")) || TerrainLower.Contains(TEXT("meadow"))) ? 0.45f : 0.0f), 0.0f, 1.25f);
	const float RockDensity = FMath::Clamp(FMath::Lerp(0.35f, 1.0f, Section.Energy) + ((MoodLower.Contains(TEXT("dark")) || MoodLower.Contains(TEXT("tense")) || TerrainLower.Contains(TEXT("cliff")) || TerrainLower.Contains(TEXT("cave"))) ? 0.35f : 0.0f), 0.0f, 1.25f);
	const float LightDensity = FMath::Clamp(FMath::Lerp(0.18f, 0.65f, Section.Energy) + ((MoodLower.Contains(TEXT("epic")) || TerrainLower.Contains(TEXT("summit")) || TerrainLower.Contains(TEXT("ridge"))) ? 0.25f : 0.0f), 0.0f, 1.0f);
	const float DensityScale = FMath::Clamp(PCGPreviewDensityMultiplier, 0.2f, 3.0f);
	const int32 Stride = FMath::Max(2, FMath::RoundToInt(FMath::Lerp(7.0f, 3.0f, Section.Energy) / DensityScale));
	const float RoadExclusionHalfWidth = SectionRoadWidth * 0.62f;
	const float ScatterWidth = FMath::Max(SectionOuterSlopeWidth, 150.0f);
	const FLinearColor FoliageColor = (MoodLower.Contains(TEXT("romantic")) || MoodLower.Contains(TEXT("sweet")))
		? FLinearColor(0.95f, 0.35f, 0.62f)
		: FLinearColor(0.18f, 0.46f, 0.22f);
	const FLinearColor RockColor = ResolveRockColor(Section);
	const FLinearColor LightColor = FLinearColor::LerpUsingHSV(Section.ThemeColor, FLinearColor(1.0f, 0.86f, 0.38f), 0.45f);
	FRandomStream PreviewRandom(GenerationSeed + SectionIndex * 1709 + 401);

	for (int32 LocalIndex = 0; LocalIndex < PointIndices.Num(); LocalIndex += Stride)
	{
		const int32 PointIndex = PointIndices[LocalIndex];
		if (!RoutePoints.IsValidIndex(PointIndex))
		{
			continue;
		}

		const FMusicMountainRoutePoint& RoutePoint = RoutePoints[PointIndex];
		const bool bUseOuterSide = PreviewRandom.FRand() > 0.35f;
		const FVector SideDirection = bUseOuterSide ? RoutePoint.Outward : RoutePoint.Inward;
		const float SideOffset = RoadExclusionHalfWidth + PreviewRandom.FRandRange(ScatterWidth * 0.25f, ScatterWidth * 0.95f);
		const FVector BaseLocation = RoutePoint.Location
			+ SideDirection * SideOffset
			+ RoutePoint.Tangent * PreviewRandom.FRandRange(-90.0f, 90.0f);

		if (PreviewRandom.FRand() < FoliageDensity)
		{
			const bool bFlowerPatch = MoodLower.Contains(TEXT("romantic")) || MoodLower.Contains(TEXT("sweet")) || TerrainLower.Contains(TEXT("flower")) || TerrainLower.Contains(TEXT("meadow"));
			if (bFlowerPatch)
			{
				const int32 FlowerCount = PreviewRandom.RandRange(3, 6);
				for (int32 FlowerIndex = 0; FlowerIndex < FlowerCount; ++FlowerIndex)
				{
					const FVector FlowerLocation = BaseLocation + FVector(PreviewRandom.FRandRange(-90.0f, 90.0f), PreviewRandom.FRandRange(-90.0f, 90.0f), 34.0f);
					SpawnPreviewDecoration(
						FString::Printf(TEXT("%s PCG Preview Flower %d-%d"), *Section.Name, LocalIndex, FlowerIndex),
						SphereMesh,
						FlowerLocation,
						FVector(0.13f, 0.13f, 0.08f),
						FoliageColor);
				}
			}
			else
			{
				SpawnPreviewDecoration(
					FString::Printf(TEXT("%s PCG Preview Tree Trunk %d"), *Section.Name, LocalIndex),
					CubeMesh,
					BaseLocation + FVector(0.0f, 0.0f, 80.0f),
					FVector(0.16f, 0.16f, 1.25f),
					FLinearColor(0.23f, 0.13f, 0.07f),
					FRotator(0.0f, PreviewRandom.FRandRange(-35.0f, 35.0f), 0.0f));
				SpawnPreviewDecoration(
					FString::Printf(TEXT("%s PCG Preview Tree Crown %d"), *Section.Name, LocalIndex),
					SphereMesh,
					BaseLocation + FVector(0.0f, 0.0f, 190.0f),
					FVector(0.75f, 0.75f, 0.62f) * PreviewRandom.FRandRange(0.75f, 1.25f),
					FoliageColor);
			}
		}

		if (PreviewRandom.FRand() < RockDensity)
		{
			SpawnPreviewDecoration(
				FString::Printf(TEXT("%s PCG Preview Rock %d"), *Section.Name, LocalIndex),
				SphereMesh,
				BaseLocation + SideDirection * PreviewRandom.FRandRange(90.0f, 220.0f) + FVector(0.0f, 0.0f, 60.0f),
				FVector(0.45f, 0.62f, 0.38f) * PreviewRandom.FRandRange(0.75f, 1.45f),
				RockColor);
		}

		if (PreviewRandom.FRand() < LightDensity)
		{
			const FVector LightBaseLocation = RoutePoint.Location + SideDirection * (RoadExclusionHalfWidth + 85.0f) + FVector(0.0f, 0.0f, 80.0f);
			SpawnPreviewDecoration(
				FString::Printf(TEXT("%s PCG Preview Light Pole %d"), *Section.Name, LocalIndex),
				CubeMesh,
				LightBaseLocation,
				FVector(0.1f, 0.1f, 1.2f),
				FLinearColor(0.08f, 0.07f, 0.1f));
			SpawnPreviewDecoration(
				FString::Printf(TEXT("%s PCG Preview Light Orb %d"), *Section.Name, LocalIndex),
				SphereMesh,
				LightBaseLocation + FVector(0.0f, 0.0f, 95.0f),
				FVector(0.22f, 0.22f, 0.22f),
				LightColor);
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
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetStaticMesh(CubeMesh);
	MeshComponent->SetWorldScale3D(Size / 100.0f);
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
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetStaticMesh(CubeMesh);
	MeshComponent->SetWorldScale3D(FVector(Hypotenuse / 100.0f, Width / 100.0f, Thickness / 100.0f));
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
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetStaticMesh(MeshToUse);
	MeshComponent->SetWorldScale3D(Scale);
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

void AMusicMountainManager::SpawnPreviewDecoration(const FString& Label, UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, const FLinearColor& Color, const FRotator& Rotation)
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

void AMusicMountainManager::LoadClientLLMSettings()
{
	const FString SettingsPath = GetClientLLMSettingsPath();
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *SettingsPath))
	{
		return;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Music Mountain could not parse client LLM settings: %s"), *SettingsPath);
		return;
	}

	ClientLLMEndpoint = RootObject->HasTypedField<EJson::String>(TEXT("endpoint"))
		? RootObject->GetStringField(TEXT("endpoint"))
		: ClientLLMEndpoint;
	ClientLLMProvider = RootObject->HasTypedField<EJson::String>(TEXT("provider"))
		? RootObject->GetStringField(TEXT("provider")).ToLower()
		: ClientLLMProvider;
	ClientLLMModel = RootObject->HasTypedField<EJson::String>(TEXT("model"))
		? RootObject->GetStringField(TEXT("model"))
		: ClientLLMModel;
	ClientLLMApiKey = RootObject->HasTypedField<EJson::String>(TEXT("api_key"))
		? RootObject->GetStringField(TEXT("api_key"))
		: ClientLLMApiKey;
}

FString AMusicMountainManager::GetClientLLMSettingsPath() const
{
	return FPaths::ProjectSavedDir() / TEXT("MusicMountainClientLLMSettings.json");
}

FString AMusicMountainManager::BuildClientLLMDirectorPrompt() const
{
	TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetStringField(TEXT("track"), TrackName);
	Summary->SetStringField(TEXT("display_name"), DisplayName);
	Summary->SetNumberField(TEXT("bpm"), Bpm);
	Summary->SetStringField(TEXT("theme"), Theme);
	Summary->SetStringField(TEXT("lyrics_source_status"), LyricsSourceStatus);
	Summary->SetStringField(TEXT("lyrics_source"), LyricsSourceName);
	Summary->SetStringField(TEXT("lyrics_source_type"), LyricsSourceType);
	Summary->SetNumberField(TEXT("lyrics_line_count"), Lyrics.Num());

	TSharedRef<FJsonObject> MountainPlan = MakeShared<FJsonObject>();
	MountainPlan->SetNumberField(TEXT("generation_seed"), GenerationSeed);
	MountainPlan->SetNumberField(TEXT("mountain_height"), MountainHeight);
	MountainPlan->SetNumberField(TEXT("base_path_radius"), BasePathRadius);
	MountainPlan->SetNumberField(TEXT("top_path_radius"), TopPathRadius);
	MountainPlan->SetNumberField(TEXT("total_turns"), TotalTurns);
	MountainPlan->SetNumberField(TEXT("segments_per_turn"), SegmentsPerTurn);
	MountainPlan->SetNumberField(TEXT("road_width"), RoadWidth);
	MountainPlan->SetNumberField(TEXT("outer_slope_width"), OuterSlopeWidth);
	MountainPlan->SetNumberField(TEXT("inner_wall_height"), InnerWallHeight);
	MountainPlan->SetNumberField(TEXT("elevation_gain_multiplier"), ElevationGainMultiplier);
	MountainPlan->SetNumberField(TEXT("max_ramp_pitch_degrees"), MaxRampPitchDegrees);
	MountainPlan->SetNumberField(TEXT("visibility_range_meters"), VisibilityRangeMeters);

	TSharedRef<FJsonObject> Variation = MakeShared<FJsonObject>();
	Variation->SetNumberField(TEXT("radius"), RadiusVariationStrength);
	Variation->SetNumberField(TEXT("height"), HeightVariationStrength);
	Variation->SetNumberField(TEXT("road_width"), RoadWidthVariationStrength);
	Variation->SetNumberField(TEXT("core"), CoreVariationStrength);
	MountainPlan->SetObjectField(TEXT("variation"), Variation);
	Summary->SetObjectField(TEXT("mountain_plan"), MountainPlan);

	TArray<TSharedPtr<FJsonValue>> SectionValues;
	for (const FMusicMountainSection& Section : Sections)
	{
		TSharedRef<FJsonObject> SectionObject = MakeShared<FJsonObject>();
		SectionObject->SetStringField(TEXT("name"), Section.Name);
		SectionObject->SetNumberField(TEXT("start"), Section.StartTime);
		SectionObject->SetNumberField(TEXT("end"), Section.EndTime);
		SectionObject->SetStringField(TEXT("mood"), Section.Mood);
		SectionObject->SetNumberField(TEXT("energy"), Section.Energy);
		SectionObject->SetStringField(TEXT("terrain"), Section.Terrain);
		SectionObject->SetStringField(TEXT("audio_style"), Section.AudioStyle);
		SectionValues.Add(MakeShared<FJsonValueObject>(SectionObject));
	}
	Summary->SetArrayField(TEXT("sections"), SectionValues);

	TArray<TSharedPtr<FJsonValue>> LyricValues;
	const int32 MaxLyricSamples = FMath::Min(Lyrics.Num(), 12);
	for (int32 Index = 0; Index < MaxLyricSamples; ++Index)
	{
		const FMusicMountainLyricLine& LyricLine = Lyrics[Index];
		TSharedRef<FJsonObject> LyricObject = MakeShared<FJsonObject>();
		LyricObject->SetNumberField(TEXT("start"), LyricLine.StartTime);
		LyricObject->SetNumberField(TEXT("end"), LyricLine.EndTime);
		LyricObject->SetStringField(TEXT("text"), LyricLine.Text);
		LyricValues.Add(MakeShared<FJsonValueObject>(LyricObject));
	}
	Summary->SetArrayField(TEXT("lyrics_sample"), LyricValues);

	FString SummaryText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SummaryText);
	FJsonSerializer::Serialize(Summary, Writer);

	return FString::Printf(
		TEXT("You are the Music Mountain LLM Director.\n")
		TEXT("Return one valid JSON object only, no markdown.\n")
		TEXT("Use the audio summary and available lyrics_sample to design a playable spiral mountain journey.\n")
		TEXT("Preserve section count and names. Keep values playable.\n")
		TEXT("Output fields: overall_theme, journey_arc, theme, mountain_plan, sections.\n")
		TEXT("Each section may override mood, energy, terrain, audio_style, visual_motif, gameplay_intent.\n\n")
		TEXT("Audio summary:\n%s"),
		*SummaryText);
}

bool AMusicMountainManager::ApplyClientLLMDirectorJson(const FString& JsonText)
{
	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ContentValues = nullptr;
		if (RootObject->TryGetArrayField(TEXT("content"), ContentValues) && ContentValues && ContentValues->Num() > 0)
		{
			const TSharedPtr<FJsonObject> ContentObject = (*ContentValues)[0]->AsObject();
			if (ContentObject.IsValid() && ContentObject->HasTypedField<EJson::String>(TEXT("text")))
			{
				FString DirectorJsonText;
				if (!ExtractJsonObjectString(ContentObject->GetStringField(TEXT("text")), DirectorJsonText))
				{
					return false;
				}
				TSharedPtr<FJsonObject> DirectorObject;
				const TSharedRef<TJsonReader<>> DirectorReader = TJsonReaderFactory<>::Create(DirectorJsonText);
				return FJsonSerializer::Deserialize(DirectorReader, DirectorObject) && ApplyDirectorObject(DirectorObject);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
		if (RootObject->TryGetArrayField(TEXT("choices"), Choices) && Choices && Choices->Num() > 0)
		{
			const TSharedPtr<FJsonObject> ChoiceObject = (*Choices)[0]->AsObject();
			const TSharedPtr<FJsonObject>* MessageObject = nullptr;
			if (ChoiceObject.IsValid() && ChoiceObject->TryGetObjectField(TEXT("message"), MessageObject) && MessageObject && MessageObject->IsValid())
			{
				const FString Content = (*MessageObject)->GetStringField(TEXT("content"));
				FString DirectorJsonText;
				if (!ExtractJsonObjectString(Content, DirectorJsonText))
				{
					return false;
				}
				TSharedPtr<FJsonObject> DirectorObject;
				const TSharedRef<TJsonReader<>> DirectorReader = TJsonReaderFactory<>::Create(DirectorJsonText);
				return FJsonSerializer::Deserialize(DirectorReader, DirectorObject) && ApplyDirectorObject(DirectorObject);
			}
		}

		return ApplyDirectorObject(RootObject);
	}

	FString ExtractedJsonText;
	if (!ExtractJsonObjectString(JsonText, ExtractedJsonText))
	{
		return false;
	}
	TSharedPtr<FJsonObject> DirectorObject;
	const TSharedRef<TJsonReader<>> ExtractedReader = TJsonReaderFactory<>::Create(ExtractedJsonText);
	return FJsonSerializer::Deserialize(ExtractedReader, DirectorObject) && ApplyDirectorObject(DirectorObject);
}

bool AMusicMountainManager::ApplyDirectorObject(const TSharedPtr<FJsonObject>& DirectorObject)
{
	if (!DirectorObject.IsValid())
	{
		return false;
	}

	if (DirectorObject->HasTypedField<EJson::String>(TEXT("theme")))
	{
		Theme = DirectorObject->GetStringField(TEXT("theme"));
	}
	else if (DirectorObject->HasTypedField<EJson::String>(TEXT("overall_theme")))
	{
		Theme = DirectorObject->GetStringField(TEXT("overall_theme"));
	}

	const TSharedPtr<FJsonObject>* MountainPlan = nullptr;
	if (DirectorObject->TryGetObjectField(TEXT("mountain_plan"), MountainPlan) && MountainPlan && MountainPlan->IsValid())
	{
		ApplyDirectorMountainPlan(*MountainPlan);
	}

	const TArray<TSharedPtr<FJsonValue>>* DirectorSections = nullptr;
	if (DirectorObject->TryGetArrayField(TEXT("sections"), DirectorSections) && DirectorSections)
	{
		for (int32 Index = 0; Index < DirectorSections->Num() && Sections.IsValidIndex(Index); ++Index)
		{
			const TSharedPtr<FJsonObject> SectionObject = (*DirectorSections)[Index]->AsObject();
			if (!SectionObject.IsValid())
			{
				continue;
			}
			FMusicMountainSection& Section = Sections[Index];
			Section.Mood = SectionObject->HasTypedField<EJson::String>(TEXT("mood")) ? SectionObject->GetStringField(TEXT("mood")) : Section.Mood;
			Section.Energy = SectionObject->HasTypedField<EJson::Number>(TEXT("energy")) ? FMath::Clamp(static_cast<float>(SectionObject->GetNumberField(TEXT("energy"))), 0.0f, 1.0f) : Section.Energy;
			Section.Terrain = SectionObject->HasTypedField<EJson::String>(TEXT("terrain")) ? SectionObject->GetStringField(TEXT("terrain")) : Section.Terrain;
			Section.AudioStyle = SectionObject->HasTypedField<EJson::String>(TEXT("audio_style")) ? SectionObject->GetStringField(TEXT("audio_style")) : Section.AudioStyle;
			Section.ThemeColor = ResolveThemeColor(Section);
		}
	}

	return true;
}

void AMusicMountainManager::ApplyDirectorMountainPlan(const TSharedPtr<FJsonObject>& MountainPlan)
{
	if (!MountainPlan.IsValid())
	{
		return;
	}

	GenerationSeed = MountainPlan->HasTypedField<EJson::Number>(TEXT("generation_seed")) ? MountainPlan->GetIntegerField(TEXT("generation_seed")) : GenerationSeed;
	MountainHeight = MountainPlan->HasTypedField<EJson::Number>(TEXT("mountain_height")) ? static_cast<float>(MountainPlan->GetNumberField(TEXT("mountain_height"))) : MountainHeight;
	BasePathRadius = MountainPlan->HasTypedField<EJson::Number>(TEXT("base_path_radius")) ? static_cast<float>(MountainPlan->GetNumberField(TEXT("base_path_radius"))) : BasePathRadius;
	TopPathRadius = MountainPlan->HasTypedField<EJson::Number>(TEXT("top_path_radius")) ? static_cast<float>(MountainPlan->GetNumberField(TEXT("top_path_radius"))) : TopPathRadius;
	TotalTurns = MountainPlan->HasTypedField<EJson::Number>(TEXT("total_turns")) ? static_cast<float>(MountainPlan->GetNumberField(TEXT("total_turns"))) : TotalTurns;
	SegmentsPerTurn = MountainPlan->HasTypedField<EJson::Number>(TEXT("segments_per_turn")) ? MountainPlan->GetIntegerField(TEXT("segments_per_turn")) : SegmentsPerTurn;
	RoadWidth = MountainPlan->HasTypedField<EJson::Number>(TEXT("road_width")) ? static_cast<float>(MountainPlan->GetNumberField(TEXT("road_width"))) : RoadWidth;
	OuterSlopeWidth = MountainPlan->HasTypedField<EJson::Number>(TEXT("outer_slope_width")) ? static_cast<float>(MountainPlan->GetNumberField(TEXT("outer_slope_width"))) : OuterSlopeWidth;
	InnerWallHeight = MountainPlan->HasTypedField<EJson::Number>(TEXT("inner_wall_height")) ? static_cast<float>(MountainPlan->GetNumberField(TEXT("inner_wall_height"))) : InnerWallHeight;
	ElevationGainMultiplier = MountainPlan->HasTypedField<EJson::Number>(TEXT("elevation_gain_multiplier")) ? static_cast<float>(MountainPlan->GetNumberField(TEXT("elevation_gain_multiplier"))) : ElevationGainMultiplier;
	MaxRampPitchDegrees = MountainPlan->HasTypedField<EJson::Number>(TEXT("max_ramp_pitch_degrees")) ? static_cast<float>(MountainPlan->GetNumberField(TEXT("max_ramp_pitch_degrees"))) : MaxRampPitchDegrees;
	VisibilityRangeMeters = MountainPlan->HasTypedField<EJson::Number>(TEXT("visibility_range_meters")) ? static_cast<float>(MountainPlan->GetNumberField(TEXT("visibility_range_meters"))) : VisibilityRangeMeters;

	const TSharedPtr<FJsonObject>* Variation = nullptr;
	if (MountainPlan->TryGetObjectField(TEXT("variation"), Variation) && Variation && Variation->IsValid())
	{
		RadiusVariationStrength = (*Variation)->HasTypedField<EJson::Number>(TEXT("radius")) ? static_cast<float>((*Variation)->GetNumberField(TEXT("radius"))) : RadiusVariationStrength;
		HeightVariationStrength = (*Variation)->HasTypedField<EJson::Number>(TEXT("height")) ? static_cast<float>((*Variation)->GetNumberField(TEXT("height"))) : HeightVariationStrength;
		RoadWidthVariationStrength = (*Variation)->HasTypedField<EJson::Number>(TEXT("road_width")) ? static_cast<float>((*Variation)->GetNumberField(TEXT("road_width"))) : RoadWidthVariationStrength;
		CoreVariationStrength = (*Variation)->HasTypedField<EJson::Number>(TEXT("core")) ? static_cast<float>((*Variation)->GetNumberField(TEXT("core"))) : CoreVariationStrength;
	}
}

bool AMusicMountainManager::ExtractJsonObjectString(const FString& RawText, FString& OutJsonText)
{
	int32 StartIndex = INDEX_NONE;
	int32 EndIndex = INDEX_NONE;
	if (RawText.FindChar(TEXT('{'), StartIndex) && RawText.FindLastChar(TEXT('}'), EndIndex) && EndIndex > StartIndex)
	{
		OutJsonText = RawText.Mid(StartIndex, EndIndex - StartIndex + 1);
		return true;
	}
	return false;
}

const FMusicMountainLyricLine* AMusicMountainManager::FindCurrentLyricLine() const
{
	const float PlaybackSeconds = GetMusicPlaybackSeconds();
	for (const FMusicMountainLyricLine& LyricLine : Lyrics)
	{
		if (PlaybackSeconds >= LyricLine.StartTime && PlaybackSeconds <= LyricLine.EndTime)
		{
			return &LyricLine;
		}
	}
	return nullptr;
}

float AMusicMountainManager::GetMusicPlaybackSeconds() const
{
	if (MusicComponent)
	{
		return MusicPlaybackSeconds;
	}
	return GetElapsedSeconds();
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
		MusicPlaybackSeconds = 0.0f;
	}
}
