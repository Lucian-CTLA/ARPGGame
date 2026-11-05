// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ProgressBar.h"  
#include "Components/TextBlock.h"
#include "UARPGHUDWidget.generated.h"

/**
 * 
 */
UCLASS()
class ARPGDEMO_API UUARPGHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void UpdateHealth(float Current, float Max);

	// 和 UpdateHealth 一模一样的套路，只是写到 StaminaBar 上；
	// 精力条之前一直满格，就是因为这个函数不存在，没人给它 SetPercent
	UFUNCTION(BlueprintCallable)
	void UpdateStamina(float Current, float Max);

	// 0 = 不显示任何状态 / 1 = 格挡中 / 2 = 破防 / 3 = 体力不足
	// 必须是 public：AARPGHeroCharacter 要从外面调它，放 protected 会报 C2248
	UFUNCTION(BlueprintCallable)
	void SetStatus(int32 StatusIndex);



protected:
	// BindWidget = 强制要求蓝图里必须有一个叫 HealthBar 的 ProgressBar，
	// 没有的话编译蓝图时会报错（这是防"忘了拖控件"的安全网）
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> StaminaBar;
	
	// 三条状态文字各一个 TextBlock，叠放在同一位置，C++ 只切显隐、不碰文本。
	// 文本留在 UMG 资产里，Gather 才能从资产管线收集到（源码侧的 NSLOCTEXT 收集在本机实测不可用）
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusTextBlocking;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusTextBroken;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusTextStamina;

	// 开局把三条状态都收起来，省得 UMG 里忘了设 Collapsed 导致三条叠着一起显示
	virtual void NativeConstruct() override;
};
