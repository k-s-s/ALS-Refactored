#include "Notify/AlsAnimNotifyState_SetLocomotionAction.h"

#include "AlsCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Utility/AlsUtility.h"

UAlsAnimNotifyState_SetLocomotionAction::UAlsAnimNotifyState_SetLocomotionAction()
{
	bIsNativeBranchingPoint = true;
}

FString UAlsAnimNotifyState_SetLocomotionAction::GetNotifyName_Implementation() const
{
	return FString::Format(TEXT("Als Set Locomotion Action: {0}"), {
		                       FName::NameToDisplayString(UAlsUtility::GetSimpleTagName(LocomotionAction).ToString(), false)
	                       });
}

void UAlsAnimNotifyState_SetLocomotionAction::NotifyBegin(USkeletalMeshComponent* MeshComponent, UAnimSequenceBase* Animation,
                                                          const float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComponent, Animation, TotalDuration, EventReference);

	auto* Character{Cast<AAlsCharacter>(MeshComponent->GetOwner())};
	if (IsValid(Character))
	{
		Character->SetLocomotionAction(LocomotionAction);
	}
}

void UAlsAnimNotifyState_SetLocomotionAction::NotifyEnd(USkeletalMeshComponent* MeshComponent, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComponent, Animation, EventReference);

	auto* Character{Cast<AAlsCharacter>(MeshComponent->GetOwner())};
	if (IsValid(Character) && Character->GetLocomotionAction() == LocomotionAction)
	{
		Character->SetLocomotionAction(FGameplayTag::EmptyTag);
	}
}
