// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UARPGCheckpointSubsystem.generated.h"

/**
 * 关卡级的存档点记录（每个关卡一份，切关卡自动清空）。
 *
 * 为什么挂在 WorldSubsystem 而不是 Player 身上：玩家死亡后 Pawn 会被销毁，
 * 存在 Pawn 成员里的存档坐标跟着一起没了，复活时就没得读了。
 * 这和 UARPGCombatStateSubsystem 记 Boss 标记是同一个思路 —— 让数据活得比 Actor 久。
 *
 * 只存"最近一个"存档点：踩第二个点会覆盖第一个，符合"最近的存档点"这个需求。
 */
UCLASS()
class ARPGDEMO_API UARPGCheckpointSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 编辑器预览世界不需要这套状态，只给真正运行的关卡建实例
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// 存档点被玩家踩到时调用；后来的会覆盖先来的
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	void SetCheckpoint(FName InId, const FTransform& InRespawnTransform);

	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	bool HasCheckpoint() const { return bHasCheckpoint; }

	// 复活位置；没存过点时返回 Identity，调用方应该先问 HasCheckpoint()
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	FTransform GetRespawnTransform() const { return RespawnTransform; }

	// 存的是哪个点，只在日志/调试里用得上
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	FName GetCheckpointId() const { return CheckpointId; }

	// GameMode 复活完可以清掉，想连续死两次还回同一个点就别调
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	void ClearCheckpoint();

private:
	bool bHasCheckpoint = false;
	FName CheckpointId = NAME_None;
	FTransform RespawnTransform = FTransform::Identity;
};
