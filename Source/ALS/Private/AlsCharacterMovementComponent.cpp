#include "AlsCharacterMovementComponent.h"

#include "AlsCharacter.h"
#include "Curves/CurveVector.h"
#include "GameFramework/Controller.h"

void FAlsCharacterNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& Move, const ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(Move, MoveType);

	const auto& SavedMove{static_cast<const FAlsSavedMove&>(Move)};

	Stance = SavedMove.Stance;
	RotationMode = SavedMove.RotationMode;
	MaxAllowedGait = SavedMove.MaxAllowedGait;
}

bool FAlsCharacterNetworkMoveData::Serialize(UCharacterMovementComponent& Movement, FArchive& Archive,
                                             UPackageMap* Map, const ENetworkMoveType MoveType)
{
	Super::Serialize(Movement, Archive, Map, MoveType);

	SerializeOptionalValue(Archive.IsSaving(), Archive, Stance, EAlsStance::Standing);
	SerializeOptionalValue(Archive.IsSaving(), Archive, RotationMode, EAlsRotationMode::LookingDirection);
	SerializeOptionalValue(Archive.IsSaving(), Archive, MaxAllowedGait, EAlsGait::Walking);

	return !Archive.IsError();
}

FAlsCharacterNetworkMoveDataContainer::FAlsCharacterNetworkMoveDataContainer()
{
	NewMoveData = &MoveData[0];
	PendingMoveData = &MoveData[1];
	OldMoveData = &MoveData[2];
}

void FAlsSavedMove::Clear()
{
	Super::Clear();

	Stance = EAlsStance::Standing;
	RotationMode = EAlsRotationMode::LookingDirection;
	MaxAllowedGait = EAlsGait::Walking;
}

void FAlsSavedMove::SetMoveFor(ACharacter* Character, const float NewDeltaTime, const FVector& NewAcceleration,
                               FNetworkPredictionData_Client_Character& PredictionData)
{
	Super::SetMoveFor(Character, NewDeltaTime, NewAcceleration, PredictionData);

	const auto* Movement{Cast<UAlsCharacterMovementComponent>(Character->GetCharacterMovement())};
	if (IsValid(Movement))
	{
		Stance = Movement->Stance;
		RotationMode = Movement->RotationMode;
		MaxAllowedGait = Movement->MaxAllowedGait;
	}
}

bool FAlsSavedMove::CanCombineWith(const FSavedMovePtr& NewMovePtr, ACharacter* Character, const float MaxDelta) const
{
	const auto* NewMove{static_cast<FAlsSavedMove*>(NewMovePtr.Get())};

	return Stance == NewMove->Stance &&
	       RotationMode == NewMove->RotationMode &&
	       MaxAllowedGait == NewMove->MaxAllowedGait &&
	       Super::CanCombineWith(NewMovePtr, Character, MaxDelta);
}

void FAlsSavedMove::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);

	auto* Movement{Cast<UAlsCharacterMovementComponent>(Character->GetCharacterMovement())};
	if (IsValid(Movement))
	{
		Movement->Stance = Stance;
		Movement->RotationMode = RotationMode;
		Movement->MaxAllowedGait = MaxAllowedGait;

		Movement->RefreshGaitSettings();
	}
}

FAlsNetworkPredictionData::FAlsNetworkPredictionData(const UCharacterMovementComponent& MovementComponent) : Super(MovementComponent) {}

FSavedMovePtr FAlsNetworkPredictionData::AllocateNewMove()
{
	return MakeShared<FAlsSavedMove>();
}

UAlsCharacterMovementComponent::UAlsCharacterMovementComponent()
{
	SetNetworkMoveDataContainer(MoveDataContainer);

	// NetworkMaxSmoothUpdateDistance = 92.0f;
	// NetworkNoSmoothUpdateDistance = 140.0f;

	MaxWalkSpeed = 175.0f;
	MaxWalkSpeedCrouched = 150.0;
	MaxAcceleration = 1500.0f;

	BrakingFrictionFactor = 0.0f;
	CrouchedHalfHeight = 56.0f;
	bRunPhysicsWithNoController = true;

	MinAnalogWalkSpeed = 25.0f;
	bCanWalkOffLedgesWhenCrouching = true;
	PerchRadiusThreshold = 20.0f;
	PerchAdditionalHeight = 0.0f;
	LedgeCheckThreshold = 0.0f;

	AirControl = 0.15f;
	BrakingDecelerationFlying = 1000.0f;

	NavAgentProps.bCanCrouch = true;
	NavAgentProps.bCanFly = true;
	bUseAccelerationForPaths = true;

	// https://unrealengine.hatenablog.com/entry/2019/01/16/231404

	FallingLateralFriction = 1.0f;
	JumpOffJumpZFactor = 0.0f;

	bAllowPhysicsRotationDuringAnimRootMotion = true;
	bIgnoreBaseRotation = true; // bIgnoreBaseRotation == false is not supported.
}

#if WITH_EDITOR
bool UAlsCharacterMovementComponent::CanEditChange(const FProperty* Property) const
{
	auto bCanEditChange{Super::CanEditChange(Property)};

	if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, bIgnoreBaseRotation))
	{
		bCanEditChange = false;
	}

	return bCanEditChange;
}
#endif

void UAlsCharacterMovementComponent::SetMovementMode(const EMovementMode NewMovementMode, const uint8 NewCustomMode)
{
	if (!bMovementModeLocked)
	{
		Super::SetMovementMode(NewMovementMode, NewCustomMode);
	}
}

void UAlsCharacterMovementComponent::OnMovementModeChanged(const EMovementMode PreviousMovementMode, const uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	// This removes some very noticeable changes in the mesh location when the character automatically uncrouches at the end of the roll.

	bCrouchMaintainsBaseLocation = true;
}

void UAlsCharacterMovementComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
                                                   FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// If move combining is enabled, then as a fallback flush server moves to ensure
	// that local character rotation is applied correctly on autonomous proxy clients.

	static const auto* MoveCombiningConsoleVariable{IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetEnableMoveCombining"))};
	if (MoveCombiningConsoleVariable != nullptr && MoveCombiningConsoleVariable->GetInt() != 0)
	{
		FlushServerMoves();
	}
}

float UAlsCharacterMovementComponent::GetMaxAcceleration() const
{
	// Get the acceleration using the movement curve. This allows for fine control over movement behavior at each speed.

	return IsMovingOnGround() && IsValid(GaitSettings.AccelerationAndDecelerationAndGroundFrictionCurve)
		       ? GaitSettings.AccelerationAndDecelerationAndGroundFrictionCurve->GetVectorValue(CalculateGaitAmount()).X
		       : Super::GetMaxAcceleration();
}

float UAlsCharacterMovementComponent::GetMaxBrakingDeceleration() const
{
	// Get the deceleration using the movement curve. This allows for fine control over movement behavior at each speed.

	return IsMovingOnGround() && IsValid(GaitSettings.AccelerationAndDecelerationAndGroundFrictionCurve)
		       ? GaitSettings.AccelerationAndDecelerationAndGroundFrictionCurve->GetVectorValue(CalculateGaitAmount()).Y
		       : Super::GetMaxBrakingDeceleration();
}

void UAlsCharacterMovementComponent::PhysicsRotation(const float DeltaTime)
{
	if (HasValidData() && (IsValid(CharacterOwner->Controller) || bRunPhysicsWithNoController))
	{
		Super::PhysicsRotation(DeltaTime);

		OnPhysicsRotation.Broadcast(DeltaTime);
	}
}

void UAlsCharacterMovementComponent::PhysWalking(const float DeltaTime, const int32 Iterations)
{
	if (IsValid(GaitSettings.AccelerationAndDecelerationAndGroundFrictionCurve))
	{
		// Get the ground friction using the movement curve. This allows for fine control over movement behavior at each speed.

		GroundFriction = GaitSettings.AccelerationAndDecelerationAndGroundFrictionCurve->GetVectorValue(CalculateGaitAmount()).Z;
	}

	Super::PhysWalking(DeltaTime, Iterations);
}

void UAlsCharacterMovementComponent::PhysCustom(const float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME)
	{
		Super::PhysCustom(DeltaTime, Iterations);
		return;
	}

	Iterations++;
	bJustTeleported = false;

	RestorePreAdditiveRootMotionVelocity();

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = FVector::ZeroVector;
	}

	ApplyRootMotionToVelocity(DeltaTime);

	MoveUpdatedComponent(Velocity * DeltaTime, UpdatedComponent->GetComponentQuat(), false);

	Super::PhysCustom(DeltaTime, Iterations);
}

FNetworkPredictionData_Client* UAlsCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		auto* MutableThis{const_cast<UAlsCharacterMovementComponent*>(this)};

		MutableThis->ClientPredictionData = new FAlsNetworkPredictionData{*this};
	}

	return ClientPredictionData;
}

void UAlsCharacterMovementComponent::MoveAutonomous(const float ClientTimeStamp, const float DeltaTime,
                                                    const uint8 CompressedFlags, const FVector& NewAcceleration)
{
	const auto* MoveData{static_cast<FAlsCharacterNetworkMoveData*>(GetCurrentNetworkMoveData())};
	if (MoveData != nullptr)
	{
		Stance = MoveData->Stance;
		RotationMode = MoveData->RotationMode;
		MaxAllowedGait = MoveData->MaxAllowedGait;

		RefreshGaitSettings();
	}

	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAcceleration);
}

void UAlsCharacterMovementComponent::SetMovementSettings(UAlsMovementSettings* NewMovementSettings)
{
	MovementSettings = NewMovementSettings;

	RefreshGaitSettings();
}

void UAlsCharacterMovementComponent::RefreshGaitSettings()
{
	GaitSettings = IsValid(MovementSettings)
		               ? *MovementSettings->GetMovementStanceSettingsForRotationMode(RotationMode)->GetMovementGaitSettingsForStance(Stance)
		               : FAlsMovementGaitSettings{};

	RefreshMaxWalkSpeed();
}

void UAlsCharacterMovementComponent::SetStance(const EAlsStance NewStance)
{
	if (Stance != NewStance)
	{
		Stance = NewStance;

		RefreshGaitSettings();
	}
}

void UAlsCharacterMovementComponent::SetRotationMode(const EAlsRotationMode NewMode)
{
	if (RotationMode != NewMode)
	{
		RotationMode = NewMode;

		RefreshGaitSettings();
	}
}

void UAlsCharacterMovementComponent::SetMaxAllowedGait(const EAlsGait NewGait)
{
	if (MaxAllowedGait != NewGait)
	{
		MaxAllowedGait = NewGait;

		RefreshMaxWalkSpeed();
	}
}

void UAlsCharacterMovementComponent::RefreshMaxWalkSpeed()
{
	MaxWalkSpeed = GaitSettings.GetSpeedForGait(MaxAllowedGait);
	MaxWalkSpeedCrouched = MaxWalkSpeed;
}

float UAlsCharacterMovementComponent::CalculateGaitAmount() const
{
	// Map the character's current speed to the configured movement speeds ranging from 0 to 3,
	// where 0 is stopped, 1 is walking, 2 is running and 3 is sprinting. This allows us to vary
	// the movement speeds but still use the mapped range in calculations for consistent results.

	const auto Speed{Velocity.Size2D()};

	if (Speed <= GaitSettings.WalkSpeed)
	{
		return FMath::GetMappedRangeValueClamped(FVector2D(0.0f, GaitSettings.WalkSpeed), FVector2D(0.0f, 1.0f), Speed);
	}

	if (Speed <= GaitSettings.RunSpeed)
	{
		return FMath::GetMappedRangeValueClamped(FVector2D(GaitSettings.WalkSpeed, GaitSettings.RunSpeed), FVector2D(1.0f, 2.0f), Speed);
	}

	return FMath::GetMappedRangeValueClamped(FVector2D(GaitSettings.RunSpeed, GaitSettings.SprintSpeed), FVector2D(2.0f, 3.0f), Speed);
}

void UAlsCharacterMovementComponent::SetMovementModeLocked(const bool bNewMovementModeLocked)
{
	bMovementModeLocked = bNewMovementModeLocked;
}
