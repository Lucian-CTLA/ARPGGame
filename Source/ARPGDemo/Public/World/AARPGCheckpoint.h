// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AARPGCheckpoint.generated.h"

class USphereComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;

/**
 * 存档点：玩家走进那个球就记下复活位置，特效炸一下当反馈。
 *
 * 三个设计取舍：
 *  1. 用**重叠**触发而不是点击交互 —— 存档点让玩家路过就记录，比专门对准点一下顺手，
 *     也避免了点击射线被路上别的东西挡住；
 *  2. 复活点默认比 Actor 本身高 100 —— 直接放在地面高度，新生成的胶囊体常常
 *     和地板插进去，一开局就被推出来甚至掉进地里；
 *  3. 只在 HasAuthority 的那端写状态 —— 现在是单机，但写成这样以后改联机不用返工。
 *
 * 用法：拖进关卡，摆在想当存档点的地方（灯下、Boss 门口），球调大一点保证走得进。
 * 复活坐标在 Details 的 Checkpoint 分类里调（默认 = 自身位置 + 高度偏移）。
 */
UCLASS()
class ARPGDEMO_API AARPGCheckpoint : public AActor
{
	GENERATED_BODY()

public:
	AARPGCheckpoint();

	virtual void BeginPlay() override;

	// 真正记录存档的地方。蓝图画个按钮想手动存也可以直接调
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	void Activate(AActor* Activator);

	// 存档点自己算出来的复活坐标（供调试/蓝图预览用）
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	FTransform GetRespawnTransform() const;

protected:
	// 触发范围。球本身游戏里隐藏，只在编辑器里看得见，方便摆位
	UPROPERTY(VisibleAnywhere, Category = "Checkpoint")
	TObjectPtr<USphereComponent> TriggerSphere;

	// 唯一标识，写进日志方便确认存的是哪个点
	UPROPERTY(EditAnywhere, Category = "Checkpoint")
	FName CheckpointId = TEXT("Checkpoint_A");

	// 复活点相对自身的高度偏移。太低容易卡进地板
	UPROPERTY(EditAnywhere, Category = "Checkpoint")
	float RespawnHeightOffset = 100.f;

	// 勾上 = 复活时朝向和存档点一致；取消就永远朝世界坐标的 +X
	UPROPERTY(EditAnywhere, Category = "Checkpoint")
	bool bUseCheckpointRotation = true;

	// 勾上 = 特效只放一次（来回跑不会刷屏），但每次踩都会刷新存档坐标
	UPROPERTY(EditAnywhere, Category = "Checkpoint")
	bool bPlayEffectOnlyOnce = true;

	// ---- 视觉反馈 ----
	// 常驻的"这里有个存档点"效果，出生时挂在球上
	UPROPERTY(EditAnywhere, Category = "Fx")
	TObjectPtr<UNiagaraSystem> IdleEffect;

	// 踩中那一刻炸一下
	UPROPERTY(EditAnywhere, Category = "Fx")
	TObjectPtr<UNiagaraSystem> ActivateEffect;

	UPROPERTY(EditAnywhere, Category = "Fx")
	TObjectPtr<USoundBase> ActivateSound;

	UPROPERTY(EditAnywhere, Category = "Fx")
	float EffectScale = 1.f;

	UFUNCTION()
	void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

private:
	bool bEffectPlayed = false;

	// 常驻特效的组件引用，单纯为了让它在 GC 眼里是"被用着的"
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> IdleEffectComponent;
};
