// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicDemoGameMode.h"
#include "MusicDemoCharacter.h"
#include "MusicMountain/MusicMountainManager.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AMusicDemoGameMode::AMusicDemoGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}

void AMusicDemoGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		World->SpawnActor<AMusicMountainManager>(
			AMusicMountainManager::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator);
	}
}
