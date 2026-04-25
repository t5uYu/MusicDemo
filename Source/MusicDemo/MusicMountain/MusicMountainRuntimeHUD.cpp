// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicMountainRuntimeHUD.h"

#include "MusicMountainManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MusicMountainRuntimeHUD"

void SMusicMountainRuntimeHUD::Construct(const FArguments& InArgs)
{
	Manager = InArgs._Manager;
	ClientLLMProviderOptions = {
		MakeShared<FString>(TEXT("gemini")),
		MakeShared<FString>(TEXT("claude")),
		MakeShared<FString>(TEXT("gpt")),
		MakeShared<FString>(TEXT("deepseek")),
	};
	if (Manager.IsValid())
	{
		ClientLLMProvider = Manager->GetClientLLMProvider();
		ClientLLMEndpointText = FText::FromString(Manager->GetClientLLMEndpoint());
		ClientLLMModelText = FText::FromString(Manager->GetClientLLMModel());
		ClientLLMApiKeyText = FText::FromString(Manager->GetClientLLMApiKey());
	}

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
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("RestartButton", "Restart"))
						.OnClicked(this, &SMusicMountainRuntimeHUD::OnRestartClicked)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("LLMSettingsButton", "LLM Settings"))
						.OnClicked(this, &SMusicMountainRuntimeHUD::OnToggleClientLLMSettingsClicked)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Visibility(this, &SMusicMountainRuntimeHUD::GetClientLLMSettingsVisibility)
					.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
					.Padding(10.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ClientLLMWarning", "Client BYOK demo: your API key is saved locally in plain text on this machine. Do not put a shared production key here."))
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ClientLLMProviderLabel", "Provider"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SComboBox<TSharedPtr<FString>>)
							.OptionsSource(&ClientLLMProviderOptions)
							.OnGenerateWidget(this, &SMusicMountainRuntimeHUD::MakeClientLLMProviderWidget)
							.OnSelectionChanged(this, &SMusicMountainRuntimeHUD::OnClientLLMProviderChanged)
							[
								SNew(STextBlock)
								.Text(this, &SMusicMountainRuntimeHUD::GetClientLLMProviderText)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ClientLLMEndpointLabel", "Endpoint"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SEditableTextBox)
							.Text(this, &SMusicMountainRuntimeHUD::GetClientLLMEndpointText)
							.OnTextCommitted(this, &SMusicMountainRuntimeHUD::OnClientLLMEndpointCommitted)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ClientLLMModelLabel", "Resolved Model"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SEditableTextBox)
							.Text(this, &SMusicMountainRuntimeHUD::GetClientLLMModelText)
							.OnTextCommitted(this, &SMusicMountainRuntimeHUD::OnClientLLMModelCommitted)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ClientLLMApiKeyLabel", "API Key"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SEditableTextBox)
							.Text(this, &SMusicMountainRuntimeHUD::GetClientLLMApiKeyText)
							.OnTextCommitted(this, &SMusicMountainRuntimeHUD::OnClientLLMApiKeyCommitted)
							.IsPassword(true)
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
								.Text(LOCTEXT("SaveClientLLMSettings", "Save Settings"))
								.OnClicked(this, &SMusicMountainRuntimeHUD::OnSaveClientLLMSettingsClicked)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.Text(LOCTEXT("RunClientLLMDirector", "Run Client LLM"))
								.OnClicked(this, &SMusicMountainRuntimeHUD::OnRunClientLLMDirectorClicked)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SMusicMountainRuntimeHUD::GetClientLLMStatusText)
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SMusicMountainRuntimeHUD::GetLyricsLookupStatusText)
							.AutoWrapText(true)
						]
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
		.Padding(190.0f, 0.0f, 190.0f, 26.0f)
		[
			SNew(SBorder)
			.Visibility(this, &SMusicMountainRuntimeHUD::GetSubtitleVisibility)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(this, &SMusicMountainRuntimeHUD::GetSubtitleAccentColor)
			.Padding(2.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.025f, 0.018f, 0.05f, 0.88f))
				.Padding(FMargin(20.0f, 14.0f, 20.0f, 16.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 10.0f, 0.0f)
						[
							SNew(SBorder)
							.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
							.BorderBackgroundColor(this, &SMusicMountainRuntimeHUD::GetSubtitleAccentColor)
							.Padding(FMargin(12.0f, 4.0f))
							[
								SNew(STextBlock)
								.Text(this, &SMusicMountainRuntimeHUD::GetSubtitleSpeakerText)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
								.ColorAndOpacity(FLinearColor(0.03f, 0.02f, 0.045f, 1.0f))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(this, &SMusicMountainRuntimeHUD::GetSubtitleMoodBadgeText)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
							.ColorAndOpacity(FLinearColor(0.82f, 0.78f, 0.92f, 0.9f))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 10.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(this, &SMusicMountainRuntimeHUD::GetSubtitleBodyText)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 24))
						.ColorAndOpacity(FLinearColor(0.98f, 0.94f, 1.0f, 1.0f))
						.ShadowOffset(FVector2D(1.5f, 1.5f))
						.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
						.AutoWrapText(true)
					]
				]
			]
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

FText SMusicMountainRuntimeHUD::GetClientLLMEndpointText() const
{
	return ClientLLMEndpointText;
}

FText SMusicMountainRuntimeHUD::GetClientLLMModelText() const
{
	return ClientLLMModelText;
}

FText SMusicMountainRuntimeHUD::GetClientLLMApiKeyText() const
{
	return ClientLLMApiKeyText;
}

FText SMusicMountainRuntimeHUD::GetClientLLMStatusText() const
{
	return Manager.IsValid() ? FText::FromString(Manager->GetClientLLMStatusText()) : FText::GetEmpty();
}

FText SMusicMountainRuntimeHUD::GetLyricsLookupStatusText() const
{
	return Manager.IsValid() ? FText::FromString(Manager->GetLyricsLookupStatusText()) : FText::GetEmpty();
}

FText SMusicMountainRuntimeHUD::GetClientLLMProviderText() const
{
	return FText::FromString(ClientLLMProvider);
}

FText SMusicMountainRuntimeHUD::GetSubtitleSpeakerText() const
{
	if (!Manager.IsValid())
	{
		return FText::GetEmpty();
	}
	const FString Speaker = Manager->GetCurrentSubtitleSpeaker();
	return FText::FromString(Speaker.IsEmpty() ? TEXT("Lyrics") : Speaker);
}

FText SMusicMountainRuntimeHUD::GetSubtitleBodyText() const
{
	if (!Manager.IsValid())
	{
		return FText::GetEmpty();
	}

	const FString Text = Manager->GetCurrentSubtitleText();
	if (Text.IsEmpty())
	{
		return FText::GetEmpty();
	}

	return FText::FromString(FString::Printf(TEXT("%s |"), *Text));
}

FText SMusicMountainRuntimeHUD::GetSubtitleMoodBadgeText() const
{
	if (!Manager.IsValid())
	{
		return FText::GetEmpty();
	}

	const FString Mood = Manager->GetCurrentSubtitleMood();
	return FText::FromString(Mood.IsEmpty() ? TEXT("VOICE / MUSIC") : FString::Printf(TEXT("MOOD / %s"), *Mood.ToUpper()));
}

FSlateColor SMusicMountainRuntimeHUD::GetSubtitleAccentColor() const
{
	if (!Manager.IsValid())
	{
		return FSlateColor(FLinearColor(0.95f, 0.38f, 0.82f, 0.95f));
	}

	const FString Mood = Manager->GetCurrentSubtitleMood().ToLower();
	if (Mood.Contains(TEXT("dark")) || Mood.Contains(TEXT("tense")) || Mood.Contains(TEXT("melancholy")))
	{
		return FSlateColor(FLinearColor(0.56f, 0.34f, 1.0f, 0.95f));
	}
	if (Mood.Contains(TEXT("epic")) || Mood.Contains(TEXT("rock")))
	{
		return FSlateColor(FLinearColor(1.0f, 0.42f, 0.26f, 0.95f));
	}
	if (Mood.Contains(TEXT("dreamy")) || Mood.Contains(TEXT("calm")) || Mood.Contains(TEXT("classical")))
	{
		return FSlateColor(FLinearColor(0.45f, 0.78f, 1.0f, 0.95f));
	}
	if (Mood.Contains(TEXT("sweet")) || Mood.Contains(TEXT("romantic")))
	{
		return FSlateColor(FLinearColor(1.0f, 0.45f, 0.78f, 0.95f));
	}

	return FSlateColor(FLinearColor(0.95f, 0.38f, 0.82f, 0.95f));
}

EVisibility SMusicMountainRuntimeHUD::GetSubtitleVisibility() const
{
	return Manager.IsValid() && Manager->HasActiveSubtitle() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SMusicMountainRuntimeHUD::GetClientLLMSettingsVisibility() const
{
	return bShowClientLLMSettings ? EVisibility::Visible : EVisibility::Collapsed;
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

FReply SMusicMountainRuntimeHUD::OnToggleClientLLMSettingsClicked() const
{
	bShowClientLLMSettings = !bShowClientLLMSettings;
	return FReply::Handled();
}

FReply SMusicMountainRuntimeHUD::OnSaveClientLLMSettingsClicked() const
{
	if (Manager.IsValid())
	{
		Manager->SetClientLLMSettings(
			ClientLLMProvider,
			ClientLLMEndpointText.ToString(),
			ClientLLMModelText.ToString(),
			ClientLLMApiKeyText.ToString());
		Manager->SaveClientLLMSettings();
	}
	return FReply::Handled();
}

FReply SMusicMountainRuntimeHUD::OnRunClientLLMDirectorClicked() const
{
	if (Manager.IsValid())
	{
		Manager->SetClientLLMSettings(
			ClientLLMProvider,
			ClientLLMEndpointText.ToString(),
			ClientLLMModelText.ToString(),
			ClientLLMApiKeyText.ToString());
		Manager->SaveClientLLMSettings();
		Manager->RequestClientLLMDirector();
	}
	return FReply::Handled();
}

void SMusicMountainRuntimeHUD::OnClientLLMProviderChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo) const
{
	if (!NewSelection.IsValid())
	{
		return;
	}

	ClientLLMProvider = *NewSelection;
	if (Manager.IsValid())
	{
		const FString ExistingKey = ClientLLMApiKeyText.ToString();
		Manager->SetClientLLMProvider(ClientLLMProvider);
		ClientLLMEndpointText = FText::FromString(Manager->GetClientLLMEndpoint());
		ClientLLMModelText = FText::FromString(Manager->GetClientLLMModel());
		ClientLLMApiKeyText = FText::FromString(ExistingKey);
		Manager->SetClientLLMSettings(ClientLLMProvider, ClientLLMEndpointText.ToString(), ClientLLMModelText.ToString(), ExistingKey);
	}
}

TSharedRef<SWidget> SMusicMountainRuntimeHUD::MakeClientLLMProviderWidget(TSharedPtr<FString> Provider) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(Provider.IsValid() ? *Provider : FString()));
}

void SMusicMountainRuntimeHUD::OnClientLLMEndpointCommitted(const FText& Text, ETextCommit::Type CommitType) const
{
	ClientLLMEndpointText = Text;
}

void SMusicMountainRuntimeHUD::OnClientLLMModelCommitted(const FText& Text, ETextCommit::Type CommitType) const
{
	ClientLLMModelText = Text;
}

void SMusicMountainRuntimeHUD::OnClientLLMApiKeyCommitted(const FText& Text, ETextCommit::Type CommitType) const
{
	ClientLLMApiKeyText = Text;
}

#undef LOCTEXT_NAMESPACE
