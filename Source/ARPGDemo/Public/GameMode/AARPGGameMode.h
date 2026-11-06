// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "AARPGGameMode.generated.h"

/**
 * 
 */
UCLASS()
class ARPGDEMO_API AAARPGGameMode : public AGameMode
{
	GENERATED_BODY()
public:
	// 重新开始：整关重载，敌人、存档点、Boss 标记全部回到初始状态
	UFUNCTION(BlueprintCallable, Category = "Respawn")
	void RespawnHero();

	// 从最近的存档点复活：只重造玩家 Pawn，场景原样保留（死掉的敌人不会回来）
	// 没有存过档时自动退回 RespawnHero()，保证按钮点了永远有反应
	UFUNCTION(BlueprintCallable, Category = "Respawn")
	void RespawnAtCheckpoint();
};
