// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimMontage.h"   // UAnimMontage：举盾 / 破防硬直动画
#include "NiagaraSystem.h"           // UNiagaraSystem：护盾特效资产（NS_Shield_1）
#include "NiagaraComponent.h"        // UNiagaraComponent：运行时生成的那个特效实例
#include "Sound/SoundBase.h"         // USoundBase：举盾 / 破防音效
#include "GameplayEffect.h"          // UGameplayEffect：TSubclassOf 要求完整定义，不能只前置声明
#include "ActiveGameplayEffectHandle.h"   // FActiveGameplayEffectHandle：用来精确移除自己那一份持续效果
#include "UARPGGameplayAbility_Block.generated.h"

/**
 * 格挡技能
 * 激活：挂 State.Blocking -> 伤害结算（PostGameplayEffectExecute）查到它就减伤 + 扣体力
 * 结束：EndAbility 里统一撤 Tag（松手 / 被打断 / 破防 / 死亡都经过这里）
 * 注意：激活后不自动结束，一直保持激活，等外部松手来 Cancel
 *
 * 破防流程：
 *   体力不够 -> AttributeSet 挂 State.BlockBroken 并 Cancel 掉本技能
 *   -> 本技能 EndAbility 读到 BlockBroken -> 挂 State.Stunned + 播硬直动画 -> 到时间撤 Stunned
 */
UCLASS()
class ARPGDEMO_API UUARPGGameplayAbility_Block : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UUARPGGameplayAbility_Block();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	TObjectPtr<UAnimMontage> BlockMontage;     // 举盾动画（要在 montage 里把 section 设成循环，才能一直举着）

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	TObjectPtr<UAnimMontage> StaggerMontage;   // 破防硬直动画，不配就只上 Tag 不播动画

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	float StaggerDuration = 0.6f;              // 硬直持续多久（秒）；动画比它长的话以此为准

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	TObjectPtr<UNiagaraSystem> ShieldEffectSystem;   // 举盾时罩在身上的护盾特效，拖 NS_Shield_1

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	FName ShieldAttachSocket = NAME_None;            // 挂到哪根骨骼，比如 spine_03；留空 = 挂 Mesh 根部（脚底）

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	FVector ShieldEffectOffset = FVector(0.f, 0.f, 90.f);   // 相对挂载点的偏移，Z=90 大约是角色腰/胸高度

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	FVector ShieldEffectScale = FVector(1.f);        // 特效大小；太大就调小，比如 0.6

	// 举盾的"维持成本"：不配的话举着盾站着不动是零消耗，体力回复会一直把它补满，
	// 结果就是永远挡得住 = 无敌。配 GE_BlockDrain（Infinite + Period，每周期扣体力）即可
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	TSubclassOf<UGameplayEffect> BlockDrainEffectClass;

	// 举盾起手音：和护盾特效是同一件事，建议 MagicShieldActivate
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	TObjectPtr<USoundBase> BlockStartSound;

	// 破防音：只在"体力耗尽被打崩"时响，正常松手不响，建议 SC_BlockedMelee
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	TObjectPtr<USoundBase> GuardBreakSound;

protected:
	// 激活：挂格挡 Tag
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData
	) override;

	// 结束：唯一撤 Tag 的地方，同时处理破防硬直
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled
	) override;

private:
	// 硬直计时到了：撤掉 State.Stunned
	void OnStaggerEnd();

	FTimerHandle StaggerTimerHandle;

	// 运行时生成的护盾特效实例。必须是 UPROPERTY，否则可能被 GC 回收导致特效莫名消失
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ShieldEffectComp = nullptr;

	// 持续耗体力那份 GE 的句柄。必须存下来：EndAbility 里要靠它精确移除"我这一份"，
	// 用 RemoveActiveGameplayEffectBySourceEffect 会把别人施加的同名效果一起误删
	FActiveGameplayEffectHandle DrainEffectHandle;
};
