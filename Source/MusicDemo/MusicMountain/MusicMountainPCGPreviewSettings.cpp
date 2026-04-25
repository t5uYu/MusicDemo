// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicMountainPCGPreviewSettings.h"

#include "MusicMountainPCGSectionActor.h"
#include "PCGComponent.h"
#include "PCGContext.h"

FPCGElementPtr UPCGMusicMountainPreviewSettings::CreateElement() const
{
	return MakeShared<FMusicMountainPCGPreviewElement>();
}

bool FMusicMountainPCGPreviewElement::ExecuteInternal(FPCGContext* Context) const
{
	if (!Context)
	{
		return true;
	}

	const UPCGMusicMountainPreviewSettings* Settings = Context->GetInputSettings<UPCGMusicMountainPreviewSettings>();
	UPCGComponent* SourceComponent = Context->SourceComponent.Get();
	AMusicMountainPCGSectionActor* SectionActor = SourceComponent ? Cast<AMusicMountainPCGSectionActor>(SourceComponent->GetOwner()) : nullptr;
	if (!Settings || !SectionActor)
	{
		return true;
	}

	SectionActor->GeneratePreviewDecorationsFromPCG(Settings->DensityMultiplier);
	Context->OutputData = Context->InputData;
	return true;
}
