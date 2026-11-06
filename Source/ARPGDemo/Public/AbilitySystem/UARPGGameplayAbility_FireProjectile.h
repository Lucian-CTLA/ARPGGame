// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "UARPGGameplayAbility_FireProjectile.generated.h"

class AARPGFireProjectile;

/**
 * P 键远程技能：在手的 socket 上生成一颗火球（AARPGFireProjectile），朝角色正前方飞出去。
 * 冷却不在这里做 —— 放在 AAARPGHeroCharacter 里（Input_Fire 里挡），
 * 这样 HUD 读一个浮点数就能画倒计时，不用去翻 AbilitySpec。
 */
UCLASS()
class ARPGDEMO_API UUARPGGameplayAbility_FireProjectile : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UUARPGGameplayAbility_FireProjectile();

	// 火球类；不填就用 C++ 那个 AARPGFireProjectile
	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	TSubclassOf<AARPGFireProjectile> ProjectileClass;

	// 从哪只手飞出来；骨骼上没这个 socket 就退回角色正前方
	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	FName SpawnSocket = FName("hand_r");

	// 没 socket 时的兜底偏移（前方 / 上方，单位厘米）
	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	float ForwardOffset = 90.f;

	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	float UpOffset = 40.f;

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
