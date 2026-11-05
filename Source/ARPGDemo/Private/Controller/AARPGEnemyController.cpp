// Fill out your copyright notice in the Description page of Project Settings.

#include "Controller/AARPGEnemyController.h"    // 带子目录名

AAARPGEnemyController::AAARPGEnemyController()  // 构造函数
{
	
}

void AAARPGEnemyController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);                    // 注意是 Super 不是 Supper；Super 指父类 AAIController，这一步让基类先完成接管流程

	UE_LOG(LogTemp, Warning, TEXT("[AI] OnPossess: pawn=%s, BT=%s"),   // 打一行日志：接管了哪个角色、行为树配上了没有；这行是排查"敌人为什么不动"的第一道防线
		*GetNameSafe(InPawn),                    // 被接管的角色名字
		BehaviorTreeAsset ? *BehaviorTreeAsset->GetName() : TEXT("NULL"));   // 配了就打印行为树资产名，没配就打印 NULL

	if (BehaviorTreeAsset)                       // 指定了行为树才跑；没指定就不跑，避免在蓝图里漏配时出问题
	{
		RunBehaviorTree(BehaviorTreeAsset);      // 启动行为树；这一行才是敌人"活过来"的开关
	}
}
