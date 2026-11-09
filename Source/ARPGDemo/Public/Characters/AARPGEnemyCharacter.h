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
class UBrainComponent;
class UTextRenderComponent;

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

	// ---- 分档移动速度 ----
	// ABP_StoneGolem 的移动是状态机 + BS_MM_WalkRun，由 GroundSpeed 决定播 walk 还是 jog，
	// 所以"不同状态配不同动作"不用另做动画，改速度就够：慢就是走，快就是跑。
	// 这些值由 BTService_ARPGPreception 按当前距离档位每 0.2 秒刷一次。
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float PatrolSpeed = 150.f;   // 游荡：慢悠悠逛，对应 walkF

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float AlertSpeed = 200.f;    // 警戒：转身盯着玩家，小幅挪动

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float ChaseSpeed = 320.f;    // 追击：小跑，对应 jogF

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float CombatSpeed = 180.f;   // 贴身战斗：压住速度，免得挥拳时整个人飘过去

	// 按感知状态切速度。State 取值和 BTService_ARPGPreception 里写黑板的 0~3 一致。
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void ApplyMoveSpeedForState(int32 State);

	// 感知状态的统一入口：BTService 每 0.2 秒调一次。
	// 内部一次做三件事——切速度、切警戒表现（循环动作 + 头顶感叹号）、脱战补游荡点。
	// 以前 Service 直接调 ApplyMoveSpeedForState，只能改速度，改不了动作，
	// 所以"警戒播 turn180""游荡走来走去"这类需求没有落点。
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SetPerceptionState(int32 NewState);

	// 0 游荡 / 1 警戒 / 2 追击 / 3 战斗
	UFUNCTION(BlueprintPure, Category = "Movement")
	int32 GetPerceptionState() const { return CurrentPerceptionState; }

	// ---- 连招期间的跟随 ----
	// 连招不再把控制权完全交给动画：每 ComboFollowInterval 秒量一次距离，
	// 玩家还在 ComboFollowMaxRange 内就慢慢挪过去，免得第 4、5 段对着空气挥。
	// 速度压在 ComboFollowSpeed（一百出头），所以看起来是"压着人打"，不是整个人滑出去。
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	float ComboFollowMaxRange = 520.f;

	// 贴进这个距离就停住。再往前挤会把玩家顶穿，反而打不中
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	float ComboFollowMinRange = 160.f;

	// 连招期间允许的最大移动速度。数值越大越容易滑步，100 左右是挪步
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	float ComboFollowSpeed = 110.f;

	// 多久重新下一次移动指令。太密会反复重建路径，0.2 秒够用
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	float ComboFollowInterval = 0.2f;

	// ---- 脱战游荡 ----
	// 行为树 ComboState == 0 的那个分支如果只挂了 Wait，敌人就会站着不动。
	// 这里在"没有别人在给它下发移动"的时候补一个随机可达点，让它真的走来走去。
	// 行为树自己在走的时候不插手，所以不会两边抢目标。
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	bool bUseCodePatrol = true;

	// 随机点最远挑多远（厘米）
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float PatrolWanderRadius = 700.f;

	// 玩家离得比这个还远才算"放羊"，才会去挑游荡点（厘米）。
	// 要和 BTService_ARPGPreception 的 AlertDistance 填一样的值（默认都是 1000）。
	// 为什么不用 CurrentPerceptionState 判断：那个状态是行为树上的 Service 每 0.2 秒刷的，
	// 行为树一旦卡在别的分支（或者 Service 没挂对），状态就永远停在旧值，
	// 游荡这条分支一辈子进不去 —— 距离是当场量的，不依赖别人有没有在跑
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float PatrolTriggerDistance = 1000.f;

	// 游荡决策打日志：为什么没走、挑不到点、寻路失败，全都打出来。
	// 排查完可以关掉，不然 Output Log 每隔几秒一条
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	bool bDebugWander = true;

	// 走到一个点之后停多久再挑下一个，实际取这两者之间的随机值
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float PatrolIdleMin = 2.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float PatrolIdleMax = 5.5f;

	// ---- 多段攻击（连招）----
	// 一招一个蒙太奇，按数组顺序往下打（1→2→3→4→5），打完一轮回到第一段。
	// 石巨人用的话：把 AM_GolemAttack 复制成 AM_GolemAttack2 / 3 / 4 / 5，各自把 Slot 里的序列换成
	// AS_StoneGolem_attack2 / 3 / 4 / 5（复制会连 notify 一起带过来，伤害照样触发），
	// 然后五个一起拖进这个数组。
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	TArray<TObjectPtr<UAnimMontage>> ComboMontages;

	// 一次进攻最多连几段（2 = 砸一下再接一下，5 = 五连）。
	// 超过蒙太奇数量的部分会循环取模，比如 5 个蒙太奇配 7 段，最后两段回到第 1、2 招。
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	int32 ComboMaxSegments = 5;

	// 两段之间的空档，留 0.2~0.3 秒才有"抬手再来"的节奏，0 会看着像卡帧
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	float ComboSegmentGap = 0.25f;

	// 段间空档的上限。ComboSegmentGap 在蓝图里被改大过（改到 1 秒以上就是
	// 五次互不相干的单击，不像连招），这里加一道封顶，保证五个动作还是连在一起的。
	// 想让连招更慢就把这个值调大，别去动 ComboSegmentGap
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	float MaxComboSegmentGap = 0.4f;

	// 连招打到一半玩家跑出这个距离就收手，别对着空气挥拳。
	// 比行为树的攻击距离（300）留一点余量，避免擦边就断
	UPROPERTY(EditDefaultsOnly, Category = "Attack|Combo")
	float ComboBreakDistance = 420.f;

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

	// 举盾时不该再开新连招，BTTask 要问这个再决定"续一轮"还是"交回行为树"
	UFUNCTION(BlueprintCallable, Category = "Guard")
	bool IsGuarding() const { return bGuarding; }

	// 收掉正在进行的连招：清计时器、把状态归位、停掉正在播的那段攻击蒙太奇
	void StopCombo();

	// 导航代理位置：基类实现是 ActorLocation - BaseEyeHeight（眼高固定 64），
	// 石巨人这种高个子算出来的点悬在半空，导航投影会失败，MoveTo 直接返回 Failed。
	// 这里改成"脚底"，路径查询和到达判定都以地面为准
	virtual FVector GetNavAgentLocation() const override;

protected:
	// ---- 盾牌格挡 ----
	// 玩家靠近时，每隔 GuardCheckInterval 秒掷一次骰子，中了就举盾 GuardDuration 秒。
	// 举盾期间身上挂 State.Guard，伤害结算（UARPGAttributeSet）会按正面减免。
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	bool bCanGuard = false;

	// 多久判断一次"要不要举盾"
	// 3 秒太粗：连招段间只有 0.25 秒空档，采样根本落不进去，表现就是"从来没见过它举盾"。
	// 现在是"玩家出招就挡"，而玩家一次挥击的起手只有 0.3~0.6 秒，
	// 采样必须比它密，否则整段错过 —— 0.2 秒刚好和感知 Service 同频
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	float GuardCheckInterval = 0.2f;

	// 两次举盾之间至少隔这么久（秒）。这里就是"每 10 秒只能挡一次"的那道闸
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	float GuardCooldown = 10.f;

	// 只在玩家真的出招时才举盾。关掉就退回老逻辑（纯掷骰），
	// 掷骰的问题是"该挡的时候不挡、不该挡的时候乱举盾"，看起来像发呆
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	bool bGuardOnPlayerAttack = true;

	// 玩家没出招时还允不允许随机举盾。默认关：
	// 你要的是"看到你抬手就挡"，而不是"没事也举一下"
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	bool bGuardOnRandom = false;

	// 玩家当前是不是正在出招：遍历它的 ASC，看有没有激活中的轻攻击技能。
	// 判据两条（命中任一即可）：技能类是 UUARPGGameplayAbility_LightAttack（BP 子类也认），
	// 或者技能资产上挂了 Ability.Attack.Light 标签
	bool IsPlayerAttacking(APawn* Player) const;

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

	// 举盾同时后撤：朝"背离玩家"的方向退 GuardBackstepDistance 厘米，用 GuardBackstepSpeed 的速度退完。
	// 走的是 root motion source 而不是 LaunchCharacter —— 冲量会被 MaxWalkSpeed 当场截断
	// （战斗态只有 180），实际只挪几十厘米，看起来就像没动。
	// 距离填 0 = 原地举盾。时长 = 距离 / 速度，别让它超过 GuardDuration。
	UPROPERTY(EditDefaultsOnly, Category = "Guard")
	float GuardBackstepDistance = 420.f;

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

	// 举盾期间被暂停的行为树。放盾时要按它恢复，否则敌人会一直傻站着
	TWeakObjectPtr<UBrainComponent> PausedBrain;

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

	// ---- 警戒表现（循环动作 + 头顶感叹号）----
	// 警戒状态循环播这个序列。ABP_StoneGolem 里没有 turn180 这个状态，
	// 所以只能走 DefaultSlot 动态播；播完由计时器重新播一次，实现"循环"。
	// 石巨人填 AS_StoneGolem_turn180
	UPROPERTY(EditDefaultsOnly, Category = "Alert")
	TObjectPtr<UAnimSequence> AlertSequence;

	// 播进哪个 Slot。ABP_StoneGolem 里那个节点叫 DefaultSlot
	// （FName 的默认值在构造函数里给，UHT 对声明处写 TEXT 挑剔）
	UPROPERTY(EditDefaultsOnly, Category = "Alert")
	FName AlertSlotName;

	// 循环播的播放速率，1 = 原速
	UPROPERTY(EditDefaultsOnly, Category = "Alert")
	float AlertAnimRate = 1.f;

	// 警戒时头顶显示感叹号
	UPROPERTY(EditDefaultsOnly, Category = "Alert")
	bool bShowAlertMark = true;

	// 感叹号放在胶囊顶部再往上多少厘米
	UPROPERTY(EditDefaultsOnly, Category = "Alert")
	float AlertMarkHeight = 60.f;

	// 感叹号字号（世界单位，石巨人个头大，48 差不多）
	UPROPERTY(EditDefaultsOnly, Category = "Alert")
	float AlertMarkSize = 48.f;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> AlertMarkComponent;

	// 当前正在播的那条动态蒙太奇。切状态时要停它，否则离开警戒还在转身
	UPROPERTY()
	TObjectPtr<UAnimMontage> AlertPlayingMontage;

	FTimerHandle AlertLoopTimer;
	FTimerHandle AlertMarkFaceTimer;
	FTimerHandle ComboFollowTimer;
	FTimerHandle PatrolCheckTimer;

	int32 CurrentPerceptionState = 0;
	bool bAlertPresenting = false;
	float NextPatrolWanderTime = 0.f;
	float LastGuardEndTime = -1000.f;

	// 连招期间朝玩家慢慢挪
	void UpdateComboFollow();
	// 警戒表现开关：进入警戒播循环动作 + 显示感叹号，离开时全部收掉
	void UpdateAlertPresentation(bool bOn);
	// 播一次警戒动作，并挂上"播完再来一次"的计时器
	void RestartAlertAnim();
	// 让感叹号始终正面朝向摄像机
	void FaceAlertMarkToCamera();
	// 脱战且无人下发移动时，自己挑一个可达点走过去
	void UpdatePatrolWander();

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
	// 上一段用的下标。顺序播放时靠它推下一段（-1 表示这一轮还没开始，自增后正好是 0）
	int32 LastComboIndex = -1;
	FTimerHandle ComboTimer;
	// 整段连招（含最后一段）是否还没播完，BTTask 靠它判断攻击结束没有
	bool bComboRunning = false;

	void PlayNextComboSegment();
	void FinishCombo();

	virtual void HandleDeath() override;
};
