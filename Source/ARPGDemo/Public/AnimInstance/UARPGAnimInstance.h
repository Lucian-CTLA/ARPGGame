// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "UARPGAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class ARPGDEMO_API UUARPGAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;//every tick update Animation

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim")
	float GroundSpeed;//On Ground speed

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Anim")
	bool bHasAcceleration;//is it has add speed
	
};
