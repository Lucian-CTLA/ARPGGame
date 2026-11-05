// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/UARPGGameplayAbility_Roll.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"   // 播蒙太奇并等待的 AbilityTask
#include "AbilitySystemComponent.h"                          // AddLooseGameplayTag / RemoveLooseGameplayTag
#include "GameFramework/Character.h"                         // ACharacter：LaunchCharacter / GetLastMovementInputVector
#include "Kismet/GameplayStatics.h"                          // PlaySoundAtLocation

UUARPGGameplayAbility_Roll::UUARPGGameplayAbility_Roll()
{
	// 每个角色一份技能实例，技能里才能有自己的成员变量（bFinished）
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 翻滚时打断举盾：这里匹配的是对方技能的 AbilityTags（GA_Block 上写着 Ability.Block），
	// 不是 Loose Tag —— 写 State.Blocking 是无效的
	CancelAbilitiesWithTag.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Block")));

	// 破防硬直期间翻滚不了（State.Stunned 由 GA_Block 破防时挂上，到时间自动撤）
	ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Stunned")));
}

void UUARPGGameplayAbility_Roll::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	bFinished = false;                                        // 每次激活都重置，否则第二次翻滚会被上一次的 true 挡住

	if (!RollMontage) { FinishRoll(); return; }               // 没配蒙太奇：直接收尾，别把技能卡在激活状态
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo)) { FinishRoll(); return; }   // 查消耗 + 扣消耗；返回 false 必须自己收尾

	// 翻滚音：放在 CommitAbility 之后 —— 消耗没过就别响，否则会出现"空气翻滚"也有声音
	if (RollSound)
	{
		if (AActor* Avatar = GetAvatarActorFromActorInfo())
		{
			UGameplayStatics::PlaySoundAtLocation(this, RollSound, Avatar->GetActorLocation());
		}
	}

	// 无敌：Tag 挂在 ASC 上，伤害结算时（PostGameplayEffectExecute）随时查得到
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Rolling")));
	}

	// 位移：沿最后一次移动输入方向；玩家站着按就朝角色正面
	if (ACharacter* Avatar = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		FVector Dir = Avatar->GetLastMovementInputVector();
		if (Dir.IsNearlyZero()) { Dir = Avatar->GetActorForwardVector(); }
		Dir.Z = 0.f;                                          // 清掉竖直分量，否则会往上窜
		Dir.Normalize();
		Avatar->LaunchCharacter(Dir * RollSpeed, true, true);  // 两个 true：XY 和 Z 都用新速度覆盖原值
	}

	// 播放蒙太奇，写法和 LightAttack 一致
	UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, RollMontage, 1.f, NAME_None);
	if (!Task) { FinishRoll(); return; }

	// 左边是 Task 给的委托（OnInterrupted），右边是自己的回调函数（OnMontageInterrupted），两边名字不一样
	Task->OnCompleted.AddDynamic(this, &UUARPGGameplayAbility_Roll::OnMontageCompleted);
	Task->OnInterrupted.AddDynamic(this, &UUARPGGameplayAbility_Roll::OnMontageInterrupted);
	Task->OnBlendOut.AddDynamic(this, &UUARPGGameplayAbility_Roll::OnMontageBlendOut);
	Task->OnCancelled.AddDynamic(this, &UUARPGGameplayAbility_Roll::OnMontageCancelled);
	Task->ReadyForActivation();                               // 不调用的话什么都不播
}

void UUARPGGameplayAbility_Roll::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

	// 撤 Tag 只放在这里：播完 / 打断 / 混合出 / 取消 / 死亡时 CancelAbilities，五条路都经过 EndAbility
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Rolling")));
	}
}

void UUARPGGameplayAbility_Roll::OnMontageCompleted()
{
	FinishRoll();
}

void UUARPGGameplayAbility_Roll::OnMontageInterrupted()
{
	FinishRoll();
}

void UUARPGGameplayAbility_Roll::OnMontageBlendOut()
{
	FinishRoll();
}

void UUARPGGameplayAbility_Roll::OnMontageCancelled()
{
	FinishRoll();
}

void UUARPGGameplayAbility_Roll::FinishRoll()
{
	if (bFinished) return;                                    // 四个回调都会来，只认第一次
	bFinished = true;

	// GA 基类自己存着这次激活的上下文，不用像 LightAttack 那样再缓存一份
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
