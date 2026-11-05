// Copyright Lucian. All Rights Reserved.

#include "ARPGAIAgentSubsystem.h"

#include "ARPGAIAgentSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"

void UARPGAIAgentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UsedSlots = 0;
	ConsecutiveFailures = 0;
	CircuitOpenUntilTime = 0.0;
	Stats = FARPGAIAgentStats();
}

void UARPGAIAgentSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

UARPGAIAgentSubsystem* UARPGAIAgentSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	return World ? World->GetSubsystem<UARPGAIAgentSubsystem>() : nullptr;
}

// ============================================================================
//  预算
// ============================================================================

bool UARPGAIAgentSubsystem::TryAcquireRequestSlot()
{
	const UARPGAIAgentSettings* Settings = GetDefault<UARPGAIAgentSettings>();
	const int32 MaxSlots = Settings ? Settings->MaxConcurrentRequests : 4;

	FScopeLock Lock(&StateLock);
	if (UsedSlots >= MaxSlots)
	{
		return false;
	}
	++UsedSlots;
	return true;
}

void UARPGAIAgentSubsystem::ReleaseRequestSlot()
{
	FScopeLock Lock(&StateLock);
	UsedSlots = FMath::Max(0, UsedSlots - 1);
}

// ============================================================================
//  统计 + 熔断
// ============================================================================

void UARPGAIAgentSubsystem::RecordLocalDecision()
{
	FScopeLock Lock(&StateLock);
	++Stats.LocalDecisions;
}

void UARPGAIAgentSubsystem::RecordRemoteAccepted(EARPGAIAction Action, float LatencyMs)
{
	const UARPGAIAgentSettings* Settings = GetDefault<UARPGAIAgentSettings>();

	FScopeLock Lock(&StateLock);
	++Stats.RemoteDecisions;
	++Stats.RemoteAccepted;
	ConsecutiveFailures = 0;   // 成功一次就把熔断计数清零

	LatencyAccumulatorMs += static_cast<double>(LatencyMs);
	++LatencySampleCount;
	Stats.AverageLatencyMs = static_cast<float>(LatencyAccumulatorMs / static_cast<double>(LatencySampleCount));

	if (Settings && Settings->bVerboseLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[AIAgent] remote accepted: %s (%.0f ms)"),
			*ARPGAIAgentUtil::ActionToString(Action), LatencyMs);
	}
}

void UARPGAIAgentSubsystem::RecordRemoteRejected()
{
	FScopeLock Lock(&StateLock);
	++Stats.RemoteDecisions;
	++Stats.RemoteRejected;
}

void UARPGAIAgentSubsystem::RecordRemoteTimeout()
{
	const UARPGAIAgentSettings* Settings = GetDefault<UARPGAIAgentSettings>();

	FScopeLock Lock(&StateLock);
	++Stats.RemoteDecisions;
	++Stats.RemoteTimeouts;
	++ConsecutiveFailures;

	if (Settings && ConsecutiveFailures >= Settings->CircuitBreakerThreshold)
	{
		// 熔断：一段时间内彻底不再发远程请求
		CircuitOpenUntilTime = GetWorld() ? GetWorld()->GetTimeSeconds() + Settings->CircuitBreakerCooldownSec : 0.0;
		++Stats.CircuitOpenCount;
		UE_LOG(LogTemp, Warning, TEXT("[AIAgent] circuit OPEN after %d consecutive failures; cooling down %.0fs"),
			ConsecutiveFailures, Settings->CircuitBreakerCooldownSec);
	}
}

void UARPGAIAgentSubsystem::RecordRemoteFailure()
{
	const UARPGAIAgentSettings* Settings = GetDefault<UARPGAIAgentSettings>();

	FScopeLock Lock(&StateLock);
	++Stats.RemoteDecisions;
	++Stats.RemoteFailures;
	++ConsecutiveFailures;

	if (Settings && ConsecutiveFailures >= Settings->CircuitBreakerThreshold)
	{
		CircuitOpenUntilTime = GetWorld() ? GetWorld()->GetTimeSeconds() + Settings->CircuitBreakerCooldownSec : 0.0;
		++Stats.CircuitOpenCount;
		UE_LOG(LogTemp, Warning, TEXT("[AIAgent] circuit OPEN after %d consecutive failures; cooling down %.0fs"),
			ConsecutiveFailures, Settings->CircuitBreakerCooldownSec);
	}
}

void UARPGAIAgentSubsystem::ResetCircuitBreaker()
{
	FScopeLock Lock(&StateLock);
	ConsecutiveFailures = 0;
	CircuitOpenUntilTime = 0.0;
}

// ============================================================================
//  状态查询
// ============================================================================

EARPGAIAgentRemoteState UARPGAIAgentSubsystem::GetRemoteState() const
{
	const UARPGAIAgentSettings* Settings = GetDefault<UARPGAIAgentSettings>();
	if (!Settings || !Settings->IsRemoteConfigured())
	{
		return EARPGAIAgentRemoteState::Disabled;
	}

	FScopeLock Lock(&StateLock);
	if (CircuitOpenUntilTime > 0.0)
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Now < CircuitOpenUntilTime)
		{
			return EARPGAIAgentRemoteState::CircuitOpen;
		}
	}
	return EARPGAIAgentRemoteState::Available;
}

bool UARPGAIAgentSubsystem::CanIssueRemoteRequest() const
{
	if (GetRemoteState() != EARPGAIAgentRemoteState::Available)
	{
		return false;
	}

	const UARPGAIAgentSettings* Settings = GetDefault<UARPGAIAgentSettings>();
	const int32 MaxSlots = Settings ? Settings->MaxConcurrentRequests : 4;

	FScopeLock Lock(&StateLock);
	return UsedSlots < MaxSlots;
}

const FARPGAIAgentStats& UARPGAIAgentSubsystem::GetStats() const
{
	return Stats;
}

int32 UARPGAIAgentSubsystem::GetUsedSlots() const
{
	FScopeLock Lock(&StateLock);
	return UsedSlots;
}

// ============================================================================
//  控制台命令：ARPGAIAgent.Stats
// ============================================================================

static void PrintAIAgentStats()
{
	if (!GEngine)
	{
		return;
	}

	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.WorldType != EWorldType::Game && Ctx.WorldType != EWorldType::PIE)
		{
			continue;
		}
		UWorld* World = Ctx.World();
		if (!World)
		{
			continue;
		}
		if (UARPGAIAgentSubsystem* Sub = World->GetSubsystem<UARPGAIAgentSubsystem>())
		{
			const FARPGAIAgentStats& S = Sub->GetStats();
			UE_LOG(LogTemp, Display,
				TEXT("[AIAgent] %s | slots=%d local=%d remote=%d accepted=%d rejected=%d timeout=%d fail=%d avgLat=%.0fms circuitOpen=%d state=%d"),
				*World->GetName(), Sub->GetUsedSlots(), S.LocalDecisions, S.RemoteDecisions,
				S.RemoteAccepted, S.RemoteRejected, S.RemoteTimeouts, S.RemoteFailures,
				S.AverageLatencyMs, S.CircuitOpenCount, static_cast<int32>(Sub->GetRemoteState()));
		}
	}
}

static FAutoConsoleCommand GAIAgentStatsCommand(
	TEXT("ARPGAIAgent.Stats"),
	TEXT("Print ARPG AI Agent decision statistics (local vs remote vs timeout)."),
	FConsoleCommandDelegate::CreateStatic(&PrintAIAgentStats));
