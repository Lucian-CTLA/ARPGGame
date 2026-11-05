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

	UPROPERTY(EditAnywhere, Category = "Perception")
	float AttackDistance = 300.f;

	UPROPERTY(EditAnywhere, Category = "Perception")
	float ChaseDistance = 800.f;

	UPROPERTY(EditAnywhere, Category = "Perception")
	float AlertDistance = 1500.f;


};
