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
	UFUNCTION(BlueprintCallable, Category = "Respawn")
	void RespawnHero();
};
