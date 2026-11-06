// Fill out your copyright notice in the Description page of Project Settings.

#include "AbilitySystem/UARPGGameplayAbility_FireProjectile.h"

#include "Combat/AARPGFireProjectile.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"        // PlaySlotAnimationAsDynamicMontage
#include "Animation/AnimSequenceBase.h"   // UAnimSequenceBase
#include "Engine/World.h"

UUARPGGameplayAbility_FireProjectile::UUARPGGameplayAbility_FireProjectile()
{
	// 每个角色一份技能实例：以后要往实例里塞状态（比如"这次打出去几颗"）不会互相串
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	ProjectileClass = AARPGFireProjectile::StaticClass();
}

void UUARPGGameplayAbility_FireProjectile::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 先做常规检查（消耗、冷却 GE 这些）——这里没有配消耗，等于只做基础合法性检查
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ACharacter* Avatar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UWorld* World = GetWorld();
	if (!Avatar || !World || !ProjectileClass)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ---- 顺便播英雄的第三个攻击动作 ----
	// 用「动态蒙太奇」直接播 Animation Sequence，好处是不用再去建一个 Montage 资产：
	// PlaySlotAnimationAsDynamicMontage 会在运行时临时包一个 montage 塞进 Slot 节点。
	// SlotNodeName 必须和 ABP_Hero 里动画图那个 Slot 节点的名字一致，这里就是 DefaultSlot
	// （名字写错不会报错，只是动作不出来，所以改之前先去 ABP_Hero 里看一眼）。
	if (USkeletalMeshComponent* AttackMesh = Avatar->GetMesh())
	{
		if (UAnimInstance* AnimInst = AttackMesh->GetAnimInstance())
		{
			// 静态缓存路径对象：路径只拼一次，不用每次按 P 都构造一遍
			static const FSoftObjectPath ThirdAttackPath(
				TEXT("/Game/Anim/Hero_Axe_LightAttack_1/Hero_Axe_LightAttack_3.Hero_Axe_LightAttack_3"));

			// 想换成别的动作就把上面这行路径改掉，或者换成 AM_Axe_Combo 再配合 JumpToSection
			if (UAnimSequenceBase* ThirdAttack = Cast<UAnimSequenceBase>(ThirdAttackPath.TryLoad()))
			{
				AnimInst->PlaySlotAnimationAsDynamicMontage(
					ThirdAttack,
					FName(TEXT("DefaultSlot")),
					0.08f,   // BlendInTime：从当前姿势淡入的时长，短一点出手更利落
					0.15f,   // BlendOutTime：结束淡出时长
					1.f,     // InPlayRate：播放倍速
					1);      // LoopCount：只播一次
			}
		}
	}

	// 出手位置：优先手上 socket，拿不到就角色正前方偏上
	FVector SpawnLocation = Avatar->GetActorLocation()
		+ Avatar->GetActorForwardVector() * ForwardOffset
		+ FVector(0.f, 0.f, UpOffset);

	if (USkeletalMeshComponent* Mesh = Avatar->GetMesh())
	{
		if (Mesh->DoesSocketExist(SpawnSocket))
		{
			SpawnLocation = Mesh->GetSocketLocation(SpawnSocket);
		}
	}

	const FRotator SpawnRotation = Avatar->GetActorRotation();

	FActorSpawnParameters Params;
	Params.Owner = Avatar;                 // 火球靠 Owner 找施法者，读它的 AttackPower
	Params.Instigator = Avatar;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	World->SpawnActor<AARPGFireProjectile>(ProjectileClass, SpawnLocation, SpawnRotation, Params);

	// 这颗火球发完就结束技能，冷却由角色那边按时间戳挡，所以这里立刻 EndAbility
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
