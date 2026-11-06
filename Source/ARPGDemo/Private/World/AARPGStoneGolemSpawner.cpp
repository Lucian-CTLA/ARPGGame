// Fill out your copyright notice in the Description page of Project Settings.

#include "World/AARPGStoneGolemSpawner.h"

#include "Characters/AARPGEnemyCharacter.h"
#include "World/UARPGCombatStateSubsystem.h"
#include "Components/BoxComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AAARPGStoneGolemSpawner::AAARPGStoneGolemSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	// 空场景根，这个 Actor 本身不显示任何东西
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// 点击盒：只 Block Visibility（鼠标射线走这条通道），
	// 对 Pawn / 物理全 Ignore —— 玩家可以穿过它，但鼠标点得到
	ClickBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ClickBox"));
	ClickBox->SetupAttachment(Root);
	ClickBox->SetBoxExtent(FVector(160.f, 160.f, 220.f));
	ClickBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ClickBox->SetCollisionObjectType(ECC_WorldDynamic);
	ClickBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	ClickBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// 默认特效：工程里现成的"巨人召唤"，是给 Boss 出场用的，正好对味
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SummonFxAsset(
		TEXT("/Game/Assets/Niagara/Summon/NS_GiantSummon.NS_GiantSummon"));
	if (SummonFxAsset.Succeeded())
	{
		SummonEffect = SummonFxAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> GroundFxAsset(
		TEXT("/Game/Assets/Niagara/GroundRocks/NS_GroundBurstRocks.NS_GroundBurstRocks"));
	if (GroundFxAsset.Succeeded())
	{
		GroundEffect = GroundFxAsset.Object;
	}
}

void AAARPGStoneGolemSpawner::BeginPlay()
{
	Super::BeginPlay();

	// 引擎默认是"不接收 Actor 点击"的（APlayerController::bEnableClickEvents = false），
	// 这里帮玩家开一下，省得再去建一个 PC 蓝图设置它。
	// 只对本地玩家生效，AI 控制器上没有这条路径，不影响性能。
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bEnableClickEvents = true;
	}
}

bool AAARPGStoneGolemSpawner::TriggerSpawn()
{
	if (bSpawned)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!GolemClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StoneGolem] Spawner 没有设置 GolemClass，先把 BP_StoneGolemEnemy 填进 Details"));
		return false;
	}

	// 可选的前置条件：勾上才要求"前面那只 Boss 已经打过"。
	// 默认是关的 —— 点灯就召唤，玩家跟灯交互完直接开打。
	// 这里必须把 State 为空也算成"条件不满足"：早期写成 if (State) {...}，
	// 一旦 WorldSubsystem 没建出来，整段检查会被静默跳过，等于放行。
	if (bRequireBossDefeated)
	{
		UARPGCombatStateSubsystem* State = World->GetSubsystem<UARPGCombatStateSubsystem>();
		if (!State || !State->IsBossDefeated())
		{
			UE_LOG(LogTemp, Warning, TEXT("[StoneGolem] Boss 还没被打死，召唤不生效"));
			return false;
		}
	}

	bSpawned = true;

	const FVector TargetLocation = bSpawnAtSpawnerLocation ? GetActorLocation() : SpawnLocation;

	// 火光先炸：位置对齐怪物的落脚点，别用召唤点自己的位置（两者可能不在一处）
	if (SummonEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, SummonEffect, TargetLocation, FRotator::ZeroRotator,
			FVector(EffectScale), true, true, ENCPoolMethod::None, true);
	}
	if (GroundEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, GroundEffect, TargetLocation, FRotator::ZeroRotator,
			FVector(EffectScale), true, true, ENCPoolMethod::None, true);
	}
	if (SummonSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SummonSound, TargetLocation);
	}

	// 火光烧一小会儿，怪物再从里面走出来。
	// 注意：SpawnDelay <= 0 时不能交给 SetTimer —— Engine/Private/TimerManager.cpp:617
	// 只处理 InRate > 0 的情况，0 或负数会走 else 把 Handle 直接 Invalidate，
	// 既不报错也不回调，表现就是"火光炸了但怪物永远不出来"。所以延迟为 0 就直接生成。
	if (SpawnDelay > 0.f)
	{
		World->GetTimerManager().SetTimer(SpawnTimer, this, &AAARPGStoneGolemSpawner::DoSpawn, SpawnDelay, false);
	}
	else
	{
		DoSpawn();
	}

	UE_LOG(LogTemp, Warning, TEXT("[StoneGolem] 火光已点燃，%.2f 秒后在 (%.0f, %.0f, %.0f) 召唤"),
		SpawnDelay, TargetLocation.X, TargetLocation.Y, TargetLocation.Z);

	return true;
}

void AAARPGStoneGolemSpawner::DoSpawn()
{
	UWorld* World = GetWorld();
	if (!World || !GolemClass)
	{
		return;
	}

	const FVector TargetLocation = bSpawnAtSpawnerLocation ? GetActorLocation() : SpawnLocation;
	// 朝向沿用召唤点自己的 Yaw，这样在关卡里转召唤点就能控制怪物出场时面朝哪边
	const FRotator TargetRotation = FRotator(0.f, GetActorRotation().Yaw, 0.f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AAARPGEnemyCharacter* Golem = World->SpawnActor<AAARPGEnemyCharacter>(
		GolemClass, TargetLocation, TargetRotation, Params);

	if (!Golem)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StoneGolem] SpawnActor 失败，检查 GolemClass 是否是 AAARPGEnemyCharacter 的子类"));
		return;
	}

	// 血条上的名字由召唤点统一指定，避免每只怪都要去蓝图里改一遍
	if (!GolemDisplayName.IsEmpty())
	{
		Golem->SetBossDisplayName(GolemDisplayName);
	}

	// 记一笔，别的地方（比如后续剧情/UI）想查"石巨人是不是已经出来了"直接问这个
	if (UARPGCombatStateSubsystem* State = World->GetSubsystem<UARPGCombatStateSubsystem>())
	{
		State->NotifyStoneGolemSpawned();
	}

	UE_LOG(LogTemp, Warning, TEXT("[StoneGolem] %s 已生成"), *Golem->GetName());
}

void AAARPGStoneGolemSpawner::NotifyActorOnClicked(FKey ButtonPressed)
{
	Super::NotifyActorOnClicked(ButtonPressed);

	TriggerSpawn();
}
