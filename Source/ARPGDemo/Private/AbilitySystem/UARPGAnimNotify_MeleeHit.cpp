// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/UARPGAnimNotify_MeleeHit.h"
#include "Components/SkeletalMeshComponent.h"  
#include "Animation/AnimSequenceBase.h"

#include "AARPGCharacterBase.h" 
#include "AttributeSet/UARPGAttributeSet.h" 
#include "AbilitySystemComponent.h"  
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayTagContainer.h"   

#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"

#include "NiagaraFunctionLibrary.h" 
#include "Camera/PlayerCameraManager.h"

#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"  
#include "Engine/EngineTypes.h"    

#include "TimerManager.h"


UUARPGAnimNotify_MeleeHit::UUARPGAnimNotify_MeleeHit()
{
}



 void UUARPGAnimNotify_MeleeHit::Notify(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, 
	const FAnimNotifyEventReference& EventReference) 
{
	 UE_LOG(LogTemp, Warning, TEXT("[MeleeHit] fired, class=%s"), DamageEffectClass ? *DamageEffectClass->GetName() : TEXT("NONE"));

	 // ==== who is attack
	 if (!MeshComp)
	 {
		 return;
	 }
	AActor* OwnerActor = MeshComp->GetOwner();
	AAARPGCharacterBase* Attacker = Cast<AAARPGCharacterBase>(OwnerActor);
	if (!Attacker) { return; }

	if (!DamageEffectClass)
	{
		return;
	}
	
	UAbilitySystemComponent* AttackerASC = Attacker->GetAbilitySystemComponent();
	if (!AttackerASC)
	{
		return;
	}
	if (AttackerASC->HasMatchingGameplayTag(
		FGameplayTag::RequestGameplayTag(FName("State.Dead"))))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[MeleeHit] attacker is dead, skip"));
		return;
	}
	// ==== how many damage
	bool bFound = false;
	const float AttackPower = UAbilitySystemBlueprintLibrary::GetFloatAttributeFromAbilitySystemComponent(
		AttackerASC,
		UUARPGAttributeSet::GetAttackPowerAttribute(),
		bFound);
		const float DamageValue = bFound ? AttackPower : 20.f;
	// ===== Step 3: Sphere Overlap, Find Targets Within Range 
	const FVector Forward = Attacker->GetActorForwardVector();//the character facing
	const FVector Center = Attacker->GetActorLocation() + Forward * ForwardOffset;

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(Attacker);

	TArray<AActor*> OutActors;
	UKismetSystemLibrary::SphereOverlapActors(
		Attacker,
		Center,
		SphereRadius,
		ObjectTypes,
		AAARPGCharacterBase::StaticClass(),
		ActorsToIgnore,
		OutActors
	);
	UE_LOG(LogTemp, Warning, TEXT("[MeleeHit] hit=%d, dmg=%.1f"), OutActors.Num(), DamageValue);

	// ===== Step 4: Apply damage to each target hit
	for (AActor* HitActor : OutActors)
	{
		AAARPGCharacterBase* Target = Cast< AAARPGCharacterBase>(HitActor);
		
		if (!Target)
		{
			continue;
		}

		UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();
		if (!TargetASC)
		{
			continue;
		}

		FGameplayEffectContextHandle ContextHandle = AttackerASC->MakeEffectContext();
		FGameplayEffectSpecHandle SpecHandle = AttackerASC->MakeOutgoingSpec(
			DamageEffectClass, 1.f, ContextHandle
		);

		if (!SpecHandle.IsValid())
		{
			continue;
		}

		SpecHandle.Data->SetSetByCallerMagnitude(
			FGameplayTag::RequestGameplayTag(FName("SetByCaller.Damage")),
			DamageValue
		);
		AttackerASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data, TargetASC);

		const FVector HitLoc = Target->GetActorLocation() + FVector(0.f, 0.f, 80.f);

		if (HitVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				MeshComp, HitVFX, HitLoc, FRotator::ZeroRotator,
				FVector(1.f), true, true);
		}
		if (HitSound)
		{
			UGameplayStatics::PlaySoundAtLocation(MeshComp, HitSound, HitLoc);
		}
		// ==== 卡肉：双方各顿 0.08 秒 ====
		Target->CustomTimeDilation = 0.05f;
		Attacker->CustomTimeDilation = 0.05f;

		TWeakObjectPtr<AAARPGCharacterBase> WeakTarget = Target;
		TWeakObjectPtr<AAARPGCharacterBase> WeakAttacker = Attacker;

		FTimerHandle HitStopHandle;
		Target->GetWorld()->GetTimerManager().SetTimer(
			HitStopHandle,
			FTimerDelegate::CreateLambda([WeakTarget, WeakAttacker]()
				{
					if (WeakTarget.IsValid()) { WeakTarget->CustomTimeDilation = 1.f; }
					if (WeakAttacker.IsValid()) { WeakAttacker->CustomTimeDilation = 1.f; }
				}),
			0.08f,
			false);
		

	}
	// 改成这样：
	if (OutActors.Num() > 0 && HitShakeClass)
	{
		// 攻击者可能是敌人，它的 Controller 是 AIController，Cast 必然失败。
		// 要从【世界】取玩家控制器 —— 这样"Boss 打玩家"才会震屏。
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(MeshComp, 0))
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StartCameraShake(HitShakeClass, 1.f);
			}
		}
	}
	return ;
}
