// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"                        // UE 基础类型
#include "AIController.h"                       // AAIController 的定义；它来自 AIModule，所以 Build.cs 里必须加那个模块
#include "BehaviorTree/BehaviorTree.h"          // UBehaviorTree 的定义；下面要用它来指定跑哪棵行为树
#include "AARPGEnemyController.generated.h"     // 反射生成代码，放最后

UCLASS()                                        // 让这个类进入反射系统
class ARPGDEMO_API AAARPGEnemyController : public AAIController   // 继承 AI 控制器；玩家用 PlayerController 接收按键，敌人用这个来跑行为树
{
	GENERATED_BODY()                            // 生成代码插入点

public:
	AAARPGEnemyController();                    // 构造函数

	UPROPERTY(EditDefaultsOnly, Category = "AI")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;   // 这个敌人跑哪棵行为树；具体跑哪棵在蓝图子类的 Class Defaults 里选，C++ 里不写死路径

protected:
	virtual void OnPossess(APawn* InPawn)override;   // 引擎在这个控制器接管某个角色时自动调用
};
