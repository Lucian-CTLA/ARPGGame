#include "Audio/UARPGCombatMusicSubsystem.h"

#include "Characters/AARPGEnemyCharacter.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"                 // TActorIterator
#include "Kismet/GameplayStatics.h"      // GetPlayerPawn / CreateSound2D
#include "Sound/SoundBase.h"

void UARPGCombatMusicSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// 必须转发给 Super：UTickableWorldSubsystem 靠 Initialize 才开始 Tick
	Super::Initialize(Collection);

	CombatBGM = LoadObject<USoundBase>(nullptr,
		TEXT("/Game/Assets/Sounds/Music/Ice_BossBattle01_Cue.Ice_BossBattle01_Cue"));

	if (!CombatBGM)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CombatMusic] 找不到 Ice_BossBattle01_Cue，战斗音乐不会播放"));
	}
}

void UARPGCombatMusicSubsystem::Deinitialize()
{
	// PIE 结束 / 切关卡时停掉，避免音乐残留到下一个世界
	if (MusicComponent)
	{
		MusicComponent->Stop();
		MusicComponent->DestroyComponent();
		MusicComponent = nullptr;
	}

	Super::Deinitialize();
}

TStatId UARPGCombatMusicSubsystem::GetStatId() const
{
	// 引擎里 UTickableWorldSubsystem 子类的标准写法（ExternalDirtyActorsTracker.h:109）
	return TStatId();
}

void UARPGCombatMusicSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		// 编辑器预览世界不播音乐
		return;
	}

	TimeSinceCheck += DeltaTime;
	if (TimeSinceCheck < CheckInterval)
	{
		return;
	}
	TimeSinceCheck = 0.f;

	APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!Player)
	{
		StopMusic();
		bInCombat = false;
		return;
	}

	// 回滞：战斗中用「脱离距离」判定，脱战中用「进入距离」判定
	const float Threshold = bInCombat ? LeaveCombatDistance : EnterCombatDistance;

	bool bEnemyNear = false;
	for (TActorIterator<AAARPGEnemyCharacter> It(World); It; ++It)
	{
		const AAARPGEnemyCharacter* Enemy = *It;
		if (!IsValid(Enemy))
		{
			continue;
		}
		if (FVector::Dist(Enemy->GetActorLocation(), Player->GetActorLocation()) <= Threshold)
		{
			bEnemyNear = true;
			break;
		}
	}

	if (bEnemyNear && !bInCombat)
	{
		bInCombat = true;
		StartMusic();
	}
	else if (!bEnemyNear && bInCombat)
	{
		bInCombat = false;
		StopMusic();
	}

	// SoundCue 默认播完就停：战斗中若已停止就重播，等价于循环 BGM
	if (bInCombat && MusicComponent && !MusicComponent->IsPlaying())
	{
		MusicComponent->Play();
	}
}

void UARPGCombatMusicSubsystem::StartMusic()
{
	UWorld* World = GetWorld();
	if (!World || !CombatBGM)
	{
		return;
	}

	if (!MusicComponent)
	{
		// 最后两个参数：bPersistAcrossLevelTransition=false、bAutoDestroy=false。
		// bAutoDestroy 必须关掉，否则播完组件就被销毁，脱战时没法 FadeOut。
		MusicComponent = UGameplayStatics::CreateSound2D(
			World, CombatBGM.Get(), 1.f, 1.f, 0.f, nullptr, false, false);
	}

	if (MusicComponent)
	{
		// 先 Stop 清掉上一次 FadeOut 的状态，再淡入；每次进战斗从曲子开头播
		MusicComponent->Stop();
		MusicComponent->FadeIn(0.6f, 1.f);
	}
}

void UARPGCombatMusicSubsystem::StopMusic()
{
	if (MusicComponent)
	{
		MusicComponent->FadeOut(0.8f, 0.f);
	}
}
