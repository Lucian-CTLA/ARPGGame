// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ARPGAttack.generated.h"

/** 每个 BT 实例各自的等待计时。BT 节点对象是所有 AI 共享的，状态必须放 NodeMemory 里 */
struct FBTARPGAttackMemory
{
	float ElapsedTime = 0.f;
};

/**
 * 攻击任务。
 *
 * 两种模式：
 *  - 走 GameplayAbility（老逻辑）：激活技能就返回 Succeeded，动作多长交给 GA 自己管；
 *  - 走 C++ 连招（敌人勾了 bUseComboAttack）：返回 InProgress，一直等到整段连招播完
 *    才回报 Succeeded。这个"等待"很关键——提前回报的话行为树会立刻安排下一个节点（走位），
 *    怪物就会一边挥拳一边滑走，正是"第 4、5 段攻击时跟着移动"的根源。
 *
 * 等待期间靠 Tick 轮询 IsComboActive()，不用存委托指针，简单且不会因为 Actor 提前销毁留悬空引用。
 */
UCLASS()
class ARPGDEMO_API UBTTask_ARPGAttack : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_ARPGAttack();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// 告诉行为树给这个节点分配多少内存，不覆写的话 NodeMemory 是 0 字节，写进去直接崩
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTARPGAttackMemory); }

	// 等连招时的兜底时间：万一动画资源出错、IsComboActive 永远不收尾，
	// 最多等这么久就强行结束，免得这只怪永远卡在攻击状态里不动
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	float MaxWaitTime = 12.f;
};
