// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UARPGBossHealthBarWidget.generated.h"

class UProgressBar;
class UTextBlock;

/**
 * 屏幕正上方中间的 Boss 血条（Boss / StoneGolem 共用）。
 *
 * 和其他血条的区别：
 *  - 普通小怪用的是 AARPGEnemyCharacter 里那个 WidgetComponent，血条浮在怪物头顶；
 *  - Boss 这条是"屏幕空间"的，固定贴在屏幕顶部中间，不随镜头转。
 *
 * 零编辑器配置：这个类自己会在 RebuildWidget 里用纯 C++ 把控件搭出来，
 * 所以不建 WBP 也能直接用（CreateWidget 传这个类就行）。
 * 想换外观再建一个 WBP 子类，把控件命名成 BossNameText / HealthBar / HealthText
 * 就会被自动接管（BindWidgetOptional），C++ 造的那套不再生效。
 */
UCLASS()
class ARPGDEMO_API UUARPGBossHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Boss 名字（左下角那行字）
	UFUNCTION(BlueprintCallable, Category = "BossBar")
	void SetBossName(const FText& InName);

	// 血量变化时调用；Max <= 0 时按满血处理，避免除零
	UFUNCTION(BlueprintCallable, Category = "BossBar")
	void UpdateBossHealth(float Current, float Max);

protected:
	// WBP 子类里可选绑定的控件；不建 WBP 时这三个都是空的，走 C++ 自建那条路
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BossNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	// 蓝图没摆控件时，用 WidgetTree 现场搭一套（CanvasPanel → SizeBox → Overlay → 底框 + 条 + 文字）
	void BuildDefaultLayout();

	FText CachedBossName;
	float CachedPercent = 1.f;
	FText CachedHealthText;

	// 血条整体宽度；改这里就能整体缩放
	UPROPERTY(EditDefaultsOnly, Category = "BossBar")
	float BarWidth = 760.f;

	UPROPERTY(EditDefaultsOnly, Category = "BossBar")
	float BarHeight = 52.f;

	// 距离屏幕上边缘的像素
	UPROPERTY(EditDefaultsOnly, Category = "BossBar")
	float TopMargin = 36.f;
};
