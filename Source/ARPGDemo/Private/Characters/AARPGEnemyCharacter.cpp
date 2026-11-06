// Fill out your copyright notice in the Description page of Project Settings.

#include "Characters/AARPGEnemyCharacter.h"
#include "AIController.h"
#include "Controller/AARPGEnemyController.h"
#include "AttributeSet/UARPGAttributeSet.h"
#include "AbilitySystemComponent.h"
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

AAARPGEnemyCharacter::AAARPGEnemyCharacter()
{
	AIControllerClass = AAARPGEnemyController::StaticClass();// 指定这个角色由哪个 AI 控制器接管
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;// 摆进关卡或运行时生成时，自动被 AI 接管

	// 盾牌在左手，石巨人的骨架上就是 weapon_lSocket
	GuardAttachSocket = TEXT("weapon_lSocket");

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

	// 警戒检测：0.4 秒看一眼玩家距离，只为了"第一次看见玩家时播一次动画"，
	// 用 Timer 而不是 Tick —— 这个类平时一帧都不用跑
	if (AlertMontage && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			AlertCheckTimer, this, &AAARPGEnemyCharacter::CheckAlert, 0.4f, true);
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

	ComboSegmentsLeft = FMath::Max(1, ComboMaxSegments);
	bComboRunning = true;
	PlayNextComboSegment();
	return true;
}

void AAARPGEnemyCharacter::FinishCombo()
{
	bComboRunning = false;
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

	// 随机挑一招，尽量不跟上一段重复 —— 两连砸同一个动作像卡帧，不像连招
	int32 Index = FMath::RandRange(0, ComboMontages.Num() - 1);
	if (ComboMontages.Num() > 1 && Index == LastComboIndex)
	{
		Index = (Index + 1) % ComboMontages.Num();
	}
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
			World->GetTimerManager().SetTimer(
				ComboTimer, this, &AAARPGEnemyCharacter::PlayNextComboSegment,
				Length + ComboSegmentGap, false);
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

void AAARPGEnemyCharacter::CheckGuard()
{
	if (bIsDead || bGuarding || !bCanGuard)
	{
		return;
	}

	// 正在出招就不举盾：两边抢同一个 DefaultSlot，动画会互相顶掉
	if (GetMesh() && GetMesh()->GetAnimInstance() && GetMesh()->GetAnimInstance()->IsAnyMontagePlaying())
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

	if (FMath::FRand() > GuardTriggerChance)
	{
		return;
	}

	bGuarding = true;

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

	// 举盾同时后撤：朝"背离玩家"的方向给一个瞬时冲量，靠地面摩擦自然减速。
	// bXYOverride=true 才能盖掉 AI 当下可能正在执行的移动输入，bZOverride=false 保住重力
	if (GuardBackstepSpeed > 0.f)
	{
		// 先掐掉寻路：否则冲量下一帧就被 MoveTo 的路径跟随盖掉，等于没退
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}

		FVector BackDir = GetActorLocation() - Player->GetActorLocation();
		BackDir.Z = 0.f;
		if (!BackDir.IsNearlyZero())
		{
			BackDir.Normalize();
			LaunchCharacter(BackDir * GuardBackstepSpeed, true, false);
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

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			GuardEndTimer, this, &AAARPGEnemyCharacter::EndGuard, GuardDuration, false);
	}

	UE_LOG(LogTemp, Warning, TEXT("[Guard] %s 举盾 %.2f 秒"), *GetName(), GuardDuration);
}

void AAARPGEnemyCharacter::EndGuard()
{
	if (!bGuarding)
	{
		return;
	}
	bGuarding = false;

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
