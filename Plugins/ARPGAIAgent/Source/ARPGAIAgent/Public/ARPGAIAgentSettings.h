// Copyright Lucian. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ARPGAIAgentSettings.generated.h"

/**
 * 插件全局配置。做成 UDeveloperSettings 而不是硬编码常量，
 * 好处是不改代码就能在 Project Settings 里调参，也能按项目写进 Config/DefaultGame.ini。
 *
 * 安全约定：API Key 【不】写进 ini、不进版本库，走环境变量。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ARPG AI Agent"))
class ARPGAIAGENT_API UARPGAIAgentSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UARPGAIAgentSettings();

	// ---------------- 远程（LLM）----------------

	/** 是否启用远程决策。关掉 = 纯本地规则，完全离线可跑。 */
	UPROPERTY(Config, EditAnywhere, Category = "Remote")
	bool bRemoteEnabled = false;

	/** OpenAI 兼容的 chat completions 端点 */
	UPROPERTY(Config, EditAnywhere, Category = "Remote")
	FString EndpointUrl = TEXT("https://api.openai.com/v1/chat/completions");

	UPROPERTY(Config, EditAnywhere, Category = "Remote")
	FString ModelName = TEXT("gpt-4o-mini");

	/**
	 * 读取 API Key 的环境变量名。
	 * 运行时用 FPlatformMisc::GetEnvironmentVariable 取，取不到就自动降级为纯本地。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Remote")
	FString ApiKeyEnvVar = TEXT("ARPG_AI_API_KEY");

	/** 单个请求超时（秒）。LocalFirst 的含义就是：超时不影响游戏，因为本地决策已经生效了。 */
	UPROPERTY(Config, EditAnywhere, Category = "Remote", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float RequestTimeoutSec = 0.8f;

	/** 最大重试次数（含首次）。超了就交给本地。 */
	UPROPERTY(Config, EditAnywhere, Category = "Remote", meta = (ClampMin = "0", ClampMax = "3"))
	int32 MaxRetries = 0;

	// ---------------- LocalFirst 策略 ----------------

	/**
	 * 真正的 LocalFirst 开关。
	 * true  = 本地决策【立刻】生效，远程回包只做「升级修正」，超时静默丢弃。
	 * false = 等远程回来再动（仅调试用，会有卡顿，别用于实机）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "LocalFirst")
	bool bLocalFirst = true;

	/** 两次决策之间的最小间隔（秒）。防止每帧发请求把服务端打爆。 */
	UPROPERTY(Config, EditAnywhere, Category = "LocalFirst", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DecisionIntervalSec = 1.0f;

	/** 远程置信度低于这个值就不采纳，保留本地决策。 */
	UPROPERTY(Config, EditAnywhere, Category = "LocalFirst", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinAcceptConfidence = 0.5f;

	/**
	 * 一条决策最短保持时间（秒）。防止两条决策来回横跳导致抽搐。
	 * 必须大于最长的动作时长才有意义：攻击蒙太奇约 1.39s，所以默认给 1.5。
	 * 原先它是「写了但没人读」的死字段，现在接在 RunDecisionCycle 的闸门上。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "LocalFirst", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float DecisionHoldSec = 1.5f;

	/** 进入追击的距离阈值（厘米）。本地规则用。 */
	UPROPERTY(Config, EditAnywhere, Category = "LocalFirst", meta = (ClampMin = "0.0"))
	float ChaseRangeCm = 800.f;

	/** 进入攻击的距离阈值（厘米）。本地规则用。 */
	UPROPERTY(Config, EditAnywhere, Category = "LocalFirst", meta = (ClampMin = "0.0"))
	float AttackRangeCm = 250.f;

	/** 血量低于这个百分比就考虑撤退。本地规则用。 */
	UPROPERTY(Config, EditAnywhere, Category = "LocalFirst", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthPct = 0.3f;

	// ---------------- 预算与熔断 ----------------

	/** 全局同时在飞的请求数上限。超了直接走本地，不发请求。 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxConcurrentRequests = 4;

	/** 连续失败多少次就熔断 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1", ClampMax = "20"))
	int32 CircuitBreakerThreshold = 3;

	/** 熔断后冷却多少秒再试探 */
	UPROPERTY(Config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float CircuitBreakerCooldownSec = 10.f;

	// ---------------- 调试 ----------------

	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bVerboseLog = false;

	/** 取 API Key。取不到返回空 —— 调用方看到空就自动降级。 */
	FString ResolveApiKey() const;

	/** 远程是不是「真的可用」：开关开了 + Key 有 + 端点非空 + 没熔断 */
	bool IsRemoteConfigured() const;
};
