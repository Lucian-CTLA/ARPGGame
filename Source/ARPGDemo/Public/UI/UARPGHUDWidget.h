// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ProgressBar.h"  
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "UARPGHUDWidget.generated.h"

class UCanvasPanel;
class UTexture2D;

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

	// P 键火球图标的冷却：Remaining 还剩几秒，Total 总时长（5 秒）
	// 遮罩从上往下退，数字显示剩余秒，倒计时到 0 时整个图标恢复原样
	UFUNCTION(BlueprintCallable)
	void UpdateFireCooldown(float Remaining, float Total);



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

private:
	// ---- P 键火球图标（右下角）----
	// 蓝图里手摆了就叫 FireIcon / FireCooldownMask / FireCooldownText 自动接管；
	// 没摆也没关系，NativeConstruct 里会用 C++ 直接建出来，不用在 Designer 里拖控件
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> FireIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> FireCooldownMask;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FireCooldownText;

	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	TObjectPtr<UTexture2D> FireIconTexture;

	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	float FireIconSize = 96.f;

	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	float FireIconMargin = 40.f;   // 距屏幕右下角的边距

	void BuildFireIcon();          // 蓝图没摆控件时，用 WidgetTree 现场造一个
};
