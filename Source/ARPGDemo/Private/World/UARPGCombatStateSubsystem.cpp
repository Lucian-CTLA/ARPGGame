// Fill out your copyright notice in the Description page of Project Settings.

#include "World/UARPGCombatStateSubsystem.h"
#include "Engine/World.h"

bool UARPGCombatStateSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Outer 就是这个世界；编辑器里的预览世界（蓝图预览、材质预览）不需要这套状态
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UARPGCombatStateSubsystem::NotifyBossDefeated()
{
	if (bBossDefeated)
	{
		return;
	}
	bBossDefeated = true;
	UE_LOG(LogTemp, Warning, TEXT("[Combat] Boss defeated -> StoneGolem trigger unlocked"));
}

void UARPGCombatStateSubsystem::NotifyStoneGolemSpawned()
{
	if (bStoneGolemSpawned)
	{
		return;
	}
	bStoneGolemSpawned = true;
	UE_LOG(LogTemp, Warning, TEXT("[Combat] StoneGolem spawned"));
}

void UARPGCombatStateSubsystem::ResetCombatState()
{
	bBossDefeated = false;
	bStoneGolemSpawned = false;
}
