// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/AARPGGameMode.h"

void AAARPGGameMode::RespawnHero()
{
	RestartGame();   // AGameMode 自带：重新加载当前关卡，玩家和敌人全部重置
}
