// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/UARPGHUDWidget.h"
#include "Internationalization/Text.h"

void UUARPGHUDWidget::UpdateHealth(float Current, float Max)
{
	if (!HealthBar) { return; }
	const float Percent = (Max > 0.f) ? (Current / Max) : 0.f;
	HealthBar->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));
}

void UUARPGHUDWidget::UpdateStamina(float Current, float Max)
{
	if (!StaminaBar) { return; }   // BindWidgetOptional：蓝图里没拖 StaminaBar 就跳过，不会崩
	const float Percent = (Max > 0.f) ? (Current / Max) : 0.f;
	StaminaBar->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));
}

void UUARPGHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 绑定（BindWidget）在这一步才完成，所以最早只能在这里初始化显隐
	SetStatus(0);
}

void UUARPGHUDWidget::SetStatus(int32 StatusIndex)
{
	auto Show = [](UTextBlock* TextBlock, bool bVisible)
	{
		// 用 Collapsed 不用 Hidden：Hidden 还占布局空间，三个叠放会错位
		if (TextBlock)
		{
			TextBlock->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	};

	Show(StatusTextBlocking, StatusIndex == 1);
	Show(StatusTextBroken,   StatusIndex == 2);
	Show(StatusTextStamina,  StatusIndex == 3);
}
