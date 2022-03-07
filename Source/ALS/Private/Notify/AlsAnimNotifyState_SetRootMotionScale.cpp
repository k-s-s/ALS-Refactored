#include "Notify/AlsAnimNotifyState_SetRootMotionScale.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Utility/AlsLog.h"

UAlsAnimNotifyState_SetRootMotionScale::UAlsAnimNotifyState_SetRootMotionScale()
{
	bIsNativeBranchingPoint = true;
}

FString UAlsAnimNotifyState_SetRootMotionScale::GetNotifyName_Implementation() const
{
	return FString::Format(TEXT("Als Set Root Motion Scale: {0}"), {TranslationScale});
}

void UAlsAnimNotifyState_SetRootMotionScale::NotifyBegin(USkeletalMeshComponent* MeshComponent, UAnimSequenceBase* Animation,
                                                         const float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComponent, Animation, TotalDuration, EventReference);

	auto* Character{Cast<ACharacter>(MeshComponent->GetOwner())};
	if (IsValid(Character) && Character->GetLocalRole() >= ROLE_AutonomousProxy)
	{
		Character->SetAnimRootMotionTranslationScale(TranslationScale);
	}
}

void UAlsAnimNotifyState_SetRootMotionScale::NotifyEnd(USkeletalMeshComponent* MeshComponent, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComponent, Animation, EventReference);

	auto* Character{Cast<ACharacter>(MeshComponent->GetOwner())};
	if (IsValid(Character) && Character->GetLocalRole() >= ROLE_AutonomousProxy)
	{
		if (FMath::IsNearlyEqual(Character->GetAnimRootMotionTranslationScale(), TranslationScale))
		{
			Character->SetAnimRootMotionTranslationScale(1.0f);
		}
		else
		{
			UE_LOG(LogAls, Warning, TEXT("%s: The current translation scale does not match the translation scale from the animation notify!"
				       " Probably something changed it before the animation notify ended."), ANSI_TO_TCHAR(__FUNCTION__));
		}
	}
}
