// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/BTTask_ARPGAttack.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Characters/AARPGEnemyCharacter.h"
#include "Kismet/GameplayStatics.h"
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
	if (!Enemy)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	// 这一轮还在播（含段间空档）：什么都不做，等它自己打完
	if (Enemy->IsComboActive())
	{
		if (Memory)
		{
			Memory->IdleTime = 0.f;
		}

		if (Memory && Memory->ElapsedTime >= MaxWaitTime)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BTTask_ARPGAttack] %s 连招超时 %.1f 秒，强制结束"), *GetNameSafe(Enemy), MaxWaitTime);
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		return;
	}

	// 举盾期间不续连招。举盾会把行为树暂停，这里一般是走不到的，留作保险
	if (Enemy->IsGuarding())
	{
		return;
	}

	// 玩家跑远了就把控制权交回行为树，让它去追人。
	// 不做这个判断的话，Boss 会站在原地对着空气连续挥拳
	APawn* Player = UGameplayStatics::GetPlayerPawn(Enemy, 0);
	const bool bPlayerInRange = Player
		&& FVector::DistSquared(Player->GetActorLocation(), Enemy->GetActorLocation())
			<= FMath::Square(ComboRestartRange);

	if (!bPlayerInRange || !Enemy->ShouldUseComboAttack())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	// 玩家还在面前：隔 ComboRestartDelay 秒自己再开一轮。
	// 关键在这儿 —— 以前打完一轮要回报 Succeeded，然后走行为树里那个 Wait 节点，
	// 等它数完才轮到下一次攻击，那个等待就是"攻击间隔太长"的来源。
	// 现在节点一直挂着不交还，节奏完全由 ComboRestartDelay 控制。
	if (!Memory)
	{
		Enemy->StartComboAttack();
		return;
	}

	Memory->IdleTime += DeltaSeconds;
	if (Memory->IdleTime >= ComboRestartDelay)
	{
		Memory->IdleTime = 0.f;
		Memory->ElapsedTime = 0.f;   // 每轮单独计时，别让好几轮累加成一次超时
		if (!Enemy->StartComboAttack())
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
	}
}

EBTNodeResult::Type UBTTask_ARPGAttack::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 被打断（玩家跑出范围、自己被硬直、行为树切换分支）时这里会被调用，
	// 必须把挂起的任务收掉，不然 FinishLatentTask 迟到了会干扰新分支
	return EBTNodeResult::Aborted;
}
