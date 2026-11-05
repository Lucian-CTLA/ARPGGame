// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/UARPGGameplayAbility_Block.h"
#include "AbilitySystemComponent.h"          // AddLooseGameplayTag / RemoveLooseGameplayTag / CancelAbilities
#include "GameFramework/Character.h"         // ACharacter：PlayAnimMontage / GetWorldTimerManager / GetMesh
#include "TimerManager.h"                    // FTimerDelegate / FTimerHandle
#include "NiagaraFunctionLibrary.h"          // UNiagaraFunctionLibrary::SpawnSystemAttached
#include "NiagaraComponentPoolMethodEnum.h"  // ENCPoolMethod：NiagaraComponent.h 里只有前向声明，用 None 必须有完整定义
#include "Kismet/GameplayStatics.h"          // PlaySoundAtLocation

// 小工具：少写一堆 FGameplayTag::RequestGameplayTag(FName(...))
static FGameplayTag BlockTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name));
}

UUARPGGameplayAbility_Block::UUARPGGameplayAbility_Block()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;              // 每个角色一份实例
	AbilityTags.AddTag(BlockTag(TEXT("Ability.Block")));                                  // 身份标签：别人靠它取消我
	ActivationBlockedTags.AddTag(BlockTag(TEXT("State.Rolling")));                        // 翻滚中举不了盾
	ActivationBlockedTags.AddTag(BlockTag(TEXT("State.Stunned")));                        // 破防硬直中举不了盾

	// 举着盾不能砍人。它匹配的是对方技能身上的 AbilityTags，不是 Loose Tag ——
	// 所以必须在 GA_LightAttack 的 Class Defaults → Ability Tags 里也挂上 Ability.Attack.Light，
	// 两边对上了才生效（举盾还会顺带取消正在进行的攻击）
	BlockAbilitiesWithTag.AddTag(BlockTag(TEXT("Ability.Attack.Light")));
}

void UUARPGGameplayAbility_Block::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 挂 Tag；这里不调 EndAbility，技能保持激活，等松手时外部来 Cancel
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(BlockTag(TEXT("State.Blocking")));

		// 持续耗体力：Infinite + Period 的 GE，举着盾每周期扣一点。
		// 没有它的话体力只在"被打中那一瞬"扣，站着不动还能回满 → 无敌
		if (BlockDrainEffectClass)
		{
			DrainEffectHandle = ASC->ApplyGameplayEffectToSelf(
				BlockDrainEffectClass.GetDefaultObject(),
				1.f,
				ASC->MakeEffectContext()
			);
		}
	}

	// 播举盾动画：直接用 PlayAnimMontage，不用 UAbilityTask_PlayMontageAndWait——
	// 那个 Task 是"播完回调 -> 结束技能"的模型，而格挡是按住型，循环 montage 永远不会播完
	if (ACharacter* Avatar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (BlockMontage)
		{
			Avatar->PlayAnimMontage(BlockMontage);
		}

		// 护盾特效：挂在角色 Mesh 上，人物走到哪它跟到哪
		// bAutoDestroy 传 false：举盾要持续到松手，不能让它播完就自己销毁
		if (ShieldEffectSystem)
		{
			ShieldEffectComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
				ShieldEffectSystem,
				Avatar->GetMesh(),
				ShieldAttachSocket,          // 留空 = 挂 Mesh 根部（脚底），所以要用 Offset 抬到身体中心
				ShieldEffectOffset,
				FRotator::ZeroRotator,
				ShieldEffectScale,
				EAttachLocation::KeepRelativeOffset,
				false,
				ENCPoolMethod::None
			);
		}

		// 举盾起手音：和护盾特效同时出声，"看到盾 + 听到盾"才算一次完整的格挡起手
		if (BlockStartSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, BlockStartSound, Avatar->GetActorLocation());
		}
	}
}

void UUARPGGameplayAbility_Block::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	// 撤 Tag 只放这里：松手 / 被打断 / 破防 / 死亡 Cancel，几条路都经过 EndAbility
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC) { return; }

	ASC->RemoveLooseGameplayTag(BlockTag(TEXT("State.Blocking")));

	// 停掉持续耗体力。放在破防分支之前：体力已经见底了，多扣一帧毫无意义
	if (DrainEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(DrainEffectHandle);
		DrainEffectHandle.Invalidate();   // 清掉句柄，避免下次 EndAbility 重复移除
	}

	// 放下盾：停掉举盾动画。必须放在破防分支之前，
	// 否则循环中的举盾动画会和硬直动画抢同一个 DefaultSlot
	if (ACharacter* Avatar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (BlockMontage)
		{
			Avatar->StopAnimMontage(BlockMontage);
		}
	}

	// 收掉护盾特效：先 Deactivate 停发射，再 DestroyComponent，
	// 直接销毁的话可能残留一帧粒子
	if (ShieldEffectComp)
	{
		ShieldEffectComp->Deactivate();
		ShieldEffectComp->DestroyComponent();
		ShieldEffectComp = nullptr;
	}

	// 破防导致的结束（不是正常松手）：上硬直
	if (ASC->HasMatchingGameplayTag(BlockTag(TEXT("State.BlockBroken"))))
	{
		ASC->AddLooseGameplayTag(BlockTag(TEXT("State.Stunned")));

		if (ACharacter* Avatar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
		{
			// 破防音：只在这个分支里响，正常松手不响
			if (GuardBreakSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, GuardBreakSound, Avatar->GetActorLocation());
			}

			if (StaggerMontage)
			{
				Avatar->PlayAnimMontage(StaggerMontage);
			}

			// 计时撤硬直；挂角色的 TimerManager，角色销毁时定时器自动失效
			FTimerDelegate Delegate;
			Delegate.BindUObject(this, &ThisClass::OnStaggerEnd);
			Avatar->GetWorldTimerManager().SetTimer(StaggerTimerHandle, Delegate, StaggerDuration, false);
		}
		else
		{
			// 拿不到角色就别挂硬直，否则 Tag 没人撤、角色永久卡住
			ASC->RemoveLooseGameplayTag(BlockTag(TEXT("State.Stunned")));
		}

		ASC->RemoveLooseGameplayTag(BlockTag(TEXT("State.BlockBroken")));   // 一次性标记，用完即清
	}
}

void UUARPGGameplayAbility_Block::OnStaggerEnd()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(BlockTag(TEXT("State.Stunned")));
	}
}
