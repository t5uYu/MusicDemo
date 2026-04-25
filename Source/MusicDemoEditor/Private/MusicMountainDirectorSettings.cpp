// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicMountainDirectorSettings.h"

UMusicMountainDirectorSettings::UMusicMountainDirectorSettings()
{
	CategoryName = TEXT("Project");
	SectionName = TEXT("Music Mountain Director");
}

const UMusicMountainDirectorSettings* UMusicMountainDirectorSettings::GetSettings()
{
	return GetDefault<UMusicMountainDirectorSettings>();
}
