// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ARPGAttack.generated.h"

/** 每个 BT 实例各自的等待计时。BT 节点对象是所有 AI 共享的，状态必须放 NodeMemory 里 */
struct FBTARPGAttackMemory
{
	float ElapsedTime = 0.f;
	/** 一轮连招打完之后，距离下一轮已经过去了多久 */
	float IdleTime = 0.f;
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

	// 一轮五段打完之后，玩家还在近身的话隔多久再开一轮。
	// 这个值就是你能直接调的"攻击间隔"——以前它完全由行为树里的 Wait 节点决定，
	// 在资产里看不见也不好改，所以才会觉得"打完一刀要站半天"。
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	float ComboRestartDelay = 0.7f;

	// 玩家超过这个距离就不再自动续连招，把控制权交回行为树去追人。
	// 不要设得比行为树的攻击距离（300）大太多：站在打不到的位置连挥拳，看起来最蠢
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	float ComboRestartRange = 320.f;
};
