// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/BTTask_ARPGAttack.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Characters/AARPGEnemyCharacter.h"
#include "AbilitySystemComponent.h"

// 等待计时结构体在头文件里（行为树要按它的大小分配 NodeMemory）

UBTTask_ARPGAttack::UBTTask_ARPGAttack()
{
	NodeName = TEXT("Attack");// 行为树里显示成 "Attack"

	// 连招模式下要靠 Tick 轮询"打完了没有"，所以必须打开
	bNotifyTick = true;
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

	// 两条攻击路线，由敌人自己选：
	//  bUseComboAttack 勾了 → C++ 随机挑蒙太奇、还能连着打第二段；
	//  没勾 → 还是走原来的 GameplayAbility（GA_GolemAttack / GA_EnemyAttack）。
	// 放在 ASC 检查之后，是为了万一没有 ASC 时仍然走下面的报错路径。
	if (Enemy->ShouldUseComboAttack())
	{
		if (!Enemy->StartComboAttack())
		{
			return EBTNodeResult::Failed;
		}

		// 关键：不在这里返回 Succeeded，而是挂起。
		// 返回 Succeeded 会让行为树马上评估下一个节点（多半是 MoveTo），
		// 角色就会一边播攻击动画一边被推向玩家——第 4、5 段那种前冲动画看着最明显。
		if (FBTARPGAttackMemory* Memory = CastInstanceNodeMemory<FBTARPGAttackMemory>(NodeMemory))
		{
			Memory->ElapsedTime = 0.f;
		}
		return EBTNodeResult::InProgress;
	}

	const bool bActivated = ASC->TryActivateAbilityByClass(Enemy->AttackAbilityClass);// 真正激活技能
	return bActivated ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}

void UBTTask_ARPGAttack::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);

	FBTARPGAttackMemory* Memory = CastInstanceNodeMemory<FBTARPGAttackMemory>(NodeMemory);
	if (Memory)
	{
		Memory->ElapsedTime += DeltaSeconds;
	}

	AAIController* AIController = OwnerComp.GetAIOwner();
	AAARPGEnemyCharacter* Enemy = AIController ? Cast<AAARPGEnemyCharacter>(AIController->GetPawn()) : nullptr;

	// 敌人没了（被打死销毁）也算这次攻击结束，否则节点会永远挂起
	if (!Enemy || !Enemy->IsComboActive())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	if (Memory && Memory->ElapsedTime >= MaxWaitTime)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BTTask_ARPGAttack] %s 连招超时 %.1f 秒，强制结束"), *GetNameSafe(Enemy), MaxWaitTime);
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}

EBTNodeResult::Type UBTTask_ARPGAttack::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 被打断（玩家跑出范围、自己被硬直、行为树切换分支）时这里会被调用，
	// 必须把挂起的任务收掉，不然 FinishLatentTask 迟到了会干扰新分支
	return EBTNodeResult::Aborted;
}
