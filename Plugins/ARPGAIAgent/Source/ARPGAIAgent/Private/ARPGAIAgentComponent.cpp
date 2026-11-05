// Copyright Lucian. All Rights Reserved.

#include "ARPGAIAgentComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AIController.h"
#include "ARPGAIAgentRemoteClient.h"
#include "ARPGAIAgentSettings.h"
#include "ARPGAIAgentSubsystem.h"
#include "AttributeSet.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

// pimpl 删除器：这里能看到完整定义，所以能 delete
void FARPGAIAgentRemoteClientDeleter::operator()(FARPGAIAgentRemoteClient* Ptr) const
{
	delete Ptr;
}

namespace
{
	/**
	 * 在任意 Actor 上按名字反射读一个 float 属性。
	 *
	 * 这是「插件不依赖游戏模块」的关键手法：
	 *   插件不认识项目里的 UUARPGAttributeSet，但它认识 GAS 的标准接口
	 *   IAbilitySystemInterface + UAttributeSet。拿 FProperty 指针构造
	 *   FGameplayAttribute，就能读到当前值（含 GE 修正后的最终值）。
	 *   属性名在组件 Details 面板里可配，换项目也不用改代码。
	 */
	float ReadFloatAttributeOnActor(const AActor* Actor, FName AttributeName)
	{
		if (!Actor || AttributeName.IsNone())
		{
			return 0.f;
		}

		const IAbilitySystemInterface* AsAsc = Cast<IAbilitySystemInterface>(Actor);
		if (!AsAsc)
		{
			return 0.f;
		}

		UAbilitySystemComponent* Asc = AsAsc->GetAbilitySystemComponent();
		if (!Asc)
		{
			return 0.f;
		}

		for (const UAttributeSet* Set : Asc->GetSpawnedAttributes())
		{
			if (!Set)
			{
				continue;
			}
			if (const FFloatProperty* Prop = FindFProperty<FFloatProperty>(Set->GetClass(), AttributeName))
			{
				// FGameplayAttribute 的构造函数收的是非 const FProperty*
				return Asc->GetNumericAttribute(FGameplayAttribute(const_cast<FFloatProperty*>(Prop)));
			}
		}
		return 0.f;
	}
}

// ============================================================================
//  生命周期
// ============================================================================

UARPGAIAgentComponent::UARPGAIAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// 决策不需要 60Hz。真正的节奏由 DecisionInterval 控制，这里只是检查的频率。
	PrimaryComponentTick.TickInterval = 0.2f;
}

void UARPGAIAgentComponent::BeginPlay()
{
	Super::BeginPlay();

	// 注意：不能用 MakeUnique —— 它返回 TUniquePtr<T>（默认 TDefaultDelete 删除器），
	// 与本成员的自定义删除器类型不匹配，移动赋值时会在函数体内做
	// Deleter = MoveTemp(OtherDeleter) 而编译失败。用 Reset 保持自定义删除器不变。
	RemoteClient.Reset(new FARPGAIAgentRemoteClient());

	// 保证进游戏后马上能出第一次决策
	LastDecisionTime = -1.0;
	LastAttackTime = -1.0;
}

void UARPGAIAgentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 关键：组件要没了，必须把在飞的请求掐掉并置死存活令牌，
	// 否则回包会打到已销毁的对象上。
	if (RemoteClient.IsValid())
	{
		RemoteClient->CancelAll();
		RemoteClient.Reset();
	}

	// 修1：组件销毁时，把在飞标志和占用的 slot 一起清掉，否则 slot 永久泄漏
	bRequestInFlight.store(false, std::memory_order_release);
	ReleaseHeldSlot();

	Super::EndPlay(EndPlayReason);
}

void UARPGAIAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 节流：真正的闸门在 RunDecisionCycle 里，这里只是别每帧都去算一遍时间。
	RunDecisionCycle(false);
}

// ============================================================================
//  LocalFirst 决策循环
// ============================================================================

void UARPGAIAgentComponent::RunDecisionCycle(bool bForce)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UARPGAIAgentSettings* Cfg = GetDefault<UARPGAIAgentSettings>();
	const float Interval = (DecisionIntervalOverride > 0.f)
		? DecisionIntervalOverride
		: (Cfg ? Cfg->DecisionIntervalSec : 1.0f);

	// 闸门同时受两个约束：固定决策间隔 + 上一条决策自带的 HoldSec。
	// HoldSec 的语义是「这条决策至少保持这么久」，用来压住攻击 / 格挡期间的改判
	// （攻击蒙太奇约 1.39s，所以 HoldSec 要大于它才有效）。
	const float EffectiveInterval = FMath::Max(Interval, CurrentDecision.HoldSec);

	const double Now = World->GetTimeSeconds();
	if (!bForce && LastDecisionTime >= 0.0 && (Now - LastDecisionTime) < static_cast<double>(EffectiveInterval))
	{
		return;   // 还没到下一次决策的时间
	}

	// ---------- 采样（GameThread）----------
	FARPGAIWorldSnapshot Snapshot = SampleWorldSnapshot();
	if (!Snapshot.IsSane())
	{
		return;
	}

	// ---------- ① 本地决策：立刻生效，不等网络 ----------
	// 这一步是 LocalFirst 的全部意义所在：即使远程永远不回，游戏也照常跑。
	const FARPGDecision LocalDecision = MakeLocalDecision(Snapshot);
	const bool bChanged = (LocalDecision.Action != CurrentDecision.Action);
	ApplyDecision(LocalDecision);

	if (UARPGAIAgentSubsystem* Sub = GetAgentSubsystem())
	{
		Sub->RecordLocalDecision();
	}

	if (Cfg && Cfg->bVerboseLog && bChanged)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[AIAgent] %s local -> %s (%s)"),
			*GetNameSafe(GetOwner()),
			*ARPGAIAgentUtil::ActionToString(LocalDecision.Action),
			*LocalDecision.Reason);
	}

	// ---------- ② 异步问远程：只是「升级」，不影响当前行为 ----------
	TryIssueRemoteRequest(Snapshot);
}

void UARPGAIAgentComponent::TryIssueRemoteRequest(const FARPGAIWorldSnapshot& Snapshot)
{
	if (!bEnableRemote)
	{
		return;
	}
	if (!RemoteClient.IsValid())
	{
		return;
	}

	UARPGAIAgentSubsystem* Sub = GetAgentSubsystem();
	if (!Sub || !Sub->CanIssueRemoteRequest())
	{
		return;   // 没配 / 熔断中 / 并发满了 → 静默走本地
	}

	// 同一时刻只允许一个请求在飞（每个组件自己的去重）
	if (bRequestInFlight.load(std::memory_order_acquire))
	{
		return;
	}

	if (!Sub->TryAcquireRequestSlot())
	{
		return;
	}
	bSlotHeld = true;   // 修1：占住 slot，后续无论哪条路径都靠 ReleaseHeldSlot 还

	// ★ 线程安全要点：TWeakObjectPtr 必须【在 GameThread 构造】。
	//   它在构造时会去读 UObject 的弱引用表，放到 HTTP 线程做会数据竞争。
	//   所以在这里先做好，再把它当成一个「值」拷进 lambda。
	TWeakObjectPtr<UARPGAIAgentComponent> WeakThis(this);

	const int32 IssuedId = RemoteClient->SendAsync(Snapshot,
		[WeakThis](int32 RequestId, bool bSucceeded, FARPGDecision Decision) mutable
		{
			// 客户端承诺：这个回调一定在 GameThread。
			UARPGAIAgentComponent* Self = WeakThis.Get();
			if (!Self)
			{
				return;   // 组件已经没了（敌人被杀 / 关卡切换），静默丢弃
			}
			Self->HandleRemoteResult(RequestId, bSucceeded, MoveTemp(Decision));
		});

	if (IssuedId <= 0)
	{
		ReleaseHeldSlot();   // 修1：发请求失败，立刻还 slot
		return;
	}

	LastIssuedRequestId = IssuedId;
	bRequestInFlight.store(true, std::memory_order_release);
}

void UARPGAIAgentComponent::HandleRemoteResult(int32 RequestId, bool bSucceeded, FARPGDecision Decision)
{
	bRequestInFlight.store(false, std::memory_order_release);

	UARPGAIAgentSubsystem* Sub = GetAgentSubsystem();
	const UARPGAIAgentSettings* Cfg = GetDefault<UARPGAIAgentSettings>();

	if (Sub)
	{
		ReleaseHeldSlot();   // 修1：回包落地（成功或失败），还掉 slot
	}

	// ★ 去乱序：网络慢的时候，先发的请求可能后到。
	//   Id 对不上说明这是过期回包，直接丢，绝不能让它覆盖新决策。
	if (RequestId != LastIssuedRequestId)
	{
		return;
	}

	if (!bSucceeded)
	{
		// 超时 / 连不上 / 解析失败 / 非法动作 —— 什么都不做，本地决策继续生效。
		if (Sub) { Sub->RecordRemoteTimeout(); }
		return;
	}

	if (Sub && Cfg && Decision.Confidence < Cfg->MinAcceptConfidence)
	{
		Sub->RecordRemoteRejected();
		return;
	}

	// 去抖：远程和当前在跑的一样，就别打断（打断会重新播动画、很丑）。
	if (Decision.Action == CurrentDecision.Action)
	{
		if (Sub) { Sub->RecordRemoteRejected(); }
		return;
	}

	// 通过全部校验 —— 升级为远程决策
	ApplyDecision(Decision);

	if (Sub)
	{
		Sub->RecordRemoteAccepted(Decision.Action, Decision.LatencyMs);
	}

	if (Cfg && Cfg->bVerboseLog)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[AIAgent] %s remote -> %s (conf %.2f, %.0f ms, %s)"),
			*GetNameSafe(GetOwner()),
			*ARPGAIAgentUtil::ActionToString(Decision.Action),
			Decision.Confidence, Decision.LatencyMs, *Decision.Reason);
	}
}

// ============================================================================
//  采样 / 本地策略 / 落地
// ============================================================================

FARPGAIWorldSnapshot UARPGAIAgentComponent::SampleWorldSnapshot_Implementation()
{
	FARPGAIWorldSnapshot S;

	UWorld* World = GetWorld();
	if (!World)
	{
		return S;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return S;
	}

	const float Health = ReadFloatAttributeOnActor(Owner, HealthAttributeName);
	const float MaxHealth = ReadFloatAttributeOnActor(Owner, MaxHealthAttributeName);
	const float Stamina = ReadFloatAttributeOnActor(Owner, StaminaAttributeName);
	const float MaxStamina = ReadFloatAttributeOnActor(Owner, MaxStaminaAttributeName);

	S.SelfHealthPct = SafePct(Health, MaxHealth);
	S.SelfStaminaPct = SafePct(Stamina, MaxStamina);

	if (AActor* Target = FindTargetActor())
	{
		S.bHasTarget = true;
		// UE 距离一律是厘米
		S.DistanceToTarget = Owner->GetDistanceTo(Target);

		const float TargetHealth = ReadFloatAttributeOnActor(Target, HealthAttributeName);
		const float TargetMaxHealth = ReadFloatAttributeOnActor(Target, MaxHealthAttributeName);
		S.TargetHealthPct = SafePct(TargetHealth, TargetMaxHealth);
	}
	else
	{
		S.bHasTarget = false;
		S.DistanceToTarget = -1.f;
	}

	const double Now = World->GetTimeSeconds();
	S.TimeSinceLastAttack = (LastAttackTime < 0.0) ? 999.f : static_cast<float>(Now - LastAttackTime);
	S.SampleWorldTime = Now;

	// 同伴数量默认给 0：真要算得做一次球形重叠查询，在 Tick 里做太贵。
	// 需要的话在蓝图里覆写 SampleWorldSnapshot 自己填，或者只在决策那一刻算一次。
	S.AllyCount = 0;

	return S;
}

FARPGDecision UARPGAIAgentComponent::MakeLocalDecision_Implementation(const FARPGAIWorldSnapshot& Snapshot)
{
	const UARPGAIAgentSettings* Cfg = GetDefault<UARPGAIAgentSettings>();
	const float AttackRange = Cfg ? Cfg->AttackRangeCm : 250.f;
	const float ChaseRange = Cfg ? Cfg->ChaseRangeCm : 800.f;
	const float LowHealth = Cfg ? Cfg->LowHealthPct : 0.3f;
	const float Hold = Cfg ? Cfg->DecisionHoldSec : 0.5f;

	// 退出攻击距离的放大系数：进入 250cm、退出 312.5cm。
	// 中间这条带子（死区）用来吃掉边界抖动，值本身可以挪到 Settings 里做成可配。
	const float AttackExitRatio = 1.25f;

	FARPGDecision D;
	D.Source = EARPGDecisionSource::Local;
	D.HoldSec = Hold;

	if (!Snapshot.bHasTarget)
	{
		D.Action = EARPGAIAction::Idle;
		D.Confidence = 1.f;
		D.Reason = TEXT("no target");
		return D;
	}

	// 血量低：够得着就格挡，够不着就先撤
	if (Snapshot.SelfHealthPct <= LowHealth)
	{
		D.Action = (Snapshot.DistanceToTarget > AttackRange) ? EARPGAIAction::Retreat : EARPGAIAction::Block;
		D.Confidence = 0.9f;
		D.Reason = TEXT("low health");
		return D;
	}

	// 回滞：已经在范围内就用放大后的退出阈值，否则用进入阈值。
	// 注意这两个必须在「无目标」和「低血量」两条 return 之后才算——
	// 无目标时 DistanceToTarget 是 -1，会被误判成「在范围内」，状态就脏了。
	const float EffectiveRange = bWasInAttackRange ? (AttackRange * AttackExitRatio) : AttackRange;
	bWasInAttackRange = (Snapshot.DistanceToTarget <= EffectiveRange);

	// 在攻击距离内：体力够就上重击，不够就普通攻击
	if (Snapshot.DistanceToTarget <= EffectiveRange)
	{
		D.Action = (Snapshot.SelfStaminaPct >= 0.5f) ? EARPGAIAction::HeavyAttack : EARPGAIAction::Attack;
		D.Confidence = 0.85f;
		D.Reason = TEXT("in melee reach");
		return D;
	}

	if (Snapshot.DistanceToTarget <= ChaseRange)
	{
		D.Action = EARPGAIAction::Chase;
		D.Confidence = 0.8f;
		D.Reason = TEXT("chasing");
		return D;
	}

	D.Action = EARPGAIAction::Idle;
	D.Confidence = 0.6f;
	D.Reason = TEXT("out of range");
	return D;
}

void UARPGAIAgentComponent::ApplyDecision(const FARPGDecision& Decision)
{
	UWorld* World = GetWorld();

	// 修2：先记下旧动作，动作没变就别再激活技能（否则每次决策都重播蒙太奇，很丑也浪费）
	const EARPGAIAction OldAction = CurrentDecision.Action;
	CurrentDecision = Decision;

	if (World)
	{
		LastDecisionTime = World->GetTimeSeconds();
		if (Decision.Action == EARPGAIAction::Attack || Decision.Action == EARPGAIAction::HeavyAttack)
		{
			LastAttackTime = LastDecisionTime;
		}
	}

	// 先广播：让行为树 / 蓝图有机会自己处理（比如 BT Service 写黑板键）
	OnDecisionReady.Broadcast(Decision);

	// 修2 核心：动作没变就跳过技能激活，只更新时间戳和广播
	if (OldAction == Decision.Action)
	{
		return;
	}

	// 再按映射表激活 GAS 技能。没配映射就什么都不做（比如 Chase 由行为树负责移动）。
	UAbilitySystemComponent* Asc = GetOwnerAbilitySystem();
	if (!Asc)
	{
		return;
	}

	const FName ActionKey(*ARPGAIAgentUtil::ActionToString(Decision.Action));
	if (const TSubclassOf<UGameplayAbility>* Found = ActionAbilityMap.Find(ActionKey))
	{
		if (Found && *Found)
		{
			Asc->TryActivateAbilityByClass(*Found);
		}
	}
}

// ============================================================================
//  工具
// ============================================================================

void UARPGAIAgentComponent::SetRemoteEnabled(bool bInEnabled)
{
	bEnableRemote = bInEnabled;
	if (!bInEnabled && RemoteClient.IsValid())
	{
		// 立刻掐掉在飞的，后续也不会再发；回包不会再回来，所以必须把 slot 和标志一起清掉
		RemoteClient->CancelAll();
		bRequestInFlight.store(false, std::memory_order_release);
		ReleaseHeldSlot();
	}
}

void UARPGAIAgentComponent::ReleaseHeldSlot()
{
	if (!bSlotHeld)
	{
		return;   // 幂等：没占就不还，避免把别人的 slot 减多
	}
	bSlotHeld = false;
	if (UARPGAIAgentSubsystem* Sub = GetAgentSubsystem())
	{
		Sub->ReleaseRequestSlot();
	}
}

UAbilitySystemComponent* UARPGAIAgentComponent::GetOwnerAbilitySystem() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	const IAbilitySystemInterface* AsAsc = Cast<IAbilitySystemInterface>(Owner);
	if (!AsAsc)
	{
		return nullptr;
	}

	// GetAbilitySystemComponent() 本身是 const 方法，返回非 const 指针，不用转型
	return AsAsc->GetAbilitySystemComponent();
}

UARPGAIAgentSubsystem* UARPGAIAgentComponent::GetAgentSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UARPGAIAgentSubsystem>() : nullptr;
}

AActor* UARPGAIAgentComponent::FindTargetActor() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	// 优先从 AI 黑板拿目标（敌人走这条路）
	if (APawn* Pawn = Cast<APawn>(Owner))
	{
		if (AAIController* Ai = Cast<AAIController>(Pawn->GetController()))
		{
			if (UBlackboardComponent* Bb = Ai->GetBlackboardComponent())
			{
				if (UObject* Obj = Bb->GetValueAsObject(TargetBlackboardKey))
				{
					if (AActor* AsActor = Cast<AActor>(Obj))
					{
						return AsActor;
					}
				}
			}
		}
	}

	// 兜底：直接拿玩家（给玩家角色挂这个组件时有用）
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* Pc = World->GetFirstPlayerController())
		{
			return Pc->GetPawn();
		}
	}
	return nullptr;
}

float UARPGAIAgentComponent::ReadNumericAttribute(FName AttributeName) const
{
	return ReadFloatAttributeOnActor(GetOwner(), AttributeName);
}
