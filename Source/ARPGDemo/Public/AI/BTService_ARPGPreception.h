// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BlackboardComponent.h" 
#include "BTService_ARPGPreception.generated.h"

/**
 * 
 */
UCLASS()
class ARPGDEMO_API UBTService_ARPGPreception : public UBTService
{
	GENERATED_BODY()
public:
	UBTService_ARPGPreception();
protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;// 每隔一小段时间引擎自动调用
	
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetActorKey;// 黑板里"目标角色"这个键

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector DistanceToTargetKey;// 黑板里"到目标的距离"这个键

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector ComboStateKey;// 黑板里"战斗状态"这个键

	// 三档距离，由近到远：攻击 / 追击 / 警戒，超出 AlertDistance 就是脱战游荡
	// 300 / 500 / 1000：进 300 就开打，500 之外不再追，1000 之外彻底放羊
	UPROPERTY(EditAnywhere, Category = "Perception")
	float AttackDistance = 300.f;

	UPROPERTY(EditAnywhere, Category = "Perception")
	float ChaseDistance = 500.f;

	UPROPERTY(EditAnywhere, Category = "Perception")
	float AlertDistance = 1000.f;


};
