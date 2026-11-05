// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/BTService_ARPGPreception.h"
#include "AIController.h"                       // AAIController 的完整定义；头文件里只有前置声明，这里要调它的 GetPawn 所以必须补全
#include "Kismet/GameplayStatics.h"             // UGameplayStatics 的定义；GetPlayerPawn 在这里

UBTService_ARPGPreception::UBTService_ARPGPreception()
{
	NodeName = TEXT("ARPG_Distance_Perception");
	Interval = 0.2f;// 每 0.2 秒检查一次；不用每帧算，省性能
	RandomDeviation = 0.f;// 检查间隔不随机
}

void UBTService_ARPGPreception::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	AAIController* OwnerController = OwnerComp.GetAIOwner();// 这个行为树属于哪个 AI 控制器
	if (!OwnerController)
	{
		return;
	}
	APawn* OwnerPawn = OwnerController->GetPawn();//Enemy
	UBlackboardComponent* BlackboardComp = OwnerComp.GetBlackboardComponent();
	if(!OwnerPawn||!BlackboardComp)
		{
			return;
		}
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);// 拿到玩家控制的角色
	if (!PlayerPawn)  
	{
		return;
	}

	const float Distance = FVector::Dist(OwnerPawn->GetActorLocation(), PlayerPawn->GetActorLocation());// 两个坐标之间的直线距离
	BlackboardComp->SetValueAsObject(TargetActorKey.SelectedKeyName, PlayerPawn);
	BlackboardComp->SetValueAsFloat(DistanceToTargetKey.SelectedKeyName, Distance);

	int32 NewState = 0;// 默认是 0，也就是脱战
	if (Distance <= AttackDistance)
	{
		NewState = 3;
	}
	else if (Distance <= ChaseDistance)
	{
		NewState = 2;

	}
	else if (Distance <= AlertDistance)
	{
		NewState = 1;
	}

	BlackboardComp->SetValueAsInt(ComboStateKey.SelectedKeyName, NewState);// 把状态写进黑板，行为树分支靠它决定往哪走
}

