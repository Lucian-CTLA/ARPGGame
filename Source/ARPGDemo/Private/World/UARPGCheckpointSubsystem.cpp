// Fill out your copyright notice in the Description page of Project Settings.

#include "World/UARPGCheckpointSubsystem.h"
#include "Engine/World.h"

bool UARPGCheckpointSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Outer 就是这个世界；蓝图预览、材质预览这类世界不需要存档点
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

void UARPGCheckpointSubsystem::SetCheckpoint(FName InId, const FTransform& InRespawnTransform)
{
	bHasCheckpoint = true;
	CheckpointId = InId;
	RespawnTransform = InRespawnTransform;
}

void UARPGCheckpointSubsystem::ClearCheckpoint()
{
	bHasCheckpoint = false;
	CheckpointId = NAME_None;
	RespawnTransform = FTransform::Identity;
}
