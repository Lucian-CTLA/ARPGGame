// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/BTTask_ARPGWander.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"   // EPathFollowingStatus

UBTTask_ARPGWander::UBTTask_ARPGWander()
{
	NodeName = TEXT("ARPG Wander");

	// 不打开这个，TickTask 永远不被调用，任务会一直卡在 InProgress，敌人这辈子不再动
	bNotifyTick = true;
}

void UBTTask_ARPGWander::EnterIdle(FBTARPGWanderMemory* Memory)
{
	Memory->bMoving = false;
	Memory->TimeLeft = FMath::FRandRange(IdleMin, IdleMax);
}

EBTNodeResult::Type UBTTask_ARPGWander::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTARPGWanderMemory* Memory = CastInstanceNodeMemory<FBTARPGWanderMemory>(NodeMemory);

	// 刚进分支先站一会儿再走，不然玩家一跑远敌人立刻窜出去，像被弹了一下
	EnterIdle(Memory);

	// 必须是 InProgress：返回 Succeeded 的话行为树下一帧会重入本节点，等于每帧挑点
	return EBTNodeResult::InProgress;
}

void UBTTask_ARPGWander::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTARPGWanderMemory* Memory = CastInstanceNodeMemory<FBTARPGWanderMemory>(NodeMemory);

	AAIController* AI = OwnerComp.GetAIOwner();
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	if (!AI || !Pawn)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	Memory->TimeLeft -= DeltaSeconds;

	// ---- 阶段一：发愣 ----
	if (!Memory->bMoving)
	{
		if (Memory->TimeLeft > 0.f)
		{
			return;
		}

		if (StartMove(Pawn, AI))
		{
			Memory->bMoving = true;
			Memory->TimeLeft = MoveTimeout;   // 换成本次移动的时间预算
		}
		else
		{
			// 挑不到点（NavMesh 没盖到 / 这块地被围住），1 秒后重试。
			// 不能 return Failed：Failed 会让行为树立刻重新 tick 本节点，日志瞬间刷屏
			Memory->TimeLeft = 1.f;
		}
		return;
	}

	// ---- 阶段二：走路 ----
	const EPathFollowingStatus::Type Status = AI->GetMoveStatus();

	// 只有 Idle 才是"结束了"（到点 / 走不通 / 被 StopMovement 掐掉）。
	// Waiting 表示路径还在异步计算，把它当结束会导致刚出发就原地发愣
	if (Status == EPathFollowingStatus::Idle)
	{
		EnterIdle(Memory);
		if (bDebugWander)
		{
			UE_LOG(LogTemp, Log, TEXT("[Wander] %s 到点，歇 %.1f 秒"),
				*Pawn->GetName(), Memory->TimeLeft);
		}
		return;
	}

	// 还在走，但预算用完了：多半是目标点绕不过去、或者被别的怪堵住，放弃这个点
	if (Memory->TimeLeft <= 0.f)
	{
		AI->StopMovement();
		EnterIdle(Memory);
		if (bDebugWander)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Wander] %s 超时没走到，换点"), *Pawn->GetName());
		}
	}
}

bool UBTTask_ARPGWander::StartMove(APawn* Pawn, AAIController* AI)
{
	UWorld* World = Pawn->GetWorld();
	UNavigationSystemV1* NavSys = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)
		: nullptr;
	if (!NavSys)
	{
		if (bDebugWander)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Wander] %s 拿不到 NavigationSystem：查 Build.cs 的 NavigationSystem 模块和关卡里的 NavMeshBoundsVolume"),
				*Pawn->GetName());
		}
		return false;
	}

	// 起点必须是脚底：AAARPGEnemyCharacter 重写过 GetNavAgentLocation()。
	// 石巨人胶囊中心离地两三百厘米，超出导航系统默认 250cm 的垂直投影容差，查询一律失败
	const FVector Origin = Pawn->GetNavAgentLocation();

	// 优先"随机可达点"：后者（GetRandomPointInNavigableRadius）只保证能站人，
	// 不保证从当前位置走得过去，容易挑到墙另一侧，敌人会一直撞墙
	FNavLocation Point;
	bool bFound = NavSys->GetRandomReachablePointInRadius(Origin, WanderRadius, Point);
	if (!bFound)
	{
		bFound = NavSys->GetRandomPointInNavigableRadius(Origin, WanderRadius, Point);
	}
	if (!bFound)
	{
		if (bDebugWander)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Wander] %s 半径 %.0fcm 内挑不到点（脚下=%s）"),
				*Pawn->GetName(), WanderRadius, *Origin.ToCompactString());
		}
		return false;
	}

	const EPathFollowingRequestResult::Type Result =
		AI->MoveToLocation(Point.Location, AcceptanceRadius);

	// 0=Failed / 1=AlreadyAtGoal / 2=RequestSuccessful。
	// AlreadyAtGoal 也算发出去了：下一帧状态就是 Idle，自然回到发愣
	if (Result == EPathFollowingRequestResult::Failed)
	{
		if (bDebugWander)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Wander] %s 挑的点寻路失败，换点"), *Pawn->GetName());
		}
		return false;
	}

	if (bDebugWander)
	{
		UE_LOG(LogTemp, Log, TEXT("[Wander] %s 挑点 %s（离 %.0fcm）"),
			*Pawn->GetName(), *Point.Location.ToCompactString(),
			FVector::Dist2D(Origin, Point.Location));
	}
	return true;
}

EBTNodeResult::Type UBTTask_ARPGWander::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AAIController* AI = OwnerComp.GetAIOwner())
	{
		// 玩家进入警戒范围、Decorator 判定 false 时行为树会 abort 本节点。
		// 这里必须收脚，否则敌人会带着上一段路径继续往前滑，追击还没开始人已经飘过去了
		AI->StopMovement();
	}

	EnterIdle(CastInstanceNodeMemory<FBTARPGWanderMemory>(NodeMemory));
	return EBTNodeResult::Aborted;
}
