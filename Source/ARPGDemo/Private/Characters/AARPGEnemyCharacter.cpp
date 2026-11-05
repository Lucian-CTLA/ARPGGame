// Fill out your copyright notice in the Description page of Project Settings.

#include "Characters/AARPGEnemyCharacter.h"
#include "Controller/AARPGEnemyController.h"
#include "AttributeSet/UARPGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "TimerManager.h"

AAARPGEnemyCharacter::AAARPGEnemyCharacter()
{
	AIControllerClass = AAARPGEnemyController::StaticClass();// 指定这个角色由哪个 AI 控制器接管
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;// 摆进关卡或运行时生成时，自动被 AI 接管

	HealthBarComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarComponent"));
	HealthBarComponent->SetupAttachment(GetRootComponent());
	HealthBarComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarComponent->SetDrawAtDesiredSize(false);
	HealthBarComponent->SetDrawSize(FVector2D(200.f, 20.f));
	// 注意：Z 不在这里设。蓝图里改的 HealthBarHeight 要等构造完成后才生效，
	// 在构造函数里读会永远拿到 C++ 默认值 120，Boss 配 300 也不管用。
	// 真正的定位放到 BeginPlay。
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
	}
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
	if (!HealthBarComponent)
	{
		return;
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

	if (!HealthBarComponent)
	{
		return;
	}

	UUARPGHUDWidget* Bar = Cast<UUARPGHUDWidget>(HealthBarComponent->GetWidget());
	if (!Bar)
	{
		return;
	}

	const float Max = AttributeSet ? AttributeSet->GetMaxHealth() : 0.f;
	Bar->UpdateHealth(ChangeData.NewValue, Max);

	ShowHealthBarTemporarily();
}

void AAARPGEnemyCharacter::HandleDeath()
{
	Super::HandleDeath();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HealthBarHideTimer);
	}

	if (HealthBarComponent)
	{
		HealthBarComponent->SetVisibility(false);
	}
}
