// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "Sound/SoundBase.h"
#include "NiagaraSystem.h"
#include "Camera/CameraShakeBase.h"
#include "ARPGAnimNSMeleeHitWindow.generated.h"

class AAARPGCharacterBase;
class UAbilitySystemComponent;
class USkeletalMeshComponent;

/**
 * 一段窗口（NotifyState）内部的运行时状态。
 * 注意：这不是 USTRUCT，不参与反射，只活在内存里，用来记录"这一次挥砍"的进度和已经打过谁。
 */
struct FARPGMeleeHitWindowState
{
	/** 窗口已经走过的时间（秒），每帧累加 FrameDeltaTime */
	float Elapsed = 0.f;

	/** 窗口总时长（秒），NotifyBegin 给的 TotalDuration */
	float Duration = 1.f;

	/** 上一帧判定球的球心，用来做「从上一帧位置扫到这一帧位置」的扫掠起点 */
	FVector LastCenter = FVector::ZeroVector;

	/** 本次窗口已经命中过的目标（去重表）。用弱指针，目标被打死销毁了也不会悬空 */
	TSet<TWeakObjectPtr<AActor>> HitTargets;
};

/**
 * 近战命中窗口：在蒙太奇上拖一段区间，这段区间内每一帧都用球体扫掠找敌人。
 *
 * 和 UARPGAnimNotify_MeleeHit（单帧 Notify、一次 SphereOverlap）的区别：
 *   1. 判定持续一段时间，挥砍过程中移动的敌人也能打到；
 *   2. 用「上一帧位置 -> 这一帧位置」的球体扫掠代替单点重叠，避免刀挥得太快、两帧之间从敌人身体里穿过去（穿透）；
 *   3. 带按 MeshComp 分组的去重表，同一次挥砍每个目标只吃一次伤害。
 */
UCLASS(meta = (DisplayName = "Melee Hit Window (Sweep)"))
class ARPGDEMO_API UARPGAnimNSMeleeHitWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UARPGAnimNSMeleeHitWindow();

	// ==================== 伤害 ====================

	/** 命中时施加的伤害 GameplayEffect（里面用 SetByCaller 读伤害值） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Damage")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/**
	 * GE 里 SetByCaller 用的 Data Tag。
	 * 默认 SetByCaller.Damage —— 必须和你 GE 里配置的 Tag 完全一致，否则伤害恒为 0 且不报错。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Damage")
	FGameplayTag DamageSetByCallerTag;

	/** 读不到 AttackPower 属性时用的保底伤害 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Damage")
	float FallbackDamage = 20.f;

	// ==================== 判定形状 ====================

	/** 判定球半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Shape", meta = (ClampMin = "1.0"))
	float SphereRadius = 45.f;

	/**
	 * 武器 socket 名（比如 weapon_r / hand_r）。
	 * 填了就用 socket 的世界位置当球心 —— 刀挥到哪儿就判到哪儿，最准。
	 * 留空则退化成「角色前向偏移」模式，用下面三个参数算。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Shape")
	FName SweepSocket = NAME_None;

	/** 无 socket 时：窗口开始时的前向偏移（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Shape")
	float StartForwardOffset = 60.f;

	/** 无 socket 时：窗口结束时的前向偏移（cm），从 Start 线性插值到 End */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Shape")
	float EndForwardOffset = 180.f;

	/** 球心离角色脚下的高度（cm），一般取胸口高度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Shape")
	float HeightOffset = 50.f;

	// ==================== 规则 ====================

	/**
	 * true：一次窗口内每个目标只吃一次伤害（主流做法，靠 HitTargets 去重表实现）。
	 * false：每一帧命中的目标都再吃一次伤害 —— 会瞬间打空血，除非你要做持续伤害，否则别关。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Rule")
	bool bSingleHitPerTarget = true;

	/**
	 * true：只打敌对阵营（Hero 与 Enemy 类互为敌对）。
	 * 关掉的话，Boss 挥刀会把站在旁边的杂兵一起砍死。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Rule")
	bool bRequireHostile = true;

	// ==================== 打击感 ====================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|FX")
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|FX")
	TObjectPtr<UNiagaraSystem> HitVFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|FX")
	TSubclassOf<UCameraShakeBase> HitShakeClass;

	/** 是否卡肉（命中瞬间把双方动画速度压到 HitStopDilation） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|FX")
	bool bApplyHitStop = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|FX", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float HitStopDilation = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|FX", meta = (ClampMin = "0.0"))
	float HitStopDuration = 0.08f;

	// ==================== 调试 ====================

	/** 打开后每帧画出扫掠球体轨迹，方便确认判定范围和角色胶囊体对得上 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Melee|Debug")
	bool bDrawDebug = false;

	// ==================== 引擎回调 ====================

	virtual void NotifyBegin(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	/** 必须是带 FAnimNotifyEventReference 的四参数版；三参数版 5.0 起已 deprecated */
	virtual void NotifyTick(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float FrameDeltaTime,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;

private:
	/** 算出当前时刻判定球的球心：有 socket 就用 socket，否则用前向偏移插值 */
	FVector ComputeCenter(USkeletalMeshComponent* MeshComp, float Alpha) const;

	/** 收集这一帧碰到的人：移动了就扫掠，没动就重叠 */
	void CollectTargets(
		USkeletalMeshComponent* MeshComp,
		const FVector& From,
		const FVector& To,
		TArray<AActor*>& OutActors) const;

	/** 从 ASC 上读 AttackPower，读不到用 FallbackDamage */
	float GetAttackDamage(UAbilitySystemComponent* ASC) const;

	/** 真正结算一次命中：伤害 GE + 特效 + 音效 + 卡肉 */
	void ApplyHit(
		AAARPGCharacterBase* Attacker,
		AAARPGCharacterBase* Target,
		USkeletalMeshComponent* MeshComp,
		float Damage) const;

	/**
	 * 去重表：Key 是 MeshComp，不是「一个成员变量」。
	 *
	 * 因为 Notify 对象是「资产级共享」的 —— 蒙太奇资产的同一个 Notify 实例，
	 * 会被所有播放这段动画的角色共用（Hero、Boss、杂兵都是同一个 UObject）。
	 * 如果把 HitTargets 写成单个 TSet 成员，Boss 挥刀时 Hero 的攻击窗口就会读到脏数据，
	 * 表现是「明明没打到却不出伤害」。所以必须按 MeshComp 分组。
	 */
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FARPGMeleeHitWindowState> WindowStates;
};
