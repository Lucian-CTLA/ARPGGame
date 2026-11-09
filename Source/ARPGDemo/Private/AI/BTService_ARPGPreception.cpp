// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/BTService_ARPGPreception.h"
#include "AIController.h"                       // AAIController 的完整定义；头文件里只有前置声明，这里要调它的 GetPawn 所以必须补全
#include "GameFramework/Character.h"            // ACharacter：这里要把 Pawn 转成 Character 才能动它的旋转开关
#include "GameFramework/CharacterMovementComponent.h" // UCharacterMovementComponent::bOrientRotationToMovement 在这里
#include "Characters/AARPGEnemyCharacter.h"     // ApplyMoveSpeedForState：按档位改移动速度，间接切换 walk / jog 动作
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

	// 让敌人一直面朝玩家。Focus 会驱动 Pawn 的 Yaw（前提是 Pawn 那边 bUseControllerRotationYaw = true），
	// 所以敌人不管是走、是绕、还是站着，都会转过身来对着玩家。
	// 放在 Service 而不是 Character 的 Tick 里，一是这里本来就 0.2 秒跑一次，
	// 二是玩家死亡重生换了新 Pawn 之后，这里会自动锁到新 Pawn 上，不用额外处理。
	//
	// 脱战（NewState == 0）要把朝向交还给移动方向，否则角色因为 bOrientRotationToMovement
	// 被关掉了，游荡时会保持一个固定朝向横着走甚至倒着退 —— 所以这里两个开关一起切。
	if (ACharacter* OwnerCharacter = Cast<ACharacter>(OwnerPawn))
	{
		if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
		{
			const bool bInCombat = NewState > 0;
			// 战斗中：朝向由 Controller 的 Focus 驱动，不跟着移动方向转
			// 脱战：把朝向还给移动方向，否则角色会保持固定朝向横着走甚至倒着退
			OwnerCharacter->bUseControllerRotationYaw = bInCombat;
			MoveComp->bOrientRotationToMovement = !bInCombat;
		}

		// 速度按档位切：ABP 的 BS_MM_WalkRun 靠 GroundSpeed 选 walk / jog，
		// 所以改速度等于换动作，游荡是漫步、追击是小跑、贴身时压住速度。
		// 走 SetPerceptionState 而不是 ApplyMoveSpeedForState：后者只改速度，
		// 改不动动作；警戒要循环播 turn180 + 头顶感叹号、游荡要自己挑点走，
		// 这些都得在状态切换时一起处理，所以统一交给 Character
		if (AAARPGEnemyCharacter* Enemy = Cast<AAARPGEnemyCharacter>(OwnerPawn))
		{
			Enemy->SetPerceptionState(NewState);
		}
	}

	if (NewState > 0)
	{
		OwnerController->SetFocus(PlayerPawn);
	}
	else
	{
		OwnerController->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

