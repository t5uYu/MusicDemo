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
	TOptional<float> GetProgress() const;
	EVisibility GetCompletionVisibility() const;
	FReply OnToggleMusicClicked() const;
	FReply OnRestartClicked() const;

	TWeakObjectPtr<AMusicMountainManager> Manager;
};
