// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/UARPGAnimInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"

void UUARPGAnimInstance::NativeUpdateAnimation(float DeltaSecond)
{
	Super::NativeUpdateAnimation(DeltaSecond);
	const APawn* OwnerPawn = TryGetPawnOwner();//you can see anim

	if (!OwnerPawn)//don't have then had a nulllptr
	{
		return;
	}
	GroundSpeed = OwnerPawn->GetVelocity().Size2D();//

	const UCharacterMovementComponent* MoveComp = OwnerPawn->FindComponentByClass
		<UCharacterMovementComponent>();//get move component
	if (MoveComp)
	{
		bHasAcceleration = MoveComp->GetCurrentAcceleration().SizeSquared() > 0.f;
	}
	
}
