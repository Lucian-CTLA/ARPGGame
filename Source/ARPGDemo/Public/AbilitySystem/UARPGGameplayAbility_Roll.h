// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimMontage.h"
#include "Sound/SoundBase.h"
#include "UARPGGameplayAbility_Roll.generated.h"

/**
 * 翻滚技能
 * 激活：挂 State.Rolling 无敌 Tag -> 沿输入方向位移 -> 播蒙太奇
 * 结束：EndAbility 里统一撤销 Tag（所有退出路径都经过这里）
 */
UCLASS()
class ARPGDEMO_API UUARPGGameplayAbility_Roll : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UUARPGGameplayAbility_Roll();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roll")
	TObjectPtr<UAnimMontage> RollMontage;      // 在 GA_Roll 蓝图里选 AM_Roll

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roll")
	float RollSpeed = 900.f;                   // 位移速度，单位厘米/秒；900 约 0.35 秒窜 3 米

	// 翻滚音效（建议 SC_Character_Effort：发力闷哼，比挥空风更像"使劲滚一下"）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Roll")
	TObjectPtr<USoundBase> RollSound;

protected:
	// 技能被激活时引擎调用：挂 Tag、位移、播蒙太奇
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData
	) override;

	// 技能结束时引擎调用：唯一撤销无敌 Tag 的地方
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled
	) override;

	// 四条退出路径，全部收尾到 FinishRoll
	UFUNCTION() void OnMontageCompleted();
	UFUNCTION() void OnMontageInterrupted();
	UFUNCTION() void OnMontageBlendOut();
	UFUNCTION() void OnMontageCancelled();

private:
	void FinishRoll();          // 统一收尾：保证只结束一次
	bool bFinished = false;
};
