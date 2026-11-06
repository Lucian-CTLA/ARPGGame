// Fill out your copyright notice in the Description page of Project Settings.

#include "World/AARPGCheckpoint.h"
#include "World/UARPGCheckpointSubsystem.h"
#include "Characters/AARPGHeroCharacter.h"

#include "Components/SphereComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AARPGCheckpoint::AARPGCheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerSphere = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerSphere"));
	SetRootComponent(TriggerSphere);
	TriggerSphere->InitSphereRadius(300.f);

	// 用引擎自带的 Trigger 预设：QueryOnly + 对所有物体类型 Overlap，
	// 和 ATriggerVolume 一模一样 —— 玩家能穿过去、又能收到重叠事件。
	// 自己乱配响应最容易出的问题是 Pawn 预设默认 Block，结果存档点把玩家挡住
	TriggerSphere->SetCollisionProfileName(TEXT("Trigger"));
	TriggerSphere->SetGenerateOverlapEvents(true);
	TriggerSphere->SetHiddenInGame(true);   // 游戏里不画那个绿球，只在编辑器里当摆位辅助

	// 常驻光效：治疗石，远远能看到一点微光，比空地上一个看不见的球友好
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> IdleFxAsset(
		TEXT("/Game/Assets/Niagara/Stones/NS_HealingStone.NS_HealingStone"));
	if (IdleFxAsset.Succeeded())
	{
		IdleEffect = IdleFxAsset.Object;
	}

	// 踩中时的爆发
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ActivateFxAsset(
		TEXT("/Game/Assets/Niagara/Stones/NS_ConsumeHealingStoneBurst.NS_ConsumeHealingStoneBurst"));
	if (ActivateFxAsset.Succeeded())
	{
		ActivateEffect = ActivateFxAsset.Object;
	}

	// 治疗音效当"已记录"的反馈，工程里现成的
	static ConstructorHelpers::FObjectFinder<USoundBase> ActivateSoundAsset(
		TEXT("/Game/Assets/Sounds/Character/SC_Character_Heal.SC_Character_Heal"));
	if (ActivateSoundAsset.Succeeded())
	{
		ActivateSound = ActivateSoundAsset.Object;
	}
}

void AARPGCheckpoint::BeginPlay()
{
	Super::BeginPlay();

	if (TriggerSphere)
	{
		// 在 BeginPlay 绑而不是构造函数绑：构造期组件还没注册，事件派发时机也不确定
		TriggerSphere->OnComponentBeginOverlap.AddDynamic(this, &AARPGCheckpoint::OnSphereBeginOverlap);
	}

	// 常驻光效挂在球上，跟着存档点走。
	// 注意 5.7 的 SpawnSystemAttached 没有 Scale 参数（只有 SpawnSystemAtLocation 有），
	// 缩放要等生成出来以后对组件设 RelativeScale3D
	if (IdleEffect && TriggerSphere)
	{
		IdleEffectComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			IdleEffect, TriggerSphere, NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset, false);

		if (IdleEffectComponent)
		{
			IdleEffectComponent->SetRelativeScale3D(FVector(EffectScale));
		}
	}
}

FTransform AARPGCheckpoint::GetRespawnTransform() const
{
	// 抬高一点：胶囊体中心离地 100 左右才是"站在地面上"，
	// 直接用 Actor 位置生成，有大概率被生成进地里再被挤出来
	const FVector Location = GetActorLocation() + FVector(0.f, 0.f, RespawnHeightOffset);

	FRotator Rotation = bUseCheckpointRotation ? GetActorRotation() : FRotator::ZeroRotator;
	// 只保留偏航：存档点被摆歪了（有人会用细节面板转一点角度）也不会让角色复活时倾斜
	Rotation.Pitch = 0.f;
	Rotation.Roll = 0.f;

	return FTransform(Rotation, Location);
}

void AARPGCheckpoint::Activate(AActor* Activator)
{
	// 单机也满足 HasAuthority，写在这里是为了以后改联机时不用返工
	if (!HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 1) 记坐标：先写状态再放特效，万一特效资源丢了，存档逻辑照样成立
	if (UARPGCheckpointSubsystem* CheckpointState = World->GetSubsystem<UARPGCheckpointSubsystem>())
	{
		CheckpointState->SetCheckpoint(CheckpointId, GetRespawnTransform());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Checkpoint] %s 取不到 UARPGCheckpointSubsystem，存档没记住"), *GetName());
		return;
	}

	// 2) 再放反馈
	if (!bEffectPlayed || !bPlayEffectOnlyOnce)
	{
		bEffectPlayed = true;

		if (ActivateEffect)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				World, ActivateEffect, GetActorLocation(), GetActorRotation(),
				FVector(EffectScale), true, true, ENCPoolMethod::None, true);
		}
		if (ActivateSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, ActivateSound, GetActorLocation());
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[Checkpoint] %s 触发存档 %s，复活点 (%.0f, %.0f, %.0f)"),
		*GetNameSafe(Activator),
		*CheckpointId.ToString(),
		GetRespawnTransform().GetLocation().X,
		GetRespawnTransform().GetLocation().Y,
		GetRespawnTransform().GetLocation().Z);
}

void AARPGCheckpoint::OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	// 只有玩家踩才记；敌人、投射物、掉落的石头路过不算
	if (AAARPGHeroCharacter* Hero = Cast<AAARPGHeroCharacter>(OtherActor))
	{
		Activate(Hero);
	}
}
