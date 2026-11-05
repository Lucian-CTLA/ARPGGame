// Copyright Lucian. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Services/BTService_BlackboardBase.h"
#include "ARPGAIAgentBTService.generated.h"

/**
 * 把 AI Agent 组件当前的决策动作写到黑板枚举键。
 *
 * 设计意图（面试重点）：
 *   组件只负责「想」（出决策），行为树负责「走」（MoveTo / 装饰器消费黑板键）。
 *   两者解耦——组件不反向依赖行为树，行为树也不认识具体游戏逻辑。
 *   用法：在行为树里挂这个 Service，把 Blackboard Key 指到一个 Enum（类型 EARPGAIAction）键，
 *   然后让 MoveTo / 分支装饰器去读这个键。
 */
UCLASS()
class ARPGAIAGENT_API UARPGAIAgentBTService : public UBTService_BlackboardBase
{
	GENERATED_BODY()

public:
	UARPGAIAgentBTService();

	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
