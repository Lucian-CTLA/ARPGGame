// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UARPGCombatStateSubsystem.generated.h"

/**
 * 关卡级的战斗进度记录（每个关卡一份，切换关卡自动清空）。
 *
 * 存在的理由：StoneGolem 要"打完 Boss 才能召唤"，但 Boss 死亡后 3.2 秒就被
 * SetLifeSpan 销毁了，之后再去场景里找 BP_Enemy_Boss 会找不到、也分不清
 * 是"还没打"还是"已经打完了"。所以让 Boss 临死前在这里打一个标记，
 * 触发器只查这个标记，跟 Actor 生灭解耦。
 */
UCLASS()
class ARPGDEMO_API UARPGCombatStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 编辑器预览世界不需要这套状态，只给真正运行的关卡建实例
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// Boss 死亡时由 AAARPGEnemyCharacter::HandleDeath 调用（bIsBoss 为真的才会调）
	UFUNCTION(BlueprintCallable, Category = "Combat|State")
	void NotifyBossDefeated();

	UFUNCTION(BlueprintPure, Category = "Combat|State")
	bool IsBossDefeated() const { return bBossDefeated; }

	// 石巨人已经召唤过一次了（防止反复点灯刷出一堆）
	UFUNCTION(BlueprintCallable, Category = "Combat|State")
	void NotifyStoneGolemSpawned();

	UFUNCTION(BlueprintPure, Category = "Combat|State")
	bool IsStoneGolemSpawned() const { return bStoneGolemSpawned; }

	// 调试用：把两个标记都清掉，方便反复测试
	UFUNCTION(BlueprintCallable, Category = "Combat|State")
	void ResetCombatState();

private:
	bool bBossDefeated = false;
	bool bStoneGolemSpawned = false;
};
