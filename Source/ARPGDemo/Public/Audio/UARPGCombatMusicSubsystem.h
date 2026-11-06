#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UARPGCombatMusicSubsystem.generated.h"

class USoundBase;
class UAudioComponent;

/**
 * C 组 · 战斗音乐：进入战斗自动淡入 BGM，脱离战斗自动淡出。
 *
 * 用 UTickableWorldSubsystem 实现——随 UWorld 自动创建和销毁，
 * 所以【不需要在关卡里放任何 Actor】【不需要改任何蓝图】【不需要改 ini】。
 * 编译进 Editor DLL 之后，只要关卡里有敌人，按 Play 就自动生效。
 *
 * 判定带回滞，避免在边界距离反复开关音乐：
 *   任一敌人 <= 800cm  -> 进入战斗
 *   所有敌人 > 1500cm  -> 脱离战斗
 * 这两个值与 BTService_ARPGPreception 的 ChaseDistance / AlertDistance 对齐。
 *
 * BGM 用的是 /Game/Assets/Sounds/Music/Ice_BossBattle01_Cue，
 * 它本身挂在 SC_Master 上，所以 A 组菜单的音量滑块同样能控制战斗音乐。
 */
UCLASS()
class ARPGDEMO_API UARPGCombatMusicSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void StartMusic();
	void StopMusic();

	// 资产按路径加载，不用在编辑器里拖引用；UPROPERTY 保活
	UPROPERTY()
	TObjectPtr<USoundBase> CombatBGM;

	// 必须 UPROPERTY 保活，否则播放过程中会被 GC，音乐突然断掉
	UPROPERTY()
	TObjectPtr<UAudioComponent> MusicComponent;

	// 进入战斗距离（追击阈值）
	float EnterCombatDistance = 800.f;

	// 脱离战斗距离（警戒阈值），比进入距离大，形成回滞
	float LeaveCombatDistance = 1500.f;

	// 每 0.25 秒扫一次敌人距离，不用每帧算
	float CheckInterval = 0.25f;
	float TimeSinceCheck = 0.f;

	bool bInCombat = false;
};
