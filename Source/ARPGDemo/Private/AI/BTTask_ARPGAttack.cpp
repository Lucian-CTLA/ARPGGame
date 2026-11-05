// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/BTTask_ARPGAttack.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Characters/AARPGEnemyCharacter.h"
#include "AbilitySystemComponent.h"

UBTTask_ARPGAttack::UBTTask_ARPGAttack()
{
	NodeName = TEXT("Attack");// 行为树里显示成 "Attack"
}

EBTNodeResult::Type UBTTask_ARPGAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return EBTNodeResult::Failed;
	}
	APawn* ControlledPawn = AIController->GetPawn();// 拿这个控制器当前控制的 Pawn
	if (!ControlledPawn)
	{
		return EBTNodeResult::Failed;
	}

	// 转成敌人类型：因为 AttackAbilityClass 挂在敌人身上，要从它读
	AAARPGEnemyCharacter* Enemy = Cast<AAARPGEnemyCharacter>(ControlledPawn);

	if (!Enemy)
	{
		return EBTNodeResult::Failed;
	}

	UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();

	if (!ASC)
	{
		return EBTNodeResult::Failed;
	}

	const bool bActivated = ASC->TryActivateAbilityByClass(Enemy->AttackAbilityClass);// 真正激活技能
	return bActivated ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
