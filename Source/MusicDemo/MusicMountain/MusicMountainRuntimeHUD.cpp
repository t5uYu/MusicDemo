// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicMountainRuntimeHUD.h"

#include "MusicMountainManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MusicMountainRuntimeHUD"

void SMusicMountainRuntimeHUD::Construct(const FArguments& InArgs)
{
	Manager = InArgs._Manager;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(24.0f, 18.0f, 24.0f, 0.0f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(12.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(this, &SMusicMountainRuntimeHUD::GetSongText)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(this, &SMusicMountainRuntimeHUD::GetSectionText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(this, &SMusicMountainRuntimeHUD::GetMoodText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(this, &SMusicMountainRuntimeHUD::GetThemeText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(SProgressBar)
					.Percent(this, &SMusicMountainRuntimeHUD::GetProgress)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 12.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(this, &SMusicMountainRuntimeHUD::GetTimeText)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(this, &SMusicMountainRuntimeHUD::GetMusicStateText)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("PauseButton", "Play / Pause"))
						.OnClicked(this, &SMusicMountainRuntimeHUD::OnToggleMusicClicked)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("RestartButton", "Restart"))
						.OnClicked(this, &SMusicMountainRuntimeHUD::OnRestartClicked)
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSpacer)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(24.0f)
		[
			SNew(SBorder)
			.Visibility(this, &SMusicMountainRuntimeHUD::GetCompletionVisibility)
			.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(16.0f)
			[
				SNew(STextBlock)
				.Text(this, &SMusicMountainRuntimeHUD::GetCompletionText)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
				.AutoWrapText(true)
			]
		]
	];
}

FText SMusicMountainRuntimeHUD::GetSongText() const
{
	if (!Manager.IsValid())
	{
		return LOCTEXT("NoManagerSong", "Music Mountain");
	}
	return FText::FromString(FString::Printf(TEXT("%s | BPM %.0f | Seed %d"), *Manager->GetSongDisplayName(), Manager->GetMusicBpm(), Manager->GetMusicSeed()));
}

FText SMusicMountainRuntimeHUD::GetSectionText() const
{
	if (!Manager.IsValid())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Printf(TEXT("Section: %s | Terrain: %s | Energy %.0f%%"), *Manager->GetCurrentSectionName(), *Manager->GetCurrentTerrain(), Manager->GetCurrentSectionEnergy() * 100.0f));
}

FText SMusicMountainRuntimeHUD::GetMoodText() const
{
	if (!Manager.IsValid())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Printf(TEXT("Mood: %s"), *Manager->GetCurrentMood()));
}

FText SMusicMountainRuntimeHUD::GetThemeText() const
{
	if (!Manager.IsValid())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Printf(TEXT("Theme: %s"), *Manager->GetThemeText()));
}

FText SMusicMountainRuntimeHUD::GetTimeText() const
{
	if (!Manager.IsValid())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Printf(TEXT("Time: %.1fs | Altitude: %.0f%%"), Manager->GetElapsedSeconds(), Manager->GetAltitudeProgress() * 100.0f));
}

FText SMusicMountainRuntimeHUD::GetMusicStateText() const
{
	if (!Manager.IsValid())
	{
		return FText::GetEmpty();
	}
	const TCHAR* State = Manager->IsMusicPaused() ? TEXT("Paused") : (Manager->IsMusicPlaying() ? TEXT("Playing") : TEXT("Stopped"));
	return FText::FromString(FString::Printf(TEXT("Music: %s | Tab Cursor | P Pause | R Restart"), State));
}

FText SMusicMountainRuntimeHUD::GetCompletionText() const
{
	if (!Manager.IsValid())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Printf(
		TEXT("Summit Complete!\n%s\nTime: %.1fs | Climb: %.0fm | Theme: %s\nPress R or click Restart to play again."),
		*Manager->GetSongDisplayName(),
		Manager->GetFinishElapsedSeconds(),
		Manager->GetTotalElevationMeters(),
		*Manager->GetThemeText()));
}

TOptional<float> SMusicMountainRuntimeHUD::GetProgress() const
{
	return Manager.IsValid() ? Manager->GetAltitudeProgress() : 0.0f;
}

EVisibility SMusicMountainRuntimeHUD::GetCompletionVisibility() const
{
	return Manager.IsValid() && Manager->IsDemoCompleted() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SMusicMountainRuntimeHUD::OnToggleMusicClicked() const
{
	if (Manager.IsValid())
	{
		Manager->ToggleMusicPaused();
	}
	return FReply::Handled();
}

FReply SMusicMountainRuntimeHUD::OnRestartClicked() const
{
	if (Manager.IsValid())
	{
		Manager->RestartDemo();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
