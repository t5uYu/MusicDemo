#include "AssetToolsModule.h"
#include "AutomatedAssetImportData.h"
#include "DesktopPlatformModule.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MusicDemoEditor"

namespace MusicMountainEditor
{
	static const FName ImportTabName("MusicMountainImport");
}

class SMusicMountainImportPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMusicMountainImportPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		AudioPathText = FText::GetEmpty();
		OutputPathText = FText::FromString(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("MusicMountain/Data/MusicAnalysisDemo.json")));

		ChildSlot
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(16.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Music Mountain Import"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 16.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Description", "Choose an mp3, mp4, m4a, or wav file. The tool runs Tools/MusicMountain/analyze_music.py and writes MusicAnalysisDemo.json for the runtime generator."))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AudioFileLabel", "Audio File"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SAssignNew(AudioPathBox, SEditableTextBox)
						.Text(this, &SMusicMountainImportPanel::GetAudioPathText)
						.OnTextCommitted(this, &SMusicMountainImportPanel::OnAudioPathCommitted)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("BrowseAudio", "Browse..."))
						.OnClicked(this, &SMusicMountainImportPanel::OnBrowseAudio)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 16.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StyleHintLabel", "Style Hints (optional, multi-select)"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 4.0f)
						[
							MakeStyleCheckBox(LOCTEXT("RomanticHint", "Romantic"), bHintRomantic)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 4.0f)
						[
							MakeStyleCheckBox(LOCTEXT("SweetHint", "Sweet"), bHintSweet)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 4.0f)
						[
							MakeStyleCheckBox(LOCTEXT("MelancholyHint", "Melancholy"), bHintMelancholy)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 4.0f)
						[
							MakeStyleCheckBox(LOCTEXT("DreamyHint", "Dreamy"), bHintDreamy)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 0.0f)
						[
							MakeStyleCheckBox(LOCTEXT("ModernHint", "Modern"), bHintModern)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 0.0f)
						[
							MakeStyleCheckBox(LOCTEXT("PopHint", "Pop"), bHintPop)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 0.0f)
						[
							MakeStyleCheckBox(LOCTEXT("ElectronicHint", "Electronic"), bHintElectronic)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 0.0f)
						[
							MakeStyleCheckBox(LOCTEXT("RockHint", "Rock"), bHintRock)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 0.0f)
						[
							MakeStyleCheckBox(LOCTEXT("AcousticHint", "Acoustic"), bHintAcoustic)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 0.0f)
						[
							MakeStyleCheckBox(LOCTEXT("ClassicalHint", "Classical"), bHintClassical)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 0.0f)
						[
							MakeStyleCheckBox(LOCTEXT("EpicHint", "Epic"), bHintEpic)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0.0f, 0.0f, 18.0f, 0.0f)
						[
							MakeStyleCheckBox(LOCTEXT("DarkHint", "Dark"), bHintDark)
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 16.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OutputLabel", "Output JSON"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SAssignNew(OutputPathBox, SEditableTextBox)
						.Text(this, &SMusicMountainImportPanel::GetOutputPathText)
						.OnTextCommitted(this, &SMusicMountainImportPanel::OnOutputPathCommitted)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("UseDemoOutput", "Use Demo JSON"))
						.OnClicked(this, &SMusicMountainImportPanel::OnUseDemoOutput)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 18.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("Analyze", "Analyze Music"))
						.OnClicked(this, &SMusicMountainImportPanel::OnAnalyze)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 18.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LogLabel", "Status"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MinDesiredHeight(160.0f)
					[
						SAssignNew(StatusTextBlock, STextBlock)
						.Text(this, &SMusicMountainImportPanel::GetStatusText)
						.AutoWrapText(true)
					]
				]
			]
		];
	}

private:
	FText GetAudioPathText() const { return AudioPathText; }
	FText GetOutputPathText() const { return OutputPathText; }
	FText GetStatusText() const { return StatusText; }

	void OnAudioPathCommitted(const FText& Text, ETextCommit::Type)
	{
		AudioPathText = Text;
	}

	void OnOutputPathCommitted(const FText& Text, ETextCommit::Type)
	{
		OutputPathText = Text;
	}

	FReply OnBrowseAudio()
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (!DesktopPlatform)
		{
			SetStatus(TEXT("DesktopPlatform is unavailable."));
			return FReply::Handled();
		}

		void* ParentWindowHandle = nullptr;
		if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
		{
			IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
			const TSharedPtr<SWindow> ParentWindow = MainFrameModule.GetParentWindow();
			if (ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid())
			{
				ParentWindowHandle = ParentWindow->GetNativeWindow()->GetOSWindowHandle();
			}
		}

		TArray<FString> OutFiles;
		const bool bSelected = DesktopPlatform->OpenFileDialog(
			ParentWindowHandle,
			TEXT("Choose Music Mountain Audio"),
			FPaths::ProjectDir(),
			TEXT(""),
			TEXT("Audio/Video Files (*.mp3;*.mp4;*.m4a;*.wav)|*.mp3;*.mp4;*.m4a;*.wav|All Files (*.*)|*.*"),
			EFileDialogFlags::None,
			OutFiles);

		if (bSelected && OutFiles.Num() > 0)
		{
			AudioPathText = FText::FromString(OutFiles[0]);
			if (AudioPathBox.IsValid())
			{
				AudioPathBox->SetText(AudioPathText);
			}
		}

		return FReply::Handled();
	}

	FReply OnUseDemoOutput()
	{
		OutputPathText = FText::FromString(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("MusicMountain/Data/MusicAnalysisDemo.json")));
		if (OutputPathBox.IsValid())
		{
			OutputPathBox->SetText(OutputPathText);
		}
		return FReply::Handled();
	}

	FReply OnAnalyze()
	{
		const FString AudioPath = AudioPathText.ToString();
		const FString OutputPath = OutputPathText.ToString();
		const FString StyleHint = BuildStyleHintString();
		if (AudioPath.IsEmpty() || !FPaths::FileExists(AudioPath))
		{
			SetStatus(FString::Printf(TEXT("Audio file does not exist: %s"), *AudioPath));
			return FReply::Handled();
		}

		const FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/MusicMountain/analyze_music.py"));
		if (!FPaths::FileExists(ScriptPath))
		{
			SetStatus(FString::Printf(TEXT("Analyzer script does not exist: %s"), *ScriptPath));
			return FReply::Handled();
		}

		FString Params = FString::Printf(
			TEXT("\"%s\" \"%s\" --output \"%s\" --pretty"),
			*ScriptPath,
			*AudioPath,
			*OutputPath);
		if (!StyleHint.IsEmpty())
		{
			Params += FString::Printf(TEXT(" --style-hint \"%s\""), *StyleHint);
		}

		SetStatus(FString::Printf(TEXT("Running: python %s"), *Params));

		int32 ReturnCode = -1;
		FString StdOut;
		FString StdErr;
		FPlatformProcess::ExecProcess(TEXT("python"), *Params, &ReturnCode, &StdOut, &StdErr);

		FString ImportStatus;
		if (ReturnCode == 0)
		{
			ImportStatus = ImportAudioForRuntime(AudioPath, OutputPath);
		}

		const FString Result = FString::Printf(
			TEXT("ExitCode: %d\n\n[stdout]\n%s\n\n[stderr]\n%s\n\n[asset import]\n%s"),
			ReturnCode,
			*StdOut,
			*StdErr,
			*ImportStatus);
		SetStatus(Result);

		FNotificationInfo Info(ReturnCode == 0
			? LOCTEXT("AnalyzeSuccess", "Music Mountain analysis completed.")
			: LOCTEXT("AnalyzeFailed", "Music Mountain analysis failed. Check the status log."));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);

		return FReply::Handled();
	}

	FString ImportAudioForRuntime(const FString& AudioPath, const FString& JsonPath) const
	{
		const FString DecoderPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/MusicMountain/bin/mf_decode.exe"));
		if (!FPaths::FileExists(DecoderPath))
		{
			return FString::Printf(TEXT("Skipped SoundWave import. Decoder missing: %s"), *DecoderPath);
		}

		const FString Stem = FPaths::GetBaseFilename(AudioPath).Replace(TEXT(" "), TEXT("_"));
		const FString WavDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MusicMountainImport"));
		IFileManager::Get().MakeDirectory(*WavDir, true);
		const FString WavPath = WavDir / (Stem + TEXT("_MusicMountain.wav"));

		int32 DecodeReturnCode = -1;
		FString DecodeStdOut;
		FString DecodeStdErr;
		const FString DecodeParams = FString::Printf(TEXT("\"%s\" \"%s\""), *AudioPath, *WavPath);
		FPlatformProcess::ExecProcess(*DecoderPath, *DecodeParams, &DecodeReturnCode, &DecodeStdOut, &DecodeStdErr);
		if (DecodeReturnCode != 0 || !FPaths::FileExists(WavPath))
		{
			return FString::Printf(TEXT("SoundWave import failed during decode. ExitCode=%d\n%s\n%s"), DecodeReturnCode, *DecodeStdOut, *DecodeStdErr);
		}

		const FString DestinationPath = TEXT("/Game/MusicMountain/Imported");
		UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
		ImportData->Filenames.Add(WavPath);
		ImportData->DestinationPath = DestinationPath;
		ImportData->bReplaceExisting = true;
		ImportData->bSkipReadOnly = true;

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		const TArray<UObject*> ImportedAssets = AssetToolsModule.Get().ImportAssetsAutomated(ImportData);
		if (ImportedAssets.Num() == 0)
		{
			return TEXT("SoundWave import failed: no assets imported.");
		}

		USoundBase* ImportedSound = Cast<USoundBase>(ImportedAssets[0]);
		if (!ImportedSound)
		{
			return FString::Printf(TEXT("Imported asset is not a SoundBase: %s"), *ImportedAssets[0]->GetPathName());
		}

		const FString AssetPath = ImportedSound->GetPathName();
		const bool bUpdatedJson = WriteAudioAssetPathToJson(JsonPath, AssetPath);
		return FString::Printf(TEXT("Imported SoundWave: %s\nUpdated JSON audio_asset_path: %s"), *AssetPath, bUpdatedJson ? TEXT("yes") : TEXT("no"));
	}

	bool WriteAudioAssetPathToJson(const FString& JsonPath, const FString& AssetPath) const
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
		{
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return false;
		}

		RootObject->SetStringField(TEXT("audio_asset_path"), AssetPath);
		FString OutputText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputText);
		if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
		{
			return false;
		}

		return FFileHelper::SaveStringToFile(OutputText + LINE_TERMINATOR, *JsonPath);
	}

	void SetStatus(const FString& NewStatus)
	{
		StatusText = FText::FromString(NewStatus);
		if (StatusTextBlock.IsValid())
		{
			StatusTextBlock->SetText(StatusText);
		}
	}

	TSharedRef<SWidget> MakeStyleCheckBox(const FText& Label, bool& bValue)
	{
		bool* ValuePtr = &bValue;
		return SNew(SCheckBox)
			.IsChecked_Lambda([ValuePtr]()
			{
				return *ValuePtr ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([ValuePtr](ECheckBoxState NewState)
			{
				*ValuePtr = NewState == ECheckBoxState::Checked;
			})
			[
				SNew(STextBlock)
				.Text(Label)
			];
	}

	FString BuildStyleHintString() const
	{
		TArray<FString> Hints;
		if (bHintRomantic)
		{
			Hints.Add(TEXT("romantic"));
		}
		if (bHintSweet)
		{
			Hints.Add(TEXT("sweet"));
		}
		if (bHintMelancholy)
		{
			Hints.Add(TEXT("melancholy"));
		}
		if (bHintDreamy)
		{
			Hints.Add(TEXT("dreamy"));
		}
		if (bHintModern)
		{
			Hints.Add(TEXT("modern"));
		}
		if (bHintPop)
		{
			Hints.Add(TEXT("pop"));
		}
		if (bHintElectronic)
		{
			Hints.Add(TEXT("electronic"));
		}
		if (bHintRock)
		{
			Hints.Add(TEXT("rock"));
		}
		if (bHintAcoustic)
		{
			Hints.Add(TEXT("acoustic"));
		}
		if (bHintClassical)
		{
			Hints.Add(TEXT("classical"));
		}
		if (bHintEpic)
		{
			Hints.Add(TEXT("epic"));
		}
		if (bHintDark)
		{
			Hints.Add(TEXT("dark"));
		}
		return FString::Join(Hints, TEXT(","));
	}

	FText AudioPathText;
	FText OutputPathText;
	FText StatusText = LOCTEXT("InitialStatus", "Choose an audio file, then click Analyze Music.");
	TSharedPtr<SEditableTextBox> AudioPathBox;
	TSharedPtr<SEditableTextBox> OutputPathBox;
	TSharedPtr<STextBlock> StatusTextBlock;
	bool bHintRomantic = false;
	bool bHintSweet = false;
	bool bHintMelancholy = false;
	bool bHintDreamy = false;
	bool bHintModern = false;
	bool bHintPop = false;
	bool bHintElectronic = false;
	bool bHintRock = false;
	bool bHintAcoustic = false;
	bool bHintClassical = false;
	bool bHintEpic = false;
	bool bHintDark = false;
};

class FMusicDemoEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			MusicMountainEditor::ImportTabName,
			FOnSpawnTab::CreateRaw(this, &FMusicDemoEditorModule::SpawnMusicMountainImportTab))
			.SetDisplayName(LOCTEXT("MusicMountainImportTab", "Music Mountain Import"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMusicDemoEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MusicMountainEditor::ImportTabName);
	}

private:
	TSharedRef<SDockTab> SpawnMusicMountainImportTab(const FSpawnTabArgs&)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(SMusicMountainImportPanel)
			];
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection("MusicMountain");
		Section.AddMenuEntry(
			"OpenMusicMountainImport",
			LOCTEXT("OpenMusicMountainImport", "Music Mountain Import"),
			LOCTEXT("OpenMusicMountainImportTooltip", "Analyze an audio file and generate Music Mountain JSON."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FMusicDemoEditorModule::OpenMusicMountainImportTab)));
	}

	void OpenMusicMountainImportTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(MusicMountainEditor::ImportTabName);
	}
};

IMPLEMENT_MODULE(FMusicDemoEditorModule, MusicDemoEditor)

#undef LOCTEXT_NAMESPACE
