// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AARPGCharacterBase.h"
#include "Abilities/GameplayAbility.h"
#include "Components/WidgetComponent.h"
#include "UI/UARPGHUDWidget.h"
#include "AARPGEnemyCharacter.generated.h"

UCLASS()
class ARPGDEMO_API AAARPGEnemyCharacter : public AAARPGCharacterBase
{
	GENERATED_BODY()

public:
	AAARPGEnemyCharacter();
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> AttackAbilityClass;// 敌人要用的攻击技能，在蓝图 Class Defaults 里配

	virtual void OnHealthChanged(const FOnAttributeChangeData& ChangeData) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UWidgetComponent> HealthBarComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Stats")
	TSubclassOf<UGameplayEffect> InitStatsEffect;

	// 血条挂点高度：Grunt 配 120，Boss 配 300 左右，两个蓝图各填各的
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	float HealthBarHeight = 120.f;

	// 受伤后多久把血条收起来
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	float HealthBarHideDelay = 3.f;

	FTimerHandle HealthBarHideTimer;

	void RefreshHealthBar();          // 读当前血量刷一次百分比
	void ShowHealthBarTemporarily();  // 显示并重开 3 秒计时
	void HideHealthBar();             // 计时到点后隐藏

	virtual void HandleDeath() override;
};
