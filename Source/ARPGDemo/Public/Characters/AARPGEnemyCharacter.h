// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AARPGCharacterBase.h"
#include "Abilities/GameplayAbility.h"
#include "Components/WidgetComponent.h"
#include "UI/UARPGHUDWidget.h"
#include "UI/UARPGBossHealthBarWidget.h"
#include "AARPGEnemyCharacter.generated.h"

class UBoxComponent;
class UAnimMontage;
class UAnimSequence;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;

UCLASS()
class ARPGDEMO_API AAARPGEnemyCharacter : public AAARPGCharacterBase
{
	GENERATED_BODY()

public:
	AAARPGEnemyCharacter();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> AttackAbilityClass;// 敌人要用的攻击技能，在蓝图 Class Defaults 里配

	virtual void OnHealthChanged(const FOnAttributeChangeData& ChangeData) override;

	// 召唤点 / 剧情可以从外面改血条上显示的名字，不用回到蓝图里改
	UFUNCTION(BlueprintCallable, Category = "UI")
	void SetBossDisplayName(const FText& InName);

	// 是不是 Boss：死亡时给关卡进度打一个标记，StoneGolem 的召唤点只认这个标记。
	// Boss / StoneGolem 的蓝图里勾上，小怪保持不勾。
	UPROPERTY(EditDefaultsOnly, Category = "Boss")
	bool bIsBoss = false;

	// ---- 多段攻击（连招）----
	// 一招一个蒙太奇，按数组随机挑。石巨人用的话：
	// 把 AM_GolemAttack 复制成 AM_GolemAttack2 / 3 / 4，各自把 Slot 里的序列换成
	// AS_StoneGolem_attack2 / 3 / 4（复制会连 notify 一起带过来，伤害照样触发），
	// 然后四个一起拖进这个数组。
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	TArray<TObjectPtr<UAnimMontage>> ComboMontages;

	// 一次进攻最多连几段（2 = 砸一下再接一下，3 = 三连）
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	int32 ComboMaxSegments = 2;

	// 两段之间的空档，留 0.2~0.3 秒才有"抬手再来"的节奏，0 会看着像卡帧
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	float ComboSegmentGap = 0.25f;

	// 勾上以后 BTTask 不走 GA_GolemAttack，改由这里随机挑蒙太奇播（数组为空会自动退回旧逻辑）
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	bool bUseComboAttack = false;

	// BTTask 只问这一个，不直接读数组，省得判断散落各处
	UFUNCTION(BlueprintCallable, Category = "Attack|Combo")
	bool ShouldUseComboAttack() const { return bUseComboAttack && ComboMontages.Num() > 0; }

	// 连招入口：由 BTTask_ARPGAttack 调用，返回是否真的开打了
	UFUNCTION(BlueprintCallable, Category = "Attack|Combo")
	bool StartComboAttack();

	// 连招是否还在播（含段间空档）。
	// BTTask 靠它决定"这次攻击什么时候算结束"——返回 false 之前不许去走位，
	// 否则动画还没播完 AI 就开始挪步，看起来像怪物一边挥拳一边滑走。
	UFUNCTION(BlueprintCallable, Category = "Attack|Combo")
	bool IsComboActive() const { return bComboRunning; }

protected:
	// ---- 盾牌格挡 ----
	// 玩家靠近时，每隔 GuardCheckInterval 秒掷一次骰子，中了就举盾 GuardDuration 秒。
	// 举盾期间身上挂 State.Guard，伤害结算（UARPGAttributeSet）会按正面减免。
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	bool bCanGuard = false;

	// 多久判断一次"要不要举盾"
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	float GuardCheckInterval = 3.f;

	// 一次举盾持续多久
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	float GuardDuration = 1.5f;

	// 每次判断时举盾的概率，1 = 每次都举
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	float GuardTriggerChance = 0.5f;

	// 玩家进到这个距离以内才会想起来举盾
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	float GuardNoticeDistance = 900.f;

	// 盾牌挂在哪个 socket 上；留空就挂到 Mesh 根部（特效会贴在脚下，所以建议填）
	// 默认值在构造函数里给（UHT 对 FName 的默认值写法挑剔，不在声明处写 TEXT）
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	FName GuardAttachSocket;

	// 举盾动作，直接填动画序列即可（不用先做蒙太奇）。
	// 石巨人拿 AS_StoneGolem_dodge 顶：侧身迎击的姿态，比凭空浮一个魔法阵可信。
	// 播放走 ABP 里的 DefaultSlot 动态蒙太奇，所以动画蓝图必须留着那个 Slot 节点
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	TObjectPtr<UAnimSequence> GuardSequence;

	// 备用：如果你更想用自己做的蒙太奇，填这个（优先用 GuardSequence）
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	TObjectPtr<UAnimMontage> GuardMontage;

	// 举盾同时后撤：给一个朝"背离玩家"方向的瞬时冲量，靠摩擦力自然停下。
	// 0 = 原地举盾。注意单位是 cm/s，500 大约是"退半步"
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	float GuardBackstepSpeed = 500.f;

	// 举盾时裹在身上的护盾特效，留空 = 不显示。
	// 原来的默认值 NS_Shield_1 是一圈魔法阵，配石巨人的实体盾牌很违和，所以默认关掉
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	TObjectPtr<UNiagaraSystem> GuardEffect;

	// 举盾起手音 + 挡住那一下的金属声
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	TObjectPtr<USoundBase> GuardStartSound;

	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	TObjectPtr<USoundBase> GuardImpactSound;

	// 举盾期间受到伤害时放一下火花 + 音效（内部有冷却，不会被连击刷屏）
	void OnGuardImpact();

	// 挡住那一下的火花，默认空着 —— 想有火星就在蓝图里填 NS_BlockImpact 之类
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	TObjectPtr<UNiagaraSystem> GuardImpactEffect;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> GuardEffectComp;

	FTimerHandle GuardCheckTimer;
	FTimerHandle GuardEndTimer;
	float LastGuardImpactTime = -1000.f;
	bool bGuarding = false;

	void CheckGuard();
	void EndGuard();

	// ---- Boss 专属血条（屏幕正上方中间那条）----
	// 勾上以后头顶那个 WidgetComponent 就不用了，改成屏幕空间的一条长条
	UPROPERTY(EditDefaultsOnly, Category = "UI|Boss")
	bool bUseScreenBossBar = false;

	// 不填就直接用 C++ 那个自带布局的类（不用建 WBP 也能显示）
	UPROPERTY(EditDefaultsOnly, Category = "UI|Boss")
	TSubclassOf<UUARPGBossHealthBarWidget> BossBarWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Boss")
	FText BossDisplayName = FText::FromString(TEXT("Boss"));

	UPROPERTY()
	TObjectPtr<UUARPGBossHealthBarWidget> BossBarWidget;

	void CreateBossBar();
	void DestroyBossBar();
	// ---- 受击 / 警戒动画（都在各敌人 BP 的 Class Defaults 里配，两个怪各填各的）----
	// 受击：Grunt 填 AM_Grunt_Stunned（里面是 ExoGame_Gruntling_React_Stunned_Enter）
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> HitReactMontage;

	// 警戒：Troll 填一个里面是 ExoGame_Troll_Turn_Left 的 Montage
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> AlertMontage;

	// 两次受击动画之间至少隔这么久，否则被连击时会把动画一直顶掉
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	float HitReactCooldown = 0.35f;

	// 玩家进到这个距离以内就算"警戒到了"，播一次 AlertMontage
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	float AlertNoticeDistance = 1500.f;

	void PlayHitReact();
	void CheckAlert();

	FTimerHandle AlertCheckTimer;
	float LastHitReactTime = -1000.f;
	bool bAlertPlayed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UWidgetComponent> HealthBarComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Stats")
	TSubclassOf<UGameplayEffect> InitStatsEffect;

	// 血条挂点高度：Grunt 配 120，Boss 配 300 左右，两个蓝图各填各的
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	float HealthBarHeight = 120.f;

	// 受伤后多久把血条收起来
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	float HealthBarHideDelay = 3.f;

	FTimerHandle HealthBarHideTimer;

	void RefreshHealthBar();          // 读当前血量刷一次百分比
	void ShowHealthBarTemporarily();  // 显示并重开 3 秒计时
	void HideHealthBar();             // 计时到点后隐藏

	// ---- 连招内部状态 ----
	// 还剩几段没播
	int32 ComboSegmentsLeft = 0;
	// 上一段用的下标，用来避免连续两次播同一招
	int32 LastComboIndex = -1;
	FTimerHandle ComboTimer;
	// 整段连招（含最后一段）是否还没播完，BTTask 靠它判断攻击结束没有
	bool bComboRunning = false;

	void PlayNextComboSegment();
	void FinishCombo();

	virtual void HandleDeath() override;
};
