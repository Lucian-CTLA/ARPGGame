// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ARPGAttack.generated.h"

/**
 * 
 */
UCLASS()
class ARPGDEMO_API UBTTask_ARPGAttack : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_ARPGAttack();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
