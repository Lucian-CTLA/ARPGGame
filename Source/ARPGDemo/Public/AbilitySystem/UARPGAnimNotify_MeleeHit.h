// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayEffect.h"
#include "Sound/SoundBase.h" 
#include "NiagaraSystem.h" 
#include "Camera/CameraShakeBase.h"
#include "TimerManager.h"

#include "UARPGAnimNotify_MeleeHit.generated.h"

/**
 * 
 */
UCLASS()
class ARPGDEMO_API UUARPGAnimNotify_MeleeHit : public UAnimNotify
{
	GENERATED_BODY()

public:
	UUARPGAnimNotify_MeleeHit();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee")
	TSubclassOf< UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee")
	float SphereRadius = 150.f;

	UPROPERTY(EditAnywhere, Category = "Melee")
	float ForwardOffset = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee")
	TObjectPtr<UNiagaraSystem> HitVFX;


	UPROPERTY(EditAnywhere, Category = "Melee")
	TSubclassOf<UCameraShakeBase> HitShakeClass;

protected:
		virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference
	) override;
};
