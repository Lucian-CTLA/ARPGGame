// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ARPGWander.generated.h"

class AAIController;
class APawn;

/** 每个 BT 实例各自的游荡状态。BT 节点对象是所有敌人共享的，状态必须放 NodeMemory 里 */
struct FBTARPGWanderMemory
{
	/** 发愣时 = 还要站多久；走路时 = 这个点最多还能走多久 */
	float TimeLeft = 0.f;

	/** true = 已经发出 MoveTo，正在等它结束 */
	bool bMoving = false;
};

/**
 * 脱战游荡任务。
 *
 * 为什么要有它：行为树自己的 MoveTo 和 C++ 定时器都在用同一个 PathFollowingComponent，
 * 行为树一旦 abort 就会 StopMovement()，把另一头发出去的路径当场清掉 ——
 * 现象就是"敌人动一下就停"。所以移动权只能有一个主人，游荡必须做成 BT 任务。
 *
 * 一段循环：发愣（IdleMin~IdleMax 随机）→ 挑一个直径 2*WanderRadius 内的可达点 → 走过去 → 回到发愣。
 * 全程 InProgress，只有节点被打断（玩家进入警戒范围、Decorator 判定 false）才结束。
 */
UCLASS()
class ARPGDEMO_API UBTTask_ARPGWander : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_ARPGWander();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// 不覆写的话 NodeMemory 是 0 字节，往里写直接崩
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTARPGWanderMemory); }

	// 挑点多远（厘米）。别超过 NavMeshBoundsVolume 覆盖的范围，否则一半的查询都失败
	UPROPERTY(EditDefaultsOnly, Category = "Wander", meta = (ClampMin = "100"))
	float WanderRadius = 700.f;

	// 离目标点多近算"到了"。太小会绕着点转圈，太大看起来像没走到就停
	UPROPERTY(EditDefaultsOnly, Category = "Wander", meta = (ClampMin = "10"))
	float AcceptanceRadius = 60.f;

	// 走到一个点之后停多久再挑下一个，实际取这两者之间的随机值
	UPROPERTY(EditDefaultsOnly, Category = "Wander")
	float IdleMin = 2.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Wander")
	float IdleMax = 5.5f;

	// 一个点最多走多久。卡在障碍后面的兜底：到点超时就直接放弃、换下一个点，
	// 否则敌人会顶着一个走不到的目标原地磨一辈子
	UPROPERTY(EditDefaultsOnly, Category = "Wander", meta = (ClampMin = "1"))
	float MoveTimeout = 12.f;

	// 打日志：挑了什么点、为什么挑不到、有没有超时。排查完可以在资产里关掉
	UPROPERTY(EditDefaultsOnly, Category = "Wander")
	bool bDebugWander = true;

private:
	/** 挑点并发出 MoveTo。发出去了返回 true，成功与否靠后面的 GetMoveStatus 判 */
	bool StartMove(APawn* Pawn, AAIController* AI);

	/** 切回"发愣"阶段，并重新掷一次等待时长 */
	void EnterIdle(FBTARPGWanderMemory* Memory);
};
