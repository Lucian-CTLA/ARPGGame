// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AARPGStoneGolemSpawner.generated.h"

class UBoxComponent;
class USphereComponent;
class UNiagaraSystem;
class USoundBase;
class AAARPGEnemyCharacter;

/**
 * 召唤点：摆在 BP_Outside_Lamp3 的位置，玩家点它 → 先炸一阵火光 → 延迟一会儿把 StoneGolem 放到指定坐标。
 *
 * 三个关键行为：
 *  1. 默认跟灯交互（鼠标左键点它）就直接触发；bRequireBossDefeated 勾上才变成"必须打过前面的 Boss"；
 *  2. 只能触发一次，点第二下不会有第二只；
 *  3. 自身不渲染任何东西，只有一个"只能被鼠标射线打到、不挡人走路"的碰撞盒，
 *     盒子比灯大一圈把灯整个包住，所以射线会先打到盒子而不是灯壳，点击才收得到。
 *
 * 用法：把它拖进关卡，摆到 BP_Outside_Lamp3 那个灯的位置，Box Extent 调到能包住灯即可。
 * 生成位置在 Details 里的 Spawn Location 填（默认就是 (-12592, 13638.457834, 106.341315)）。
 */
UCLASS()
class ARPGDEMO_API AAARPGStoneGolemSpawner : public AActor
{
	GENERATED_BODY()

public:
	AAARPGStoneGolemSpawner();

	virtual void BeginPlay() override;

	// 真正干活的入口：条件都满足就放火光、排定时器。
	// BlueprintCallable 是为了万一点击不灵，可以在蓝图里直接从任意事件调它。
	UFUNCTION(BlueprintCallable, Category = "StoneGolem")
	bool TriggerSpawn();

	// 鼠标左键点到这个 Actor 时引擎会调这里。
	// 保留它只是为了以后真要开光标交互时还能用 —— 现在的主触发方式换成了走进范围（见下面的 TriggerSphere），
	// 因为游戏跑在 GameOnly 输入模式下（鼠标锁定、没有光标），Actor 的点击事件根本派发不出来。
	virtual void NotifyActorOnClicked(FKey ButtonPressed = EKeys::LeftMouseButton) override;

protected:
	// 玩家走进这个球就召唤。和存档点同一套做法：不挡路、不需要瞄准、不需要光标
	UPROPERTY(VisibleAnywhere, Category = "Trigger")
	TObjectPtr<USphereComponent> TriggerSphere;

	// 触发半径。默认 350，比灯的碰撞盒大一圈，走到灯跟前就能踩到
	UPROPERTY(EditAnywhere, Category = "Trigger")
	float TriggerRadius = 350.f;

	UFUNCTION()
	void OnTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	// ---- 要生成的怪物 ----
	// 填一个继承 AAARPGEnemyCharacter 的蓝图（比如 BP_StoneGolemEnemy）
	UPROPERTY(EditAnywhere, Category = "StoneGolem")
	TSubclassOf<AAARPGEnemyCharacter> GolemClass;

	// 生成坐标；默认就是需求里给的那个点
	UPROPERTY(EditAnywhere, Category = "StoneGolem")
	FVector SpawnLocation = FVector(-12592.0f, 13638.457834f, 106.341315f);

	// 默认直接在召唤点自己的位置上生成（灯的位置），这样玩家踩到范围就能看见它从火光里站起来。
	// 取消勾选才会用上面那个硬编码坐标 —— 那个点是早期从别处抄来的，
	// 和灯未必在一处，之前"点了灯但看不见 Boss"就是这么来的。
	UPROPERTY(EditAnywhere, Category = "StoneGolem")
	bool bSpawnAtSpawnerLocation = true;

	// 火光炸开到怪物出现的间隔，太短会看不清特效
	UPROPERTY(EditAnywhere, Category = "StoneGolem")
	float SpawnDelay = 0.7f;

	// 默认关掉：点灯（交互）就召唤，不看 Boss 死没死。
	// 想恢复成"必须先打赢前面那只 Boss"再勾回来 —— 关卡里这个开关当前离线态，
	// 勾上以后记得保存关卡。
	UPROPERTY(EditAnywhere, Category = "StoneGolem")
	bool bRequireBossDefeated = false;

	// 生成后覆盖血条上显示的 Boss 名字；留空就用怪物蓝图自己配的
	UPROPERTY(EditAnywhere, Category = "StoneGolem")
	FText GolemDisplayName = FText::FromString(TEXT("Stone Golem"));

	// ---- 火光 ----
	UPROPERTY(EditAnywhere, Category = "Fx")
	TObjectPtr<UNiagaraSystem> SummonEffect;

	// 叠一层地面碎石，火光看着更有冲击力；不需要就留空
	UPROPERTY(EditAnywhere, Category = "Fx")
	TObjectPtr<UNiagaraSystem> GroundEffect;

	UPROPERTY(EditAnywhere, Category = "Fx")
	float EffectScale = 1.6f;

	UPROPERTY(EditAnywhere, Category = "Fx")
	TObjectPtr<USoundBase> SummonSound;

	// 只有它响应鼠标射线，不挡角色移动
	UPROPERTY(VisibleAnywhere, Category = "Click")
	TObjectPtr<UBoxComponent> ClickBox;

	// 定时器到点后真正 SpawnActor
	void DoSpawn();

private:
	bool bSpawned = false;
	FTimerHandle SpawnTimer;
};
