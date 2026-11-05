// Fill out your copyright notice in the Description page of Project Settings.


#include "AnimInstance/ARPGAnimNSMeleeHitWindow.h"

#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "TimerManager.h"

#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "NiagaraFunctionLibrary.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AttributeSet/UARPGAttributeSet.h"

#include "AARPGCharacterBase.h"
#include "Characters/AARPGEnemyCharacter.h"

namespace
{
	/** GE 里 SetByCaller 的 Data Tag 名（必须 SetByCaller. 开头） */
	const FName DamageTagName(TEXT("SetByCaller.Damage"));

	/** 死亡 Tag，死了就不该再输出伤害 */
	const FName DeadTagName(TEXT("State.Dead"));

	/**
	 * 敌我判定：Hero 和 Enemy 是两条不同的继承线，只要「是不是 Enemy」不同就算敌对。
	 * 这样 Hero 打 Enemy 成立、Enemy 打 Hero 成立、Enemy 之间互不误伤。
	 */
	bool IsHostile(const AAARPGCharacterBase* A, const AAARPGCharacterBase* B)
	{
		if (!A || !B || A == B)
		{
			return false;
		}
		const bool bAIsEnemy = A->IsA<AAARPGEnemyCharacter>();
		const bool bBIsEnemy = B->IsA<AAARPGEnemyCharacter>();
		return bAIsEnemy != bBIsEnemy;
	}
}

UARPGAnimNSMeleeHitWindow::UARPGAnimNSMeleeHitWindow()
{
	// 构造函数里 CDO 就要能拿到 Tag。第二个参数 false = 找不到别断言，
	// 这样在 GameplayTag 还没从 ini 加载完的极早期也不会崩。
	DamageSetByCallerTag = FGameplayTag::RequestGameplayTag(DamageTagName, /*ErrorIfNotFound=*/false);
}


// =============================================================================
// NotifyBegin：窗口开始
// =============================================================================
void UARPGAnimNSMeleeHitWindow::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (!MeshComp)
	{
		return;
	}

	// 顺手清理失效的 Key：角色被销毁时若 NotifyEnd 没跑到，表里会留一个空 Key。
	// 每次 Begin 扫一遍，表就不会越用越大（这是资产级共享对象，会活一整个进程）。
	for (auto It = WindowStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	// 按 MeshComp 取（或建）自己那一份状态 —— 关键：不同角色互不干扰
	const TWeakObjectPtr<USkeletalMeshComponent> Key(MeshComp);
	FARPGMeleeHitWindowState& State = WindowStates.FindOrAdd(Key);

	// 整体重置。用赋值而不是逐个字段清空，避免漏清（比如 HitTargets 忘了 Clear）
	State = FARPGMeleeHitWindowState();
	State.Duration = FMath::Max(TotalDuration, KINDA_SMALL_NUMBER); // 防 0 除
	State.LastCenter = ComputeCenter(MeshComp, 0.f);

	UE_LOG(LogTemp, Verbose, TEXT("[MeleeWindow] begin on %s, duration=%.2f"),
		*GetNameSafe(MeshComp->GetOwner()), TotalDuration);
}


// =============================================================================
// NotifyTick：窗口内每帧扫一次
// =============================================================================
void UARPGAnimNSMeleeHitWindow::NotifyTick(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	// ---- 前置检查：任何一项不成立就直接返回，不浪费一次扫掠 ----
	if (!MeshComp || !DamageEffectClass)
	{
		return;
	}

	AAARPGCharacterBase* Attacker = Cast<AAARPGCharacterBase>(MeshComp->GetOwner());
	if (!Attacker)
	{
		return;
	}

	UAbilitySystemComponent* AttackerASC = Attacker->GetAbilitySystemComponent();
	if (!AttackerASC)
	{
		return;
	}

	// 攻击者已经死了（比如出招途中被打死），剩下的窗口不该再造成伤害
	if (AttackerASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(DeadTagName, false)))
	{
		return;
	}

	// 找到这个 MeshComp 对应的状态；没找到说明 Begin 没跑到（极少见），直接跳过
	FARPGMeleeHitWindowState* State = WindowStates.Find(TWeakObjectPtr<USkeletalMeshComponent>(MeshComp));
	if (!State)
	{
		return;
	}

	// ---- 1. 推进时间，算出这一帧的球心 ----
	State->Elapsed += FrameDeltaTime;
	const float Alpha = FMath::Clamp(State->Elapsed / State->Duration, 0.f, 1.f);
	const FVector Center = ComputeCenter(MeshComp, Alpha);

	// ---- 2. 从「上一帧球心」扫到「这一帧球心」----
	// 为什么要两帧之间扫，而不是只看当前点？
	// 刀挥得快的时候，一帧能移动几十厘米，如果只查当前点，敌人正好卡在两帧之间就会漏判。
	TArray<AActor*> Candidates;
	CollectTargets(MeshComp, State->LastCenter, Center, Candidates);
	State->LastCenter = Center;

	if (Candidates.Num() == 0)
	{
		return;
	}

	// ---- 3. 筛选 + 去重 + 结算 ----
	const float Damage = GetAttackDamage(AttackerASC);
	int32 AppliedCount = 0;

	for (AActor* Candidate : Candidates)
	{
		AAARPGCharacterBase* Target = Cast<AAARPGCharacterBase>(Candidate);
		if (!Target || Target == Attacker)
		{
			continue;
		}

		// 阵营过滤：不关掉的话 Boss 一挥刀就把旁边杂兵全砍死
		if (bRequireHostile && !IsHostile(Attacker, Target))
		{
			continue;
		}

		UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();
		if (!TargetASC)
		{
			continue;
		}

		// 打尸体没有意义
		if (TargetASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(DeadTagName, false)))
		{
			continue;
		}

		// 去重：这次窗口里已经打过他就跳过
		const TWeakObjectPtr<AActor> TargetKey(Target);
		if (bSingleHitPerTarget && State->HitTargets.Contains(TargetKey))
		{
			continue;
		}

		State->HitTargets.Add(TargetKey);
		ApplyHit(Attacker, Target, MeshComp, Damage);
		++AppliedCount;
	}

	if (AppliedCount == 0)
	{
		return;
	}

	// ---- 4. 震屏：一帧最多一次，别按命中人数叠加 ----
	if (HitShakeClass)
	{
		// 注意：攻击者可能是 AI，Cast<APlayerController>(Attacker->GetController()) 必失败，
		// 所以统一从世界取玩家控制器 —— 这样「Boss 打玩家」才会震屏。
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(MeshComp, 0))
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->StartCameraShake(HitShakeClass, 1.f);
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[MeleeWindow] %s hit %d target(s), dmg=%.1f"),
		*GetNameSafe(Attacker), AppliedCount, Damage);
}


// =============================================================================
// NotifyEnd：窗口结束（动画被打断时引擎也会调用）
// =============================================================================
void UARPGAnimNSMeleeHitWindow::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	// 必须在这里删掉本 MeshComp 的条目，否则：
	//   1. 下次同一个角色再挥刀，Begin 虽然会重置，但销毁的角色会永久占着 Key；
	//   2. 去重表会残留上一刀的记录。
	// NotifyEnd 一定会在窗口结束或动画中断（Montage_Stop / 被打断 / Actor 销毁）时被调用，
	// 所以清理放这里是安全的。
	WindowStates.Remove(TWeakObjectPtr<USkeletalMeshComponent>(MeshComp));
}


FString UARPGAnimNSMeleeHitWindow::GetNotifyName_Implementation() const
{
	return TEXT("Melee Hit Window");
}


// =============================================================================
// ComputeCenter：当前判定球球心
// =============================================================================
FVector UARPGAnimNSMeleeHitWindow::ComputeCenter(USkeletalMeshComponent* MeshComp, float Alpha) const
{
	if (!MeshComp)
	{
		return FVector::ZeroVector;
	}

	// 首选武器 socket：它跟着骨骼动画走，刀在哪就判在哪
	if (SweepSocket != NAME_None && MeshComp->DoesSocketExist(SweepSocket))
	{
		return MeshComp->GetSocketLocation(SweepSocket);
	}

	// 兜底：角色位置 + 前向偏移（从 Start 插值到 End），模拟「刀往前扫」的轨迹
	AActor* Owner = MeshComp->GetOwner();
	const FVector Origin = Owner ? Owner->GetActorLocation() : MeshComp->GetComponentLocation();
	const FVector Forward = Owner ? Owner->GetActorForwardVector() : MeshComp->GetForwardVector();
	const float Offset = FMath::Lerp(StartForwardOffset, EndForwardOffset, Alpha);

	return Origin + Forward * Offset + FVector(0.f, 0.f, HeightOffset);
}


// =============================================================================
// CollectTargets：扫掠 / 重叠
// =============================================================================
void UARPGAnimNSMeleeHitWindow::CollectTargets(
	USkeletalMeshComponent* MeshComp,
	const FVector& From,
	const FVector& To,
	TArray<AActor*>& OutActors) const
{
	if (!MeshComp)
	{
		return;
	}

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(MeshComp->GetOwner());

	const EDrawDebugTrace::Type DrawType = bDrawDebug ? EDrawDebugTrace::ForOneFrame : EDrawDebugTrace::None;

	// 球心几乎没动（站着蓄力 / socket 卡住）—— 用重叠查询，长度 0 的扫掠没有意义
	if (FVector::DistSquared(From, To) < 1.f)
	{
		UKismetSystemLibrary::SphereOverlapActors(
			MeshComp,
			To,
			SphereRadius,
			ObjectTypes,
			AAARPGCharacterBase::StaticClass(), // 只要角色，省掉一堆道具/触发器
			ActorsToIgnore,
			OutActors);
		return;
	}

	// 正常情况：从上一帧球心扫到这一帧球心，半径 SphereRadius
	TArray<FHitResult> Hits;
	UKismetSystemLibrary::SphereTraceMultiForObjects(
		MeshComp,
		From,
		To,
		SphereRadius,
		ObjectTypes,
		/*bTraceComplex=*/false,
		ActorsToIgnore,
		DrawType,
		Hits,
		/*bIgnoreSelf=*/true);

	// 一次扫掠可能对同一个 Actor 产生多个 Hit（多段命中），所以用 AddUnique 去一下重复
	for (const FHitResult& Hit : Hits)
	{
		if (AActor* HitActor = Hit.GetActor())
		{
			OutActors.AddUnique(HitActor);
		}
	}
}


// =============================================================================
// GetAttackDamage：读属性
// =============================================================================
float UARPGAnimNSMeleeHitWindow::GetAttackDamage(UAbilitySystemComponent* ASC) const
{
	if (!ASC)
	{
		return FallbackDamage;
	}

	bool bFound = false;
	const float Value = UAbilitySystemBlueprintLibrary::GetFloatAttributeFromAbilitySystemComponent(
		ASC,
		UUARPGAttributeSet::GetAttackPowerAttribute(),
		bFound);

	return bFound ? Value : FallbackDamage;
}


// =============================================================================
// ApplyHit：结算单个目标
// =============================================================================
void UARPGAnimNSMeleeHitWindow::ApplyHit(
	AAARPGCharacterBase* Attacker,
	AAARPGCharacterBase* Target,
	USkeletalMeshComponent* MeshComp,
	float Damage) const
{
	if (!Attacker || !Target || !MeshComp || !DamageEffectClass)
	{
		return;
	}

	UAbilitySystemComponent* AttackerASC = Attacker->GetAbilitySystemComponent();
	UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();
	if (!AttackerASC || !TargetASC)
	{
		return;
	}

	// ---- 1. 造 Spec，用 SetByCaller 把伤害值塞进去 ----
	FGameplayEffectContextHandle ContextHandle = AttackerASC->MakeEffectContext();
	ContextHandle.AddSourceObject(Attacker);

	FGameplayEffectSpecHandle SpecHandle = AttackerASC->MakeOutgoingSpec(DamageEffectClass, 1.f, ContextHandle);
	if (!SpecHandle.IsValid())
	{
		return;
	}

	// 只能用 Tag 版重载！GE 里选了 Data Tag 之后再用 FName 版 SetSetByCallerMagnitude 会静默失效（伤害恒 0）
	const FGameplayTag TagToUse = DamageSetByCallerTag.IsValid()
		? DamageSetByCallerTag
		: FGameplayTag::RequestGameplayTag(DamageTagName, false);

	if (TagToUse.IsValid())
	{
		SpecHandle.Data->SetSetByCallerMagnitude(TagToUse, Damage);
	}

	AttackerASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data, TargetASC);

	// ---- 2. 特效与音效 ----
	const FVector HitLoc = Target->GetActorLocation() + FVector(0.f, 0.f, 80.f);

	if (HitVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			MeshComp, HitVFX, HitLoc, FRotator::ZeroRotator, FVector(1.f), true, true);
	}
	if (HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(MeshComp, HitSound, HitLoc);
	}

	// ---- 3. 卡肉 ----
	// 只改 Actor 级 CustomTimeDilation，绝不用 SetGlobalTimeDilation：
	// 后者会把「恢复用的 Timer」也一起拖慢，结果永远恢复不回来。
	if (!bApplyHitStop)
	{
		return;
	}

	Target->CustomTimeDilation = HitStopDilation;
	Attacker->CustomTimeDilation = HitStopDilation;

	// 用弱指针捕获：这 0.08 秒里角色可能已经死了被销毁，强指针会让 GC 保活/访问崩溃
	TWeakObjectPtr<AAARPGCharacterBase> WeakTarget(Target);
	TWeakObjectPtr<AAARPGCharacterBase> WeakAttacker(Attacker);

	// Timer 挂在 World 上，不挂在攻击者身上 —— 挂攻击者的话，攻击者一死 Timer 就跟着没了，目标会永久停在 0.05 倍速
	FTimerHandle HitStopHandle;
	Target->GetWorld()->GetTimerManager().SetTimer(
		HitStopHandle,
		FTimerDelegate::CreateLambda([WeakTarget, WeakAttacker]()
			{
				if (WeakTarget.IsValid())  { WeakTarget->CustomTimeDilation = 1.f; }
				if (WeakAttacker.IsValid()) { WeakAttacker->CustomTimeDilation = 1.f; }
			}),
		HitStopDuration,
		false);
}
