// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MusicDemoGameMode.generated.h"

UCLASS(minimalapi)
class AMusicDemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMusicDemoGameMode();

protected:
	virtual void BeginPlay() override;
};



