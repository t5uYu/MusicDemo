// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MusicMountainDirectorSettings.generated.h"

UCLASS(config = EditorPerUser, defaultconfig, meta = (DisplayName = "Music Mountain Director"))
class UMusicMountainDirectorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMusicMountainDirectorSettings();

	/** OpenAI-compatible chat completions endpoint. */
	UPROPERTY(config, EditAnywhere, Category = "LLM Director", meta = (DisplayName = "Endpoint URL"))
	FString EndpointUrl = TEXT("https://api.deepseek.com/chat/completions");

	/** Model name sent to the endpoint. */
	UPROPERTY(config, EditAnywhere, Category = "LLM Director", meta = (DisplayName = "Model"))
	FString Model = TEXT("deepseek-chat");

	/** API key used only in the local editor session. Injected as MUSIC_MOUNTAIN_LLM_API_KEY. */
	UPROPERTY(config, EditAnywhere, Category = "LLM Director", meta = (DisplayName = "API Key", PasswordField = true))
	FString ApiKey;

	/** Optional provider label for logs and future provider-specific behavior. */
	UPROPERTY(config, EditAnywhere, Category = "LLM Director", meta = (DisplayName = "Provider"))
	FString Provider = TEXT("deepseek");

	/** Request timeout in seconds for the Python LLM call. */
	UPROPERTY(config, EditAnywhere, Category = "LLM Director", meta = (DisplayName = "Request Timeout Seconds", ClampMin = "10", ClampMax = "300"))
	int32 RequestTimeoutSeconds = 90;

	/** Temperature for the LLM Director request. Lower values keep JSON more stable. */
	UPROPERTY(config, EditAnywhere, Category = "LLM Director", meta = (DisplayName = "Temperature", ClampMin = "0.0", ClampMax = "1.0"))
	float Temperature = 0.2f;

	static const UMusicMountainDirectorSettings* GetSettings();
};
