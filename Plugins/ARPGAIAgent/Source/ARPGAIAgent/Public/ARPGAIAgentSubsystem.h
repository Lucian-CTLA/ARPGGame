// Copyright Lucian. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ARPGAIAgentTypes.h"
#include "ARPGAIAgentSubsystem.generated.h"

/** 运行期统计。做成可观测的，出问题时能直接看出是「没配」「超时」还是「熔断」。 */
USTRUCT(BlueprintType)
struct FARPGAIAgentStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AI Agent") int32 LocalDecisions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "AI Agent") int32 RemoteDecisions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "AI Agent") int32 RemoteAccepted = 0;
	UPROPERTY(BlueprintReadOnly, Category = "AI Agent") int32 RemoteRejected = 0;
	UPROPERTY(BlueprintReadOnly, Category = "AI Agent") int32 RemoteTimeouts = 0;
	UPROPERTY(BlueprintReadOnly, Category = "AI Agent") int32 RemoteFailures = 0;
	UPROPERTY(BlueprintReadOnly, Category = "AI Agent") float AverageLatencyMs = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "AI Agent") int32 CircuitOpenCount = 0;
};

/**
 * 世界级子系统：管「预算」和「熔断」。
 *
 * 为什么不放组件里？
 *   远程服务挂了是「整个服务」挂了，不是某一个敌人挂了。
 *   把并发额度和熔断状态放在 World 级，所有组件共享，
 *   才能做到：服务一挂，全场立刻统一降级，而不是每个敌人各试三次才死心。
 */
UCLASS()
class ARPGAIAGENT_API UARPGAIAgentSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 抢一个并发额度。抢不到就别发请求，直接走本地。 */
	bool TryAcquireRequestSlot();

	/** 请求结束（成功或失败都算）后归还额度。 */
	void ReleaseRequestSlot();

	void RecordLocalDecision();
	void RecordRemoteAccepted(EARPGAIAction Action, float LatencyMs);
	void RecordRemoteRejected();
	void RecordRemoteTimeout();
	void RecordRemoteFailure();

	/** 远程当前状态：Disabled / Available / CircuitOpen */
	UFUNCTION(BlueprintCallable, Category = "AI Agent")
	EARPGAIAgentRemoteState GetRemoteState() const;

	/** 现在能不能发远程请求（配置 OK + 有额度 + 没熔断） */
	UFUNCTION(BlueprintCallable, Category = "AI Agent")
	bool CanIssueRemoteRequest() const;

	UFUNCTION(BlueprintCallable, Category = "AI Agent")
	const FARPGAIAgentStats& GetStats() const;
	int32 GetUsedSlots() const;

	/** 手动把熔断复位（调试用，比如服务恢复了想立刻重试） */
	UFUNCTION(BlueprintCallable, Category = "AI Agent")
	void ResetCircuitBreaker();

	/** 便于在蓝图 / 控制台拿到实例 */
	UFUNCTION(BlueprintCallable, Category = "AI Agent", meta = (WorldContext = "WorldContextObject"))
	static UARPGAIAgentSubsystem* Get(const UObject* WorldContextObject);

private:
	mutable FCriticalSection StateLock;

	int32 UsedSlots = 0;
	int32 ConsecutiveFailures = 0;
	double CircuitOpenUntilTime = 0.0;

	FARPGAIAgentStats Stats;

	/** 累计延迟（秒→毫秒），用来算均值 */
	double LatencyAccumulatorMs = 0.0;
	int32 LatencySampleCount = 0;
};
