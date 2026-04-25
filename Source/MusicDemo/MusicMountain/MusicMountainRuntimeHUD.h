// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AMusicMountainManager;

class SMusicMountainRuntimeHUD : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMusicMountainRuntimeHUD) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AMusicMountainManager>, Manager)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FText GetSongText() const;
	FText GetSectionText() const;
	FText GetMoodText() const;
	FText GetThemeText() const;
	FText GetTimeText() const;
	FText GetMusicStateText() const;
	FText GetCompletionText() const;
	FText GetClientLLMEndpointText() const;
	FText GetClientLLMModelText() const;
	FText GetClientLLMApiKeyText() const;
	FText GetClientLLMStatusText() const;
	FText GetLyricsLookupStatusText() const;
	FText GetClientLLMProviderText() const;
	FText GetSubtitleSpeakerText() const;
	FText GetSubtitleBodyText() const;
	FText GetSubtitleMoodBadgeText() const;
	FSlateColor GetSubtitleAccentColor() const;
	EVisibility GetSubtitleVisibility() const;
	EVisibility GetClientLLMSettingsVisibility() const;
	TOptional<float> GetProgress() const;
	EVisibility GetCompletionVisibility() const;
	FReply OnToggleMusicClicked() const;
	FReply OnRestartClicked() const;
	FReply OnToggleClientLLMSettingsClicked() const;
	FReply OnSaveClientLLMSettingsClicked() const;
	FReply OnRunClientLLMDirectorClicked() const;
	void OnClientLLMProviderChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo) const;
	TSharedRef<SWidget> MakeClientLLMProviderWidget(TSharedPtr<FString> Provider) const;
	void OnClientLLMEndpointCommitted(const FText& Text, ETextCommit::Type CommitType) const;
	void OnClientLLMModelCommitted(const FText& Text, ETextCommit::Type CommitType) const;
	void OnClientLLMApiKeyCommitted(const FText& Text, ETextCommit::Type CommitType) const;

	TWeakObjectPtr<AMusicMountainManager> Manager;
	mutable bool bShowClientLLMSettings = false;
	mutable FText ClientLLMEndpointText;
	mutable FText ClientLLMModelText;
	mutable FText ClientLLMApiKeyText;
	mutable FString ClientLLMProvider = TEXT("deepseek");
	TArray<TSharedPtr<FString>> ClientLLMProviderOptions;
};
