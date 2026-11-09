// Fill out your copyright notice in the Description page of Project Settings.

#include "Characters/AARPGEnemyCharacter.h"
#include "AIController.h"
#include "BrainComponent.h"                              // UBrainComponent::PauseLogic / ResumeLogic，举盾期间停思考
#include "GameFramework/RootMotionSource.h"              // FRootMotionSource_MoveToForce，后撤用它而不是 LaunchCharacter
#include "GameFramework/CharacterMovementComponent.h"
#include "Controller/AARPGEnemyController.h"
#include "AttributeSet/UARPGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/UARPGGameplayAbility_LightAttack.h"   // IsPlayerAttacking 用它做类判据
#include "TimerManager.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "World/UARPGCombatStateSubsystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraComponentPoolMethodEnum.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/TextRenderComponent.h"              // 头顶感叹号，不依赖 WBP 也能显示
#include "Components/CapsuleComponent.h"                 // GetScaledCapsuleHalfHeight，感叹号定位要用完整定义
#include "Components/SkeletalMeshComponent.h"            // SetCanEverAffectNavigation，禁止角色挖空脚下 NavMesh
#include "NavigationSystem.h"                             // GetRandomReachablePointInRadius，脱战游荡挑点
#include "NavigationData.h"                               // ANavigationData::GetBounds，诊断 NavMesh 铺到哪了
#include "Navigation/PathFollowingComponent.h"            // EPathFollowingStatus，判断"现在有没有人在给它下发移动"
#include "Camera/PlayerCameraManager.h"                   // 感叹号朝向摄像机

AAARPGEnemyCharacter::AAARPGEnemyCharacter()
{
	AIControllerClass = AAARPGEnemyController::StaticClass();// 指定这个角色由哪个 AI 控制器接管
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;// 摆进关卡或运行时生成时，自动被 AI 接管

	// 一直面朝玩家：朝向交给 Controller 的 Focus 去转，不再跟着移动方向转。
	// 这两行必须和 BTService 里的 SetFocus 配套 —— 只设 Focus 不关 bOrientRotationToMovement，
	// 角色一走起来还是会扭头朝前，看着像根本没锁目标。
	bUseControllerRotationYaw = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;

	// 别让 Boss 自己参与 NavMesh 生成。
	// Dynamic 模式下导航网格每帧重算，会把"影响导航"的碰撞体当成障碍剔除，
	// 而角色的 Capsule 默认就是这种（CanEverAffectNavigation = true）。
	// 结果：石巨人往哪一站，脚下就被挖出多大一个洞，路径查询的起点投影必然失败——
	// 日志里的"脚下没有 NavMesh"和"寻路结果=0"就是这么来的，跟体积覆盖没关系。
	// 角色之间要不要互相避让是 RVO / DetourCrowd 的事，跟导航几何生成是两回事
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	GetMesh()->SetCanEverAffectNavigation(false);

	// 初始按游荡速度走。Character 默认 600 对石巨人来说是在冲刺，
	// 一进关卡就看着像要扑过来；等感知服务跑起来后会按距离档位重新刷。
	GetCharacterMovement()->MaxWalkSpeed = PatrolSpeed;

	// 盾牌在左手，石巨人的骨架上就是 weapon_lSocket
	GuardAttachSocket = TEXT("weapon_lSocket");

	// ABP_StoneGolem 里那个 Slot 节点就叫 DefaultSlot，格挡和警戒动作都往这里播
	AlertSlotName = TEXT("DefaultSlot");

	// 头顶感叹号。用 TextRender 而不是 WidgetComponent：不用建 WBP、不吃 UMG 的开销，
	// 而且它是真正的 3D 文字，远近大小会跟着透视变，看着像场景里的东西而不是 UI 贴片。
	// 位置要等 BeginPlay 才能定（胶囊半高和 AlertMarkHeight 都可能被蓝图改）
	AlertMarkComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("AlertMarkComponent"));
	AlertMarkComponent->SetupAttachment(GetCapsuleComponent());
	AlertMarkComponent->SetText(FText::FromString(TEXT("!")));
	AlertMarkComponent->SetTextRenderColor(FColor(255, 205, 40));
	AlertMarkComponent->SetHorizontalAlignment(EHTA_Center);
	AlertMarkComponent->SetVerticalAlignment(EVRTA_TextCenter);
	AlertMarkComponent->SetWorldSize(AlertMarkSize);
	AlertMarkComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AlertMarkComponent->SetGenerateOverlapEvents(false);
	AlertMarkComponent->SetCastShadow(false);
	AlertMarkComponent->SetVisibility(false);

	HealthBarComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarComponent"));
	HealthBarComponent->SetupAttachment(GetRootComponent());
	HealthBarComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarComponent->SetDrawAtDesiredSize(false);
	HealthBarComponent->SetDrawSize(FVector2D(200.f, 20.f));
	// 注意：Z 不在这里设。蓝图里改的 HealthBarHeight 要等构造完成后才生效，
	// 在构造函数里读会永远拿到 C++ 默认值 120，Boss 配 300 也不管用。
	// 真正的定位放到 BeginPlay。

	// 格挡的默认表现：工程里现成的护盾特效和两个音效，省得每个怪都要手动配一遍。
	// 蓝图里填了就以蓝图为准（这里的赋值发生在 CDO 构造阶段，蓝图值会覆盖它）。
	// 原来这里默认塞了 NS_Shield_1，但那是一圈魔法阵，配石巨人手里的实体盾很违和，
	// 所以默认留空。想要就自己填，或者干脆用 GuardMontage 播个举盾动作。

	// 格挡的默认音效：起手一声、挡住那一下一声
	static ConstructorHelpers::FObjectFinder<USoundBase> GuardStartAsset(
		TEXT("/Game/Assets/Sounds/Weapons/Block/MagicShieldActivate.MagicShieldActivate"));
	if (GuardStartAsset.Succeeded())
	{
		GuardStartSound = GuardStartAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> GuardImpactAsset(
		TEXT("/Game/Assets/Sounds/Weapons/Block/MagicShieldImpact.MagicShieldImpact"));
	if (GuardImpactAsset.Succeeded())
	{
		GuardImpactSound = GuardImpactAsset.Object;
	}

	// 被盾挡下时炸的火花。没有它的话"减伤"只有数字变化，画面上什么都看不见
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> GuardImpactFxAsset(
		TEXT("/Game/Assets/Niagara/Impact/NS_BlockImpact.NS_BlockImpact"));
	if (GuardImpactFxAsset.Succeeded())
	{
		GuardImpactEffect = GuardImpactFxAsset.Object;
	}
}

void AAARPGEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();//super=父类，先跑同名函数

	if (AbilitySystemComponent && AttackAbilityClass)
	{
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AttackAbilityClass, 1, INDEX_NONE, this));
	}

	if (AbilitySystemComponent && InitStatsEffect)
	{
		AbilitySystemComponent->ApplyGameplayEffectToSelf(
			InitStatsEffect.GetDefaultObject(), 1.f,
			AbilitySystemComponent->MakeEffectContext()
		);
	}

	// 顺序要点：必须先应用初始属性的 GE，血量有值了再刷新，否则刷出来是 0
	if (HealthBarComponent)
	{
		HealthBarComponent->SetRelativeLocation(FVector(0.f, 0.f, HealthBarHeight));// 此时蓝图值才生效
		RefreshHealthBar();
		HealthBarComponent->SetVisibility(false);// 满血不显示，挨打才显示

		// Boss 走屏幕顶部那条，头顶这条直接停用，免得两个血条同时出现
		if (bUseScreenBossBar)
		{
			HealthBarComponent->SetHiddenInGame(true);
		}
	}

	// 屏幕 Boss 血条：等初始属性 GE 应用完再建，不然建出来是 0 血
	if (bUseScreenBossBar)
	{
		CreateBossBar();
	}

	// 感叹号站在胶囊顶上。胶囊半高要到 BeginPlay 才是最终值（蓝图能改），
	// 再往上抬 AlertMarkHeight，字才会浮在头顶而不是糊在脸上
	if (AlertMarkComponent)
	{
		const float HalfHeight = GetCapsuleComponent()
			? GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 90.f;
		AlertMarkComponent->SetRelativeLocation(FVector(0.f, 0.f, HalfHeight + AlertMarkHeight));
		AlertMarkComponent->SetWorldSize(AlertMarkSize);
	}

	// 这套配置在 PIE 里最容易配错（序列忘了填、Slot 名写错），先把实际值打出来
	UE_LOG(LogTemp, Log, TEXT("[Enemy] %s 格挡开关=%s 格挡序列=%s | 警戒序列=%s 警戒蒙太奇=%s"),
		*GetName(),
		bCanGuard ? TEXT("开") : TEXT("关"),
		GuardSequence ? *GuardSequence->GetName() : TEXT("空"),
		AlertSequence ? *AlertSequence->GetName() : TEXT("空"),
		AlertMontage ? *AlertMontage->GetName() : TEXT("空"));

	// 警戒检测：0.4 秒看一眼玩家距离，只为了"第一次看见玩家时播一次动画"，
	// 用 Timer 而不是 Tick —— 这个类平时一帧都不用跑。
	// 配了 AlertSequence 就用下面新的循环警戒，两套不能同时开：
	// 一个播一次、一个循环播，叠在一起会互相顶掉
	if (AlertMontage && !AlertSequence && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			AlertCheckTimer, this, &AAARPGEnemyCharacter::CheckAlert, 0.4f, true);
	}

	// 脱战游荡：0.5 秒看一次"该不该挑下一个点"。
	// 只在没人给它下发移动的时候补位，所以不会和行为树的 MoveTo 抢目标
	if (bUseCodePatrol && GetWorld())
	{
		NextPatrolWanderTime = GetWorld()->GetTimeSeconds()
			+ FMath::FRandRange(PatrolIdleMin, PatrolIdleMax);
		GetWorld()->GetTimerManager().SetTimer(
			PatrolCheckTimer, this, &AAARPGEnemyCharacter::UpdatePatrolWander, 0.5f, true);

		// 这行是排查"为什么不游荡"的入口：先确认计时器到底起没起、判定距离填的是多少
		UE_LOG(LogTemp, Log,
			TEXT("[Wander] %s 游荡启动：玩家超过 %.0fcm 才走，半径 %.0fcm，间隔 %.1f~%.1fs"),
			*GetName(), PatrolTriggerDistance, PatrolWanderRadius, PatrolIdleMin, PatrolIdleMax);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Wander] %s bUseCodePatrol 是关的，代码游荡不会启动"), *GetName());
	}

	// 盾牌格挡：同样用循环 Timer 轮询，不占用 Tick。
	// 钩子放在一个独立 timer 里，跟警戒检测互不影响——警戒只关心"第一次看见玩家"
	if (bCanGuard && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			GuardCheckTimer, this, &AAARPGEnemyCharacter::CheckGuard, GuardCheckInterval, true);
	}
}

void AAARPGEnemyCharacter::CheckAlert()
{
	if (bIsDead || bAlertPlayed || !AlertMontage)
	{
		return;
	}

	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		return;
	}

	if (FVector::DistSquared(Player->GetActorLocation(), GetActorLocation())
		<= FMath::Square(AlertNoticeDistance))
	{
		bAlertPlayed = true;                 // 只播一次：之后就算跑远再回来也不重播
		PlayAnimMontage(AlertMontage);

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(AlertCheckTimer);
		}
	}
}

void AAARPGEnemyCharacter::PlayHitReact()
{
	if (bIsDead || !HitReactMontage)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 挨连击时不要每一下都把动画重头播（会一直卡在起手），隔 0.35 秒才允许重播
	const float Now = World->GetTimeSeconds();
	if (Now - LastHitReactTime < HitReactCooldown)
	{
		return;
	}
	LastHitReactTime = Now;

	PlayAnimMontage(HitReactMontage);
}

void AAARPGEnemyCharacter::RefreshHealthBar()
{
	if (!AbilitySystemComponent || !HealthBarComponent)
	{
		return;
	}

	UUARPGHUDWidget* Bar = Cast<UUARPGHUDWidget>(HealthBarComponent->GetWidget());
	if (!Bar)
	{
		return;
	}

	const float Cur = AbilitySystemComponent->GetNumericAttribute(UUARPGAttributeSet::GetHealthAttribute());
	const float Max = AbilitySystemComponent->GetNumericAttribute(UUARPGAttributeSet::GetMaxHealthAttribute());
	Bar->UpdateHealth(Cur, Max);
}

void AAARPGEnemyCharacter::ShowHealthBarTemporarily()
{
	if (!HealthBarComponent || bUseScreenBossBar)
	{
		return;// Boss 用屏幕顶部那条，不需要头顶这条临时闪烁
	}

	HealthBarComponent->SetVisibility(true);

	if (UWorld* World = GetWorld())
	{
		// 同一个 Handle 再 Set 会自动顶掉上一次，所以连击时天然重新计时，不用手动 Clear
		World->GetTimerManager().SetTimer(
			HealthBarHideTimer, this, &AAARPGEnemyCharacter::HideHealthBar,
			HealthBarHideDelay, false);
	}
}

void AAARPGEnemyCharacter::HideHealthBar()
{
	if (HealthBarComponent)
	{
		HealthBarComponent->SetVisibility(false);
	}
}

void AAARPGEnemyCharacter::ApplyMoveSpeedForState(int32 State)
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	// 0 游荡 / 1 警戒 / 2 追击 / 3 贴身战斗。
	if (bIsDead)
	{
		return;
	}

	// 连招期间不能直接 return：那样保留的是上一档速度（战斗态 180），
	// 而现在连招是"边打边挪"，180 挪起来一眼就能看出在滑步，
	// 所以要单独压到 ComboFollowSpeed 这个更慢的档
	if (bComboRunning)
	{
		MoveComp->MaxWalkSpeed = ComboFollowSpeed;
		return;
	}

	float NewSpeed = PatrolSpeed;
	switch (State)
	{
	case 1:
		NewSpeed = AlertSpeed;
		break;
	case 2:
		NewSpeed = ChaseSpeed;
		break;
	case 3:
		NewSpeed = CombatSpeed;
		break;
	default:
		NewSpeed = PatrolSpeed;
		break;
	}

	if (!FMath::IsNearlyEqual(MoveComp->MaxWalkSpeed, NewSpeed))
	{
		MoveComp->MaxWalkSpeed = NewSpeed;
	}
}

void AAARPGEnemyCharacter::SetPerceptionState(int32 NewState)
{
	if (bIsDead)
	{
		return;
	}

	const int32 PrevState = CurrentPerceptionState;
	CurrentPerceptionState = NewState;

	// 警戒是"站定转身盯着"：刚进警戒的那一下掐掉寻路，
	// 否则行为树还在 MoveTo，转身动作会被一路拖着往前滑。
	// 只在"状态真的变了"时停一次，不是每 0.2 秒都停 ——
	// Service 每帧都会回调进来，要是一直 StopMovement，脱战后我们自己发的游荡路径
	// 会被它当场掐断（状态没刷新到 0 的情况下就表现为"永远不动"）
	if (NewState == 1 && PrevState != 1)
	{
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}
	}

	ApplyMoveSpeedForState(NewState);
	UpdateAlertPresentation(NewState == 1);

	// 刚从警戒掉回游荡：立刻允许挑下一个游荡点，不用干等一轮计时，
	// 不然玩家一跑远，敌人会先站着发呆两三秒才想起该走了
	if (NewState == 0 && PrevState != 0)
	{
		if (UWorld* World = GetWorld())
		{
			NextPatrolWanderTime = World->GetTimeSeconds()
				+ FMath::FRandRange(PatrolIdleMin, PatrolIdleMax);
		}
	}
}

void AAARPGEnemyCharacter::UpdateAlertPresentation(bool bOn)
{
	if (bOn == bAlertPresenting)
	{
		return;   // 每 0.2 秒都会被调一次，状态没变就别重播
	}
	bAlertPresenting = bOn;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (bOn)
	{
		RestartAlertAnim();

		if (AlertMarkComponent && bShowAlertMark)
		{
			AlertMarkComponent->SetVisibility(true);
			FaceAlertMarkToCamera();

			// 0.1 秒对一次朝向。3D 文字不像 UMG 会自动朝相机，
			// 玩家绕到侧面看就只剩一条线了
			World->GetTimerManager().SetTimer(
				AlertMarkFaceTimer, this, &AAARPGEnemyCharacter::FaceAlertMarkToCamera, 0.1f, true);
		}
	}
	else
	{
		World->GetTimerManager().ClearTimer(AlertLoopTimer);
		World->GetTimerManager().ClearTimer(AlertMarkFaceTimer);

		if (AlertPlayingMontage)
		{
			if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
			{
				Anim->Montage_Stop(0.2f, AlertPlayingMontage);
			}
			AlertPlayingMontage = nullptr;
		}

		if (AlertMarkComponent)
		{
			AlertMarkComponent->SetVisibility(false);
		}
	}
}

void AAARPGEnemyCharacter::RestartAlertAnim()
{
	UWorld* World = GetWorld();
	if (!World || !bAlertPresenting)
	{
		return;
	}

	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AlertSequence || !Anim)
	{
		// 没配序列就只留头顶那个感叹号，不报错也不刷日志。
		// 这样"只想要警戒标记、不想要动作"的怪也能直接用
		World->GetTimerManager().ClearTimer(AlertLoopTimer);
		return;
	}

	// Slot 名留空的话动态蒙太奇播不进去，这里兜一个默认值
	const FName Slot = AlertSlotName.IsNone() ? FName(TEXT("DefaultSlot")) : AlertSlotName;

	AlertPlayingMontage = Anim->PlaySlotAnimationAsDynamicMontage(
		AlertSequence, Slot, 0.15f, 0.2f, AlertAnimRate, 1);

	// 播完再来一次，这就是"循环"。
	// 没用 LoopCount = -1，是因为动态蒙太奇这条路上负值的含义没有明确约定；
	// 手动重播行为完全可控，而且离开警戒时能立刻停掉，不会拖着一条长循环
	const float Len = AlertSequence->GetPlayLength() / FMath::Max(0.1f, AlertAnimRate);
	World->GetTimerManager().SetTimer(
		AlertLoopTimer, this, &AAARPGEnemyCharacter::RestartAlertAnim,
		FMath::Max(0.2f, Len), false);
}

void AAARPGEnemyCharacter::FaceAlertMarkToCamera()
{
	if (!AlertMarkComponent || !AlertMarkComponent->IsVisible())
	{
		return;
	}

	FVector CamLoc = FVector::ZeroVector;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->PlayerCameraManager)
		{
			CamLoc = PC->PlayerCameraManager->GetCameraLocation();
		}
	}
	if (CamLoc.IsNearlyZero())
	{
		if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			CamLoc = Player->GetActorLocation();
		}
	}
	if (CamLoc.IsNearlyZero())
	{
		return;
	}

	// 只取朝向相机的旋转，不做 LookAt 的完整解算，
	// 这样镜头俯仰时字也不会跟着翻倒
	const FVector Dir = CamLoc - AlertMarkComponent->GetComponentLocation();
	AlertMarkComponent->SetWorldRotation(Dir.Rotation());
}

void AAARPGEnemyCharacter::UpdateComboFollow()
{
	if (!bComboRunning || bIsDead || bGuarding)
	{
		return;
	}

	AAIController* AI = Cast<AAIController>(GetController());
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!AI || !Player)
	{
		return;
	}

	const float Dist = FVector::Dist(Player->GetActorLocation(), GetActorLocation());

	// 两种情况都停：贴太近（再挤会顶穿玩家，判定位置反而偏掉）
	// 和跑太远（超出 ComboFollowMaxRange 就该交给行为树去追，不是一边挥拳一边长跑）
	if (Dist <= ComboFollowMinRange || Dist > ComboFollowMaxRange)
	{
		AI->StopMovement();
		return;
	}

	// 只在没有正在执行的路径时才下发。每 0.2 秒重建一次路径很浪费，
	// 而且会不停重置"已经走多远"的判断，表现出来是走走停停
	if (AI->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		AI->MoveToActor(Player, ComboFollowMinRange * 0.9f);
	}
}

FVector AAARPGEnemyCharacter::GetNavAgentLocation() const
{
	// 基类（APawn）的实现是 GetActorLocation() - BaseEyeHeight，
	// 而 BaseEyeHeight 在 ACharacter 里固定 64，跟模型大小毫无关系。
	// 石巨人胶囊半高是几百厘米，算出来的导航位置离地两三百，
	// 超出导航系统默认的垂直投影容差（250），起点投影失败 → 整条路径查询作废，
	// MoveToLocation 一律返回 Failed（日志里就是"寻路结果=0"）。
	// 导航代理本来就该站在地上，所以直接给脚底
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	return Capsule
		? GetActorLocation() - FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight())
		: Super::GetNavAgentLocation();
}

void AAARPGEnemyCharacter::UpdatePatrolWander()
{
	// 死了、在连招、在举盾，一律不插手
	if (bIsDead || bComboRunning || bGuarding)
	{
		return;
	}

	AAIController* AI = Cast<AAIController>(GetController());
	UWorld* World = GetWorld();
	if (!AI || !World)
	{
		return;
	}

	// 用"当场量的距离"决定要不要放羊，而不是看 CurrentPerceptionState。
	// 那个状态是行为树上的 Service 每 0.2 秒刷的，Service 没跑到（行为树卡在别的分支、
	// 节点被打断）状态就永远是旧值，游荡这条分支也就永远进不去 ——
	// 这是之前"站到 1000 以外也不走"的第一个坑
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (Player && FVector::DistSquared(Player->GetActorLocation(), GetActorLocation())
		<= FMath::Square(PatrolTriggerDistance))
	{
		return;
	}

	// 已经在按路径走就别抢。这里只挡 Moving：
	// Waiting / Paused 都是"有请求但没在动"（上一次寻路失败、或者被挡住了），
	// 堵在这两个状态上不放行的话敌人会永远站着 —— 之前写的是 != Idle，正好踩这个坑
	if (AI->GetMoveStatus() == EPathFollowingStatus::Moving)
	{
		return;
	}

	if (World->GetTimeSeconds() < NextPatrolWanderTime)
	{
		return;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		if (bDebugWander)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Wander] %s 拿不到 NavigationSystem：查 Build.cs 的 NavigationSystem 模块和关卡里的 NavMeshBoundsVolume"),
				*GetName());
		}
		return;
	}

	// 导航查询的起点统一用"脚底"（GetNavAgentLocation 已经改成脚底）。
	// 拿 Actor 原点（胶囊正中）去查，石巨人离地两三百厘米，
	// 超出导航系统默认的垂直投影容差，可达性测试和路径查询都会判失败
	const FVector FeetLocation = GetNavAgentLocation();

	// 先确认脚下真站在 NavMesh 上。不在的话挑什么点都是白挑，
	// 而且要和下面"挑不到点"区分开，否则永远看不出是哪一头的毛病
	FNavLocation FootNavPoint;
	if (!NavSys->ProjectPointToNavigation(FeetLocation, FootNavPoint))
	{
		if (bDebugWander)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Wander] %s 脚下没有 NavMesh（脚底=%s），说明 NavMeshBoundsVolume 没盖到它站的位置"),
				*GetName(), *FeetLocation.ToCompactString());
		}
		NextPatrolWanderTime = World->GetTimeSeconds() + 1.f;
		return;
	}

	// 优先挑"随机可达点"，而不是"随机方向 + 投影"：后者很容易挑到墙里或地图外，敌人会一直撞墙
	FNavLocation WanderPoint;
	bool bFound = NavSys->GetRandomReachablePointInRadius(FeetLocation, PatrolWanderRadius, WanderPoint);

	// 退一步：可达点找不到（这块地被围住、寻路数据没生成），至少挑一个"能站人"的点。
	// 能走起来总比站着不动强，剩下的交给寻路去绕
	if (!bFound)
	{
		bFound = NavSys->GetRandomPointInNavigableRadius(FeetLocation, PatrolWanderRadius, WanderPoint);
	}

	bool bMoveRequested = false;
	if (bFound)
	{
		const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(WanderPoint.Location, 60.f);
		// 0=Failed / 1=AlreadyAtGoal / 2=RequestSuccessful
		bMoveRequested = (Result != EPathFollowingRequestResult::Failed);

		if (bDebugWander)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Wander] %s 挑点 %s（离 %.0fcm）寻路结果=%d（0=失败 1=已在目标 2=成功）"),
				*GetName(),
				*WanderPoint.Location.ToCompactString(),
				FVector::Dist(WanderPoint.Location, FeetLocation),
				static_cast<int32>(Result));

			// 挑到了点却走不了，说明是路径查询这一层的问题：
			// 把起点、移动状态、胶囊半高一起打出来，一眼能看出是不是个子太高
			if (!bMoveRequested)
			{
				const UCapsuleComponent* CapsuleComp = GetCapsuleComponent();
				UE_LOG(LogTemp, Warning,
					TEXT("[Wander] %s 寻路失败：脚底=%s 移动状态=%d 胶囊半高=%.0f"),
					*GetName(),
					*FeetLocation.ToCompactString(),
					static_cast<int32>(AI->GetMoveStatus()),
					CapsuleComp ? CapsuleComp->GetScaledCapsuleHalfHeight() : -1.f);
			}
		}
	}
	else if (bDebugWander)
	{
		// 挑不到点的时候必须把"差多远"说清楚，否则只看到失败、不知道是体积没盖住还是没生成完。
		// GetBounds() 返回的是"已经生成出来的 tile"的包围盒，和敌人坐标一比就知道缺口在哪
		FString NavCoverage = TEXT("拿不到 NavMesh 数据实例（关卡里可能连 RecastNavMesh 都没生成）");
		if (ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
		{
			const FBox NavBounds = NavData->GetBounds();
			NavCoverage = NavBounds.IsValid
				? FString::Printf(
					TEXT("NavMesh 只覆盖到 X[%.0f, %.0f] Y[%.0f, %.0f] Z[%.0f, %.0f]"),
					NavBounds.Min.X, NavBounds.Max.X,
					NavBounds.Min.Y, NavBounds.Max.Y,
					NavBounds.Min.Z, NavBounds.Max.Z)
				: FString(TEXT("NavMesh 数据在、但一个 tile 都没生成（体积没盖住 / 刚开局还没算完）"));
		}

		const FVector SelfLoc = GetActorLocation();
		UE_LOG(LogTemp, Warning,
			TEXT("[Wander] %s 周围 %.0fcm 挑不到导航点。敌人位置=(%.0f, %.0f, %.0f)，%s"),
			*GetName(), PatrolWanderRadius,
			SelfLoc.X, SelfLoc.Y, SelfLoc.Z,
			*NavCoverage);
	}

	// 成功下发路径才按 2.5~5.5 秒的间隔歇一会儿；
	// 失败就 0.8 秒后马上换一个点重试——不然演示时会眼看着它杵好几秒才动一下
	NextPatrolWanderTime = World->GetTimeSeconds()
		+ (bMoveRequested ? FMath::FRandRange(PatrolIdleMin, PatrolIdleMax) : 0.8f);
}

void AAARPGEnemyCharacter::OnHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	Super::OnHealthChanged(ChangeData);

	if (bIsDead)
	{
		return;// 死了就别再把血条点亮。基类 HandleDeath 的 return 只退出基类，派生还会往下跑
	}

	// 只有真掉血（不是回血/初始化）才播受击动画
	if (ChangeData.NewValue < ChangeData.OldValue)
	{
		// 举着盾的时候不播受击动画：一是没有格挡姿势，二是受击动画会把这 1.5 秒顶掉，
		// 看着像"盾突然没了"。改成放火花 + 金属音，表达"这一下被盾吃了"
		if (bGuarding)
		{
			OnGuardImpact();
		}
		else
		{
			PlayHitReact();
		}
	}

	const float Max = AttributeSet ? AttributeSet->GetMaxHealth() : 0.f;

	// Boss：只刷屏幕顶部那条，头顶 WidgetComponent 在 BeginPlay 就被关掉了
	if (bUseScreenBossBar)
	{
		if (BossBarWidget)
		{
			BossBarWidget->UpdateBossHealth(ChangeData.NewValue, Max);
		}
		return;
	}

	if (!HealthBarComponent)
	{
		return;
	}

	UUARPGHUDWidget* Bar = Cast<UUARPGHUDWidget>(HealthBarComponent->GetWidget());
	if (!Bar)
	{
		return;
	}

	Bar->UpdateHealth(ChangeData.NewValue, Max);

	ShowHealthBarTemporarily();
}

// ==================== 多段攻击 ====================

bool AAARPGEnemyCharacter::StartComboAttack()
{
	if (bIsDead || bGuarding || ComboMontages.Num() == 0)
	{
		return false;
	}

	// 已经在打这一轮了，就别重来。
	// 行为树的分支被打断（距离抖动、黑板值变化）之后会重新进这个节点，
	// 以前这里不判断，每次重进都把 LastComboIndex 清成 -1、从第 1 段重新开始，
	// 表现就是"永远只播第一段"。现在重进只是接着等这一轮打完。
	if (bComboRunning)
	{
		return true;
	}

	ComboSegmentsLeft = FMath::Max(1, ComboMaxSegments);
	// 每次新开一轮都从头打：不清这一行，第二轮会从上一轮停下的地方接着走
	LastComboIndex = -1;
	bComboRunning = true;

	// 连招期间每 0.2 秒朝玩家挪一次。
	// 不这么做的话，BTTask 卡在 InProgress、行为树的 MoveTo 不执行，
	// 玩家往后一退，第 4、5 段就全打在空气里
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ComboFollowTimer, this, &AAARPGEnemyCharacter::UpdateComboFollow,
			ComboFollowInterval, true);
	}

	PlayNextComboSegment();
	return true;
}

void AAARPGEnemyCharacter::FinishCombo()
{
	bComboRunning = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboFollowTimer);
	}
}

void AAARPGEnemyCharacter::StopCombo()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboTimer);
		World->GetTimerManager().ClearTimer(ComboFollowTimer);   // 跟随也一起停，否则收手了还在追
	}
	bComboRunning = false;

	// 只停连招这一路正在播的那段，别把别的动作（受击、死亡）一起打断
	if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		if (ComboMontages.IsValidIndex(LastComboIndex))
		{
			if (UAnimMontage* Playing = ComboMontages[LastComboIndex])
			{
				if (Anim->Montage_IsPlaying(Playing))
				{
					Anim->Montage_Stop(0.15f, Playing);
				}
			}
		}
	}
}

void AAARPGEnemyCharacter::PlayNextComboSegment()
{
	if (bIsDead || ComboSegmentsLeft <= 0)
	{
		// 死了或者已经没得打了，也得把 bComboRunning 收掉，
		// 否则 BTTask 会一直等一个永远不来的"打完了"信号
		FinishCombo();
		return;
	}

	// 打到一半玩家跑了就收手。这里判断而不是靠行为树打断，
	// 是因为打断的时机不可控：行为树一分之支就在几十毫秒内，会把连招反复掐断，
	// 看起来又变成"只会播第一段"。按距离判断，含义明确也不会抖
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (FVector::DistSquared(Player->GetActorLocation(), GetActorLocation())
			> FMath::Square(ComboBreakDistance))
		{
			StopCombo();
			return;
		}
	}

	// 按顺序往下打：1→2→3→4→5，打完一轮回到第一段。
	// 以前这里是 RandRange 随机的（只避开和上一段重复），问题是你没法说"第 4 段"
	// 指的是哪一招，调动作、讲连招节奏都对不上号。改成顺序之后段号才是稳定的。
	const int32 Index = (LastComboIndex + 1) % ComboMontages.Num();
	LastComboIndex = Index;
	--ComboSegmentsLeft;

	UAnimMontage* Montage = ComboMontages[Index];
	if (!Montage)
	{
		FinishCombo();
		return;
	}

	// PlayAnimMontage 返回的是"这段实际要播多久"（已经把播放速率算进去了）
	const float Length = PlayAnimMontage(Montage);
	if (Length <= 0.f)
	{
		FinishCombo();
		return;
	}

	// 还有下一段：等这段播完，再空出 ComboSegmentGap 秒接上；
	// 最后一段则只等它播完就收尾，BTTask 靠这个收尾信号判断"攻击结束了，可以走位了"
	if (UWorld* World = GetWorld())
	{
		if (ComboSegmentsLeft > 0)
		{
			// 空档封顶：ComboSegmentGap 在蓝图里被调到 1 秒以上时，
			// 五段就散成五次单击了，这里保证它们还连在一起
			const float Gap = FMath::Min(ComboSegmentGap, MaxComboSegmentGap);
			World->GetTimerManager().SetTimer(
				ComboTimer, this, &AAARPGEnemyCharacter::PlayNextComboSegment,
				Length + Gap, false);
		}
		else
		{
			World->GetTimerManager().SetTimer(
				ComboTimer, this, &AAARPGEnemyCharacter::FinishCombo, Length, false);
		}
	}
	else
	{
		FinishCombo();
	}
}

// ==================== 盾牌格挡 ====================

bool AAARPGEnemyCharacter::IsPlayerAttacking(APawn* Player) const
{
	if (!Player)
	{
		return false;
	}

	// 不 Cast 成 AAARPGHeroCharacter，直接问接口：以后换成别的玩家类、或者测试用的假人，
	// 只要它实现了 IAbilitySystemInterface，这段代码都不用改
	const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Player);
	UAbilitySystemComponent* PlayerASC = ASI ? ASI->GetAbilitySystemComponent() : nullptr;
	if (!PlayerASC)
	{
		return false;
	}

	// 两条判据，命中任一条就算"在出招"：
	// 1) 技能类本身是 LightAttack —— 用 IsA 判，蓝图子类 GA_LightAttack 也认；
	// 2) 技能资产上挂了 Ability.Attack.Light 标签 —— 以后你把攻击换成自己的技能类，
	//    只要标签还在，这里照样能识别
	// 第二条的 Tag 用 ErrorIfNotFound=false 拿：万一标签没配置，这里返回空 Tag，
	// 只走第一条，不会在日志里刷 ensure
	static const FGameplayTag AttackTag =
		FGameplayTag::RequestGameplayTag(FName(TEXT("Ability.Attack.Light")), false);

	for (const FGameplayAbilitySpec& Spec : PlayerASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.IsActive())
		{
			continue;
		}

		if (Spec.Ability->IsA(UUARPGGameplayAbility_LightAttack::StaticClass()))
		{
			return true;
		}

		if (AttackTag.IsValid() && Spec.Ability->GetAssetTags().HasTag(AttackTag))
		{
			return true;
		}
	}

	return false;
}

void AAARPGEnemyCharacter::CheckGuard()
{
	if (bIsDead || bGuarding || !bCanGuard)
	{
		return;
	}

	// 冷却 = "每 10 秒最多挡一次"。从"上一次放盾"的时刻算，不从开始举盾算，
	// 否则被连着打的时候冷却会被举盾时长吃掉
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (World->GetTimeSeconds() - LastGuardEndTime < GuardCooldown)
	{
		return;
	}

	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		return;
	}

	if (FVector::DistSquared(Player->GetActorLocation(), GetActorLocation())
		> FMath::Square(GuardNoticeDistance))
	{
		return;
	}

	// 核心判据：玩家有没有真的在出招。
	// 出招就必挡（不掷骰）—— 这才是"看到你抬手就举盾"的反应；
	// 没出招时按 bGuardOnRandom 决定要不要随机举，默认是关的，
	// 免得出现"站着发呆突然举一下盾"这种看不懂的行为
	const bool bPlayerAttacking = IsPlayerAttacking(Player);
	const bool bReacting = bGuardOnPlayerAttack && bPlayerAttacking;

	if (!bReacting)
	{
		if (!bGuardOnRandom)
		{
			return;
		}
		if (FMath::FRand() > GuardTriggerChance)
		{
			return;
		}
	}

	// 正在出招也允许挡：下面 StopCombo() 会把这一轮连招收掉，盾举起来。
	// 但受击、警戒转身这类蒙太奇还占着 Slot 时不抢 —— 那多半是硬直或过渡，
	// 抢过来会看到"半截动作硬接到举盾"
	if (!bComboRunning && GetMesh() && GetMesh()->GetAnimInstance()
		&& GetMesh()->GetAnimInstance()->IsAnyMontagePlaying())
	{
		return;
	}

	bGuarding = true;

	// 举盾要接管 DefaultSlot，先把连招收掉。
	// 少了这一句，段间的 ComboTimer 会在 0.25 秒后把下一段捅出来，把格挡动作顶掉，
	// 表现就是"盾举了一半又变回挥拳"
	StopCombo();

	// 只挂 Tag，不减伤：减免写在 UARPGAttributeSet::PostGameplayEffectExecute 里，
	// 那里顺手还能判断"是不是从正面来的"
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Guard")));
	}

	// 举盾动作：优先用序列（走 ABP 的 DefaultSlot），其次才是自己做的蒙太奇。
	// 速率对齐 GuardDuration，让动作铺满整个格挡窗口：动作比窗口短就放慢，长了就加快。
	// 夹在 0.5~1.5 —— 再慢像卡顿，再快看不清在举盾
	const float GuardRate = (GuardDuration > 0.05f)
		? FMath::Clamp(
			(GuardSequence ? GuardSequence->GetPlayLength()
				: (GuardMontage ? GuardMontage->GetPlayLength() : 0.f)) / GuardDuration,
			0.5f, 1.5f)
		: 1.f;

	if (GuardSequence && GetMesh() && GetMesh()->GetAnimInstance())
	{
		GetMesh()->GetAnimInstance()->PlaySlotAnimationAsDynamicMontage(
			GuardSequence, TEXT("DefaultSlot"), 0.1f, 0.2f, GuardRate, 1);
	}
	else if (GuardMontage)
	{
		PlayAnimMontage(GuardMontage, GuardRate);
	}

	// 举盾同时后撤。这里两件事必须一起做，少一件就会变成"比划一下又回原位"：
	// 1) 位移走 root motion source，不用 LaunchCharacter。冲量给的是速度，
	//    下一帧就被 CalcVelocity 按 MaxWalkSpeed（战斗态 180）截断，实际只挪几十厘米。
	//    root motion source 直接按目标点插值，不经过速度上限。
	// 2) 行为树要暂停。StopMovement 只掐掉当前这条路径，MoveTo 任务还在跑，
	//    很快就会重新发起寻路，把角色推回玩家面前。
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
		if (UBrainComponent* Brain = AI->GetBrainComponent())
		{
			Brain->PauseLogic(TEXT("Guard"));   // 举盾这段时间不思考，放盾时在 EndGuard 恢复
			PausedBrain = Brain;
		}
	}

	if (GuardBackstepDistance > 0.f && GuardBackstepSpeed > 0.f)
	{
		FVector BackDir = GetActorLocation() - Player->GetActorLocation();
		BackDir.Z = 0.f;
		if (!BackDir.IsNearlyZero())
		{
			BackDir.Normalize();

			if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
			{
				// 时长 = 距离 / 速度，并夹在 [0.1, GuardDuration] 里：
				// 太长会在放盾之后还在退，太短会变成瞬移
				const float MoveDuration = FMath::Clamp(
					GuardBackstepDistance / GuardBackstepSpeed, 0.1f, GuardDuration);

				const FVector StartLoc = GetActorLocation();

				TSharedPtr<FRootMotionSource_MoveToForce> Backstep = MakeShared<FRootMotionSource_MoveToForce>();
				Backstep->InstanceName = TEXT("GuardBackstep");
				Backstep->AccumulateMode = ERootMotionAccumulateMode::Override;   // 这段时间的位移只认它
				Backstep->Priority = 10;
				Backstep->Duration = MoveDuration;
				Backstep->StartLocation = StartLoc;
				Backstep->TargetLocation = StartLoc + BackDir * GuardBackstepDistance;
				Backstep->bRestrictSpeedToExpected = true;                        // 撞墙时不会硬挤过去
				Backstep->SetTime(0.f);
				MoveComp->ApplyRootMotionSource(Backstep);
			}
		}
	}

	if (GuardStartSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, GuardStartSound, GetActorLocation());
	}

	// 护盾特效挂在 Mesh 上，人走到哪跟到哪；bAutoDestroy 传 false —— 一直挂到放盾
	if (GuardEffect && GetMesh())
	{
		GuardEffectComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			GuardEffect, GetMesh(), GuardAttachSocket,
			FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f),
			EAttachLocation::SnapToTarget, false, ENCPoolMethod::None);
	}

	// World 在本函数开头就取过了，这里直接用，别再声明一次同名变量把它遮住
	World->GetTimerManager().SetTimer(
		GuardEndTimer, this, &AAARPGEnemyCharacter::EndGuard, GuardDuration, false);

	UE_LOG(LogTemp, Warning, TEXT("[Guard] %s %s，举盾 %.2f 秒"),
		*GetName(),
		bReacting ? TEXT("看到玩家出招，格挡") : TEXT("随机举盾"),
		GuardDuration);
}

void AAARPGEnemyCharacter::EndGuard()
{
	if (!bGuarding)
	{
		return;
	}
	bGuarding = false;

	// 记下放盾时刻，CheckGuard 拿它算冷却。
	// 存在这里而不是在 BeginGuard 里算，是因为举盾可能被死亡提前打断，
	// 从"真正结束"的时刻起算才不会连着举
	if (UWorld* World = GetWorld())
	{
		LastGuardEndTime = World->GetTimeSeconds();
	}

	// Tag 撤掉以后伤害恢复正常。所有退出路径（时间到 / 死亡）都走这里，不会漏
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Guard")));
	}

	if (GuardEffectComp)
	{
		GuardEffectComp->Deactivate();       // 先停发射再销毁，否则可能残留一帧粒子
		GuardEffectComp->DestroyComponent();
		GuardEffectComp = nullptr;
	}

	// 举盾时暂停了行为树，这里必须恢复，否则敌人会一直站在原地不动。
	// 死亡路径也会走到这儿（HandleDeath 里调了 EndGuard），但死人不用恢复思考 ——
	// 基类 HandleDeath 已经把 bIsDead 置上了，这里拿它当开关
	if (UBrainComponent* Brain = PausedBrain.Get())
	{
		if (!bIsDead)
		{
			Brain->ResumeLogic(TEXT("Guard"));
		}
		PausedBrain = nullptr;
	}
}

void AAARPGEnemyCharacter::OnGuardImpact()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 被连击时别每一下都炸一遍，0.15 秒内只放一次
	const float Now = World->GetTimeSeconds();
	if (Now - LastGuardImpactTime < 0.15f)
	{
		return;
	}
	LastGuardImpactTime = Now;

	// 盾牌在左手，稍微往前上方偏一点，火花看起来才是"打在盾上"
	const FVector HitLoc = GetActorLocation() + GetActorForwardVector() * 70.f + FVector(0.f, 0.f, 90.f);

	if (GuardImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, GuardImpactSound, HitLoc);
	}

	if (GuardImpactEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, GuardImpactEffect, HitLoc, FRotator::ZeroRotator, FVector(1.f), true, true);
	}
}

void AAARPGEnemyCharacter::HandleDeath()
{
	Super::HandleDeath();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HealthBarHideTimer);
		World->GetTimerManager().ClearTimer(AlertCheckTimer);   // 死了就别再检测警戒距离
		World->GetTimerManager().ClearTimer(ComboTimer);        // 死了也别再接下一段连招
		bComboRunning = false;                                   // 死了就别再让 BTTask 等连招收尾
		World->GetTimerManager().ClearTimer(GuardCheckTimer);   // 别再想着举盾
		World->GetTimerManager().ClearTimer(GuardEndTimer);     // 举盾计时直接作废
		World->GetTimerManager().ClearTimer(ComboFollowTimer);  // 连招跟随别再推着尸体走
		World->GetTimerManager().ClearTimer(AlertLoopTimer);    // 尸体不用再循环转身
		World->GetTimerManager().ClearTimer(AlertMarkFaceTimer);
		World->GetTimerManager().ClearTimer(PatrolCheckTimer);  // 死了更不该去游荡

		// 尸体头上还挂着感叹号很怪，顺手收掉
		bAlertPresenting = false;
		if (AlertMarkComponent)
		{
			AlertMarkComponent->SetVisibility(false);
		}

		// 尸体不该还带着 State.Guard：万一死在举盾那 1.5 秒里，Tag 和特效都要收掉
		EndGuard();

		// 是 Boss 就把"打过 Boss"这件事记到关卡进度里。
		// 注意必须在这里记：基类 HandleDeath 末尾 SetLifeSpan(3.2f) 会把 Actor 销毁，
		// 之后谁也找不到这只 Boss，只能用这个标记判断。
		if (bIsBoss)
		{
			if (UARPGCombatStateSubsystem* State = World->GetSubsystem<UARPGCombatStateSubsystem>())
			{
				State->NotifyBossDefeated();
			}
		}
	}

	if (HealthBarComponent)
	{
		HealthBarComponent->SetVisibility(false);
	}

	// 屏幕血条先留着，等尸体消失（EndPlay）再一起撤掉，看着更像"打完这一场"
}

void AAARPGEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Actor 被销毁时会走到这里（SetLifeSpan 到点、切关卡都会），
	// 手动把自己加的 UMG 摘掉，否则血条会一直挂在屏幕上
	DestroyBossBar();

	Super::EndPlay(EndPlayReason);
}

void AAARPGEnemyCharacter::SetBossDisplayName(const FText& InName)
{
	BossDisplayName = InName;

	if (BossBarWidget)
	{
		BossBarWidget->SetBossName(BossDisplayName);
	}
}

void AAARPGEnemyCharacter::CreateBossBar()
{
	if (!bUseScreenBossBar)
	{
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

	// 没配 WBP 就用 C++ 那个自带布局的类，开箱即用
	TSubclassOf<UUARPGBossHealthBarWidget> WidgetClass = BossBarWidgetClass;
	if (!WidgetClass)
	{
		WidgetClass = UUARPGBossHealthBarWidget::StaticClass();
	}

	BossBarWidget = CreateWidget<UUARPGBossHealthBarWidget>(PC, WidgetClass);
	if (!BossBarWidget)
	{
		return;
	}

	BossBarWidget->SetBossName(BossDisplayName);
	// ZOrder 10：压在玩家 HUD（默认 0）上面
	BossBarWidget->AddToViewport(10);

	// 建出来先按当前血量刷一次，满血进场就是 100%
	const float Cur = AttributeSet ? AttributeSet->GetHealth() : 0.f;
	const float Max = AttributeSet ? AttributeSet->GetMaxHealth() : 0.f;
	BossBarWidget->UpdateBossHealth(Cur, Max);
}

void AAARPGEnemyCharacter::DestroyBossBar()
{
	if (BossBarWidget)
	{
		BossBarWidget->RemoveFromParent();
		BossBarWidget = nullptr;
	}
}
