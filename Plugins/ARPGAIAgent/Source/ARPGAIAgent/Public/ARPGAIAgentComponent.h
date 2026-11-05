// Copyright Lucian. All Rights Reserved.
//
// ============================================================================
//  ARPGAIAgent —— 简历「项目二：组件化异步决策与线程安全框架」设计要点索引
//  下面三点是简历上的描述，对应实现位置（方便面试时按图索骥）：
//   1) 组件化接入：见 UARPGAIAgentComponent（本文件）。它是 UActorComponent，挂谁身上
//      谁就有 AI 大脑（玩家/小怪/Boss 皆可用）；通过 IAbilitySystemInterface + 反射读
//      属性，不依赖具体游戏模块；pimpl（FARPGAIAgentRemoteClientDeleter）把 HTTP/Json
//      依赖关在 Private 侧，避免插件 ↔ 游戏模块循环依赖。
//   2) 异步请求与生命周期保护：见 ARPGAIAgentRemoteClient.cpp。HTTP 在 Http 线程完成、
//      JSON 解析后 AsyncTask 回 GameThread 回调（主线程只收结果）；NextRequestId
//      （std::atomic fetch_add）+ LastIssuedRequestId 处理请求乱序；TWeakPtr<AliveToken>
//      处理对象销毁；无主/失效请求直接丢弃，杜绝跨线程访问崩溃。
//   3) 统一并发与容错：见 ARPGAIAgentSubsystem.cpp。MaxConcurrentRequests 管理全局并发
//      额度；CircuitBreakerThreshold / CircuitBreakerCooldownSec 实现熔断；bLocalFirst
//      （ARPGAIAgentSettings.h）本地规则先行、远程结果修正，超时/失败/非法/离线自动降级。
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include "Components/ActorComponent.h"
#include "Abilities/GameplayAbility.h"
#include "ARPGAIAgentTypes.h"
#include "ARPGAIAgentComponent.generated.h"

class UAbilitySystemComponent;
class UARPGAIAgentSubsystem;
class FARPGAIAgentRemoteClient;

/**
 * pimpl 的删除器。
 *
 * 为什么要有它：TUniquePtr 默认删除器要求类型完整，那就得在 Public 头里
 * include ARPGAIAgentRemoteClient.h → 而那个头 include 了 IHttpRequest.h
 * → HTTP 依赖就被传染给所有用到本插件的模块。
 * 把 operator() 挪到 .cpp 里定义，头里只留前置声明，
 * HTTP / Json 依赖就彻底关在插件的 Private 侧了。
 */
struct FARPGAIAgentRemoteClientDeleter
{
	void operator()(FARPGAIAgentRemoteClient* Ptr) const;
};

/** 一次决策产出时广播，蓝图 / 行为树可以挂在这里做自己的反应。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnARPGDecisionReady, const FARPGDecision&, Decision);

/**
 * 组件化的 AI 决策代理。
 *
 * 【组件化接入】体现在三处：
 *  1. 它是 UActorComponent，挂谁身上谁就有 AI 大脑 —— 玩家、小怪、Boss 都能挂，
 *     不需要改它们的类、不需要继承关系。
 *  2. 它通过 GAS 标准接口（IAbilitySystemInterface）+ 反射读属性，
 *     通过黑板 / PlayerController 找目标，【完全不依赖具体游戏模块】。
 *     所以它能作为独立插件交付，不会产生 ARPGDemo ↔ 插件 的循环依赖。
 *  3. 动作到技能的映射是一张可配的 Map（ActionAbilityMap），
 *     在蓝图 Details 面板里填，不用写代码。
 *
 * 【LocalFirst】体现在一次决策循环里：
 *   采样 → 本地规则立刻出决策并【立即生效】 → 同时异步问远程 →
 *   远程回来了且合法且更优 → 才升级覆盖。
 *   远程超时 / 失败 / 返回非法值 → 什么都不做，本地决策继续跑。
 *   也就是说：远程永远不可能拖慢或卡住游戏。
 */
UCLASS(ClassGroup = (ARPG), meta = (BlueprintSpawnableComponent))
class ARPGAIAGENT_API UARPGAIAgentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UARPGAIAgentComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---------------- 采样 ----------------

	/**
	 * 采一次世界快照。默认实现走 GAS 反射 + 距离计算；蓝图可覆写塞自己的数据。
	 * 注意：只在 GameThread 调，采样结果是一个纯值，拷走之后就不再碰引擎对象。
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AI Agent")
	FARPGAIWorldSnapshot SampleWorldSnapshot();
	virtual FARPGAIWorldSnapshot SampleWorldSnapshot_Implementation();

	/**
	 * 本地策略（规则 / 效用打分）。远程不可用时全靠它。
	 * 蓝图可覆写，比如给 Boss 写一套更激进的规则。
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AI Agent")
	FARPGDecision MakeLocalDecision(const FARPGAIWorldSnapshot& Snapshot);
	virtual FARPGDecision MakeLocalDecision_Implementation(const FARPGAIWorldSnapshot& Snapshot);

	// ---------------- 对外控制 ----------------

	/** 立刻跑一次决策循环（忽略节流）。蓝图 / 行为树 Task 可以调。 */
	UFUNCTION(BlueprintCallable, Category = "AI Agent")
	void RunDecisionCycle(bool bForce = false);

	UFUNCTION(BlueprintCallable, Category = "AI Agent")
	void SetRemoteEnabled(bool bInEnabled);

	UFUNCTION(BlueprintPure, Category = "AI Agent")
	bool IsRemoteEnabled() const { return bEnableRemote; }

	UFUNCTION(BlueprintPure, Category = "AI Agent")
	const FARPGDecision& GetCurrentDecision() const { return CurrentDecision; }

	// ---------------- 配置 ----------------

	/** 是否允许问远程。关掉就是纯本地规则，离线可跑。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Agent")
	bool bEnableRemote = true;

	/** 决策间隔（秒）。<=0 表示用全局配置。别设太小，会打爆服务端。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Agent", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DecisionIntervalOverride = 0.f;

	/** 动作名 → 技能类。Key 必须是 ActionToString 出来的名字（Attack / HeavyAttack / Block / Roll ...）。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Agent|Binding")
	TMap<FName, TSubclassOf<UGameplayAbility>> ActionAbilityMap;

	/** 从 AI 黑板里读目标用的键名 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Agent|Binding")
	FName TargetBlackboardKey = TEXT("TargetActor");

	/** 属性名。默认 Health / MaxHealth / Stamina / MaxStamina，别的项目的 AttributeSet 可以改这里。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Agent|Binding")
	FName HealthAttributeName = TEXT("Health");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Agent|Binding")
	FName MaxHealthAttributeName = TEXT("MaxHealth");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Agent|Binding")
	FName StaminaAttributeName = TEXT("Stamina");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Agent|Binding")
	FName MaxStaminaAttributeName = TEXT("MaxStamina");

	/** 决策产出广播 */
	UPROPERTY(BlueprintAssignable, Category = "AI Agent")
	FOnARPGDecisionReady OnDecisionReady;

protected:
	/** 把决策落到游戏里：广播 + 按 ActionAbilityMap 激活对应 GAS 技能 */
	virtual void ApplyDecision(const FARPGDecision& Decision);

	/**
	 * 修1：还掉本次占用的远程请求 slot（幂等）。
	 * 所有「请求结束 / 被掐断」的路径都走这里，避免额度泄漏。
	 */
	void ReleaseHeldSlot();

	/** 异步问远程。不阻塞。 */
	void TryIssueRemoteRequest(const FARPGAIWorldSnapshot& Snapshot);

	/** 远程结果落地（承诺在 GameThread 调） */
	void HandleRemoteResult(int32 RequestId, bool bSucceeded, FARPGDecision Decision);

	// ---- 小工具 ----
	UAbilitySystemComponent* GetOwnerAbilitySystem() const;
	UARPGAIAgentSubsystem* GetAgentSubsystem() const;
	AActor* FindTargetActor() const;

	/** 反射读 AttributeSet 上的 float 属性。这是「插件不依赖游戏模块」的关键。 */
	float ReadNumericAttribute(FName AttributeName) const;

	/** 0~1 百分比；分母为 0 时返回 1 */
	static float SafePct(float Value, float Max) { return (Max > KINDA_SMALL_NUMBER) ? FMath::Clamp(Value / Max, 0.f, 1.f) : 1.f; }

protected:
	UPROPERTY(BlueprintReadOnly, Category = "AI Agent")
	FARPGDecision CurrentDecision;

	/** 最后一次发出的请求 Id。回包 Id 对不上就丢弃 —— 防止慢回包覆盖新决策。 */
	int32 LastIssuedRequestId = 0;

	/** 是否有请求在飞。配合 LastIssuedRequestId 做去重。 */
	std::atomic<bool> bRequestInFlight{ false };

	/**
	 * 修1：本次决策循环是否占着一个远程请求 slot。
	 * 用来在「掐断在飞请求」的路径（SetRemoteEnabled(false) / EndPlay / 发请求失败）
	 * 也能可靠地把 slot 还回去，否则 slot 会永久泄漏、并发额度越来越少。
	 * 只在 GameThread 读写，不需要加锁。
	 */
	bool bSlotHeld = false;

	/** 小于 0 表示「还没发生过」 */
	double LastDecisionTime = -1.0;
	double LastAttackTime = -1.0;

	/**
	 * 上一次决策时是否已经进入攻击距离。
	 *
	 * 这是回滞（hysteresis）的状态位：进入攻击距离用 AttackRangeCm，
	 * 退出用 AttackRangeCm * 1.25，两个阈值之间是一条死区。
	 * 单阈值的问题是距离在边界上浮动时判定结果跟着翻——
	 * 行为树 MoveTo 的停步距离、玩家自己的位移、攻击推退都会让距离在 250cm 附近抖，
	 * 1Hz 采样下就表现成 chasing / in melee reach 每秒横跳。
	 * 状态必须独立存放，不能拿 CurrentDecision 反推，否则改判定分支时会一起出错。
	 */
	bool bWasInAttackRange = false;

	TUniquePtr<FARPGAIAgentRemoteClient, FARPGAIAgentRemoteClientDeleter> RemoteClient;
};
