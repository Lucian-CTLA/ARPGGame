// Copyright Lucian. All Rights Reserved.

#include "ARPGAIAgentBTService.h"

#include "ARPGAIAgentComponent.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

UARPGAIAgentBTService::UARPGAIAgentBTService()
{
	NodeName = TEXT("Write AI Agent Action");
	// 决策本身已有 1Hz 节流，这里 0.1s 同步一次足够跟手，又不刷太频繁
	Interval = 0.1f;
	bNotifyTick = true;
}

void UARPGAIAgentBTService::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	APawn* Pawn = nullptr;
	if (AAIController* AI = OwnerComp.GetAIOwner())
	{
		Pawn = AI->GetPawn();
	}
	if (!Pawn)
	{
		return;
	}

	// 组件没挂就保持原值，不强行写（插件不假设一定有 Agent 组件）
	if (const UARPGAIAgentComponent* Agent = Pawn->FindComponentByClass<UARPGAIAgentComponent>())
	{
		const EARPGAIAction Action = Agent->GetCurrentDecision().Action;
		BB->SetValueAsEnum(BlackboardKey.SelectedKeyName, static_cast<uint8>(Action));
	}
}
