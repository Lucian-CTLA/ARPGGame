// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UARPGDeathScreenWidget.h"
#include "GameMode/AARPGGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"   // FInputModeGameOnly / FInputModeUIOnly

void UUARPGDeathScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 初始状态：黑幕全透明、文字全透明、按钮藏起来
	if (FadeImage)     { FadeImage->SetRenderOpacity(0.f); }
	if (DeadText)      { DeadText->SetRenderOpacity(0.f); }
	if (RebirthButton) { RebirthButton->SetVisibility(ESlateVisibility::Hidden); }

	Elapsed = 0.f;

	// 用定时器推进渐变，而不是 Tick：演完就自动停，不空转
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(FadeTimerHandle, this,
			&UUARPGDeathScreenWidget::OnFadeTick, TickInterval, true);
	}

	// 按钮回调在 C++ 里绑死，蓝图里不用连一根线
	if (RebirthButton)
	{
		RebirthButton->OnClicked.AddDynamic(this, &UUARPGDeathScreenWidget::OnRebirthClicked);
	}
}

void UUARPGDeathScreenWidget::NativeDestruct()
{
	// Widget 被移除时把定时器清掉，避免悬空回调
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FadeTimerHandle);
	}
	Super::NativeDestruct();
}

void UUARPGDeathScreenWidget::OnFadeTick()
{
	Elapsed += TickInterval;

	// 黑幕：0 -> TargetOpacity
	if (FadeImage)
	{
		const float Alpha = (FadeDuration > 0.f)
			? FMath::Clamp(Elapsed / FadeDuration, 0.f, 1.f) : 1.f;
		FadeImage->SetRenderOpacity(FMath::Lerp(0.f, TargetOpacity, Alpha));
	}

	// 文字：晚 TextDelay 秒开始，用 1 秒淡进来
	if (DeadText)
	{
		const float Alpha = FMath::Clamp((Elapsed - TextDelay) / 1.f, 0.f, 1.f);
		DeadText->SetRenderOpacity(Alpha);
	}

	// 按钮：到点才显示，防止玩家还没看清就点掉
	if (RebirthButton && Elapsed >= ButtonDelay)
	{
		RebirthButton->SetVisibility(ESlateVisibility::Visible);
	}

	// 全部演完就停掉定时器
	const float TotalTime = FMath::Max(FadeDuration, FMath::Max(ButtonDelay, TextDelay + 1.f));
	if (Elapsed >= TotalTime)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(FadeTimerHandle);
		}
	}
}

void UUARPGDeathScreenWidget::OnRebirthClicked()
{
	// 先把输入模式还给游戏，否则重载关卡后鼠标可能还是锁在 UI 上
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}

	// 引擎的 RestartGame() 没有 UFUNCTION 标记，蓝图调不到，
	// 所以 GameMode 里包了一层 BlueprintCallable 的 RespawnHero()
	if (AAARPGGameMode* GM = Cast<AAARPGGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GM->RespawnHero();
	}
}
