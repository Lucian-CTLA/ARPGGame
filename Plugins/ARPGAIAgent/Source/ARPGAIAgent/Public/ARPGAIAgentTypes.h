// Copyright Lucian. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ARPGAIAgentTypes.generated.h"

/**
 * ============================================================================
 *  这个文件里所有东西都必须是「能安全跨线程按值搬运」的数据。
 *
 *  设计约束（面试重点）：
 *   1. 不持有任何 UObject / AActor / UWorld 的裸指针或 TSharedPtr。
 *      UObject 有线程亲和性，只能在 GameThread 上碰，跨线程传会炸 GC。
 *   2. 只放 float / int32 / bool / 枚举 / FString（值语义）。
 *      这样在 HTTP 线程里解析完，可以直接 MoveTemp 丢给主线程，零共享状态。
 *   3. USTRUCT + BlueprintType 是为了让蓝图也能读写、能进动态多播委托，
 *      但它【不等于】UObject —— 它没有 GC 参与，所以跨线程是安全的。
 * ============================================================================
 */


/** 决策动作白名单。远程返回的值不在白名单里 → 直接判非法，走本地兜底。 */
UENUM(BlueprintType)
enum class EARPGAIAction : uint8
{
	Idle		UMETA(DisplayName = "Idle"),
	Chase		UMETA(DisplayName = "Chase"),
	Retreat		UMETA(DisplayName = "Retreat"),
	Attack		UMETA(DisplayName = "Attack"),
	HeavyAttack	UMETA(DisplayName = "Heavy Attack"),
	Block		UMETA(DisplayName = "Block"),
	Roll		UMETA(DisplayName = "Roll")
};

/** 这条决策是谁给的。用来做统计和可观测性。 */
UENUM(BlueprintType)
enum class EARPGDecisionSource : uint8
{
	Local	UMETA(DisplayName = "Local Policy"),	// 本地规则，0 延迟
	Remote	UMETA(DisplayName = "Remote LLM")	// 远程模型，异步覆盖
};

/** 远程当前处于什么状态。熔断打开时不再发请求。 */
UENUM(BlueprintType)
enum class EARPGAIAgentRemoteState : uint8
{
	Disabled	UMETA(DisplayName = "Disabled"),	// 配置关掉了
	Available	UMETA(DisplayName = "Available"),
	CircuitOpen	UMETA(DisplayName = "Circuit Open")	// 连续失败，熔断中
};


/**
 * 世界快照：一次决策所看到的全部输入。
 * 在 GameThread 采样，之后作为一个「值」拷进请求上下文 —— 采样完就不再碰引擎对象。
 */
USTRUCT(BlueprintType)
struct FARPGAIWorldSnapshot
{
	GENERATED_BODY()

	/** 自身血量百分比 0~1 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") float SelfHealthPct = 1.f;
	/** 自身体力百分比 0~1 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") float SelfStaminaPct = 1.f;
	/** 目标血量百分比 0~1（没目标时是 1） */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") float TargetHealthPct = 1.f;
	/** 到目标的距离，单位厘米（UE 一律厘米） */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") float DistanceToTarget = 0.f;
	/** 距上次出手过了多少秒 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") float TimeSinceLastAttack = 0.f;
	/** 周围同伴数量 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") int32 AllyCount = 0;
	/** 是否有有效目标 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") bool bHasTarget = false;
	/** 采样时的世界时间，用于算请求耗时 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") double SampleWorldTime = 0.0;

	/** 快照是否可用（防止属性还没初始化就发请求） */
	bool IsSane() const
	{
		return FMath::IsFinite(SelfHealthPct) && FMath::IsFinite(DistanceToTarget);
	}
};


/**
 * 决策结果。同样是小 POD，HTTP 线程里造好，MoveTemp 回主线程。
 */
USTRUCT(BlueprintType)
struct FARPGDecision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") EARPGAIAction Action = EARPGAIAction::Idle;
	/** 置信度 0~1。低于阈值就不采纳，保留本地决策。 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") float Confidence = 0.f;
	/** 这条决策最短保持多少秒，用来做去抖，防止来回横跳 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") float HoldSec = 0.5f;
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") EARPGDecisionSource Source = EARPGDecisionSource::Local;
	/** 人类可读的原因，日志和调试面板用 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") FString Reason;
	/** 对应的请求序号，组件侧用来丢弃过期回包 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") int32 RequestId = 0;
	/** 端到端耗时（毫秒），可观测性 */
	UPROPERTY(BlueprintReadWrite, Category = "AI Agent") float LatencyMs = 0.f;

	bool IsSameAction(const FARPGDecision& Other) const { return Action == Other.Action; }
};


/** 把枚举转成字符串。给日志、给提示词、给非法值校验用。 */
namespace ARPGAIAgentUtil
{
	inline FString ActionToString(EARPGAIAction Action)
	{
		switch (Action)
		{
		case EARPGAIAction::Idle:			return TEXT("Idle");
		case EARPGAIAction::Chase:			return TEXT("Chase");
		case EARPGAIAction::Retreat:		return TEXT("Retreat");
		case EARPGAIAction::Attack:			return TEXT("Attack");
		case EARPGAIAction::HeavyAttack:	return TEXT("HeavyAttack");
		case EARPGAIAction::Block:			return TEXT("Block");
		case EARPGAIAction::Roll:			return TEXT("Roll");
		}
		return TEXT("Unknown");
	}

	/** 字符串 → 枚举。认不出来返回 false —— 这就是白名单校验的实现。 */
	inline bool TryParseAction(const FString& InName, EARPGAIAction& OutAction)
	{
		if (InName.Equals(TEXT("Idle"), ESearchCase::IgnoreCase))			 { OutAction = EARPGAIAction::Idle;		   return true; }
		if (InName.Equals(TEXT("Chase"), ESearchCase::IgnoreCase))			 { OutAction = EARPGAIAction::Chase;	   return true; }
		if (InName.Equals(TEXT("Retreat"), ESearchCase::IgnoreCase))		 { OutAction = EARPGAIAction::Retreat;	   return true; }
		if (InName.Equals(TEXT("Attack"), ESearchCase::IgnoreCase))			 { OutAction = EARPGAIAction::Attack;	   return true; }
		if (InName.Equals(TEXT("HeavyAttack"), ESearchCase::IgnoreCase))	 { OutAction = EARPGAIAction::HeavyAttack; return true; }
		if (InName.Equals(TEXT("Heavy Attack"), ESearchCase::IgnoreCase))	 { OutAction = EARPGAIAction::HeavyAttack; return true; }
		if (InName.Equals(TEXT("Block"), ESearchCase::IgnoreCase))			 { OutAction = EARPGAIAction::Block;	   return true; }
		if (InName.Equals(TEXT("Roll"), ESearchCase::IgnoreCase))			 { OutAction = EARPGAIAction::Roll;		   return true; }
		return false;
	}
}
