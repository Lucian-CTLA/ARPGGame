// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/AARPGGameMode.h"
#include "World/UARPGCheckpointSubsystem.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

void AAARPGGameMode::RespawnHero()
{
	RestartGame();   // AGameMode 自带：重新加载当前关卡，玩家和敌人全部重置
}

void AAARPGGameMode::RespawnAtCheckpoint()
{
	UWorld* World = GetWorld();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);

	UARPGCheckpointSubsystem* CheckpointState = World
		? World->GetSubsystem<UARPGCheckpointSubsystem>()
		: nullptr;

	// 没存过档（或者子系统取不到）就退回重开一局，别让按钮点了没反应
	if (!PC || !CheckpointState || !CheckpointState->HasCheckpoint())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Checkpoint] 没有可用存档点，改为重开关卡"));
		RespawnHero();
		return;
	}

	const FTransform SpawnTransform = CheckpointState->GetRespawnTransform();

	// ---- 这一步是整个复活流程里最容易踩的坑 ----
	// AGameModeBase::RestartPlayerAtTransform 在 Controller 手上已经有 Pawn 时
	// 不会重新生成，只是拿旧 Pawn 的朝向继续走（GameModeBase.cpp:1347），
	// 最后 Possess 的还是那具尸体。
	// 而玩家死亡时是 SetLifeSpan(0.f) 特意留着尸体的，所以必须先拆掉再重造。
	if (APawn* OldPawn = PC->GetPawn())
	{
		PC->UnPossess();     // 先断开，否则 Destroy 时引擎会再走一遍死亡/脱离逻辑
		OldPawn->Destroy();
	}

	// 新 Pawn 会在存档点位置生成、自动 Possess；
	// BeginPlay 里会重新给技能、重建 HUD，AttributeSet 的构造函数把血量初始化成满值，
	// 所以"回满血"这件事不用额外写代码
	RestartPlayerAtTransform(PC, SpawnTransform);

	// 尸体上挂着输入模式是 UIOnly + 鼠标，恢复成游戏模式，否则复活后动不了
	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;

	UE_LOG(LogTemp, Warning, TEXT("[Checkpoint] 已在 %s 复活"),
		*CheckpointState->GetCheckpointId().ToString());
}
