// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"          // FTimerHandle
#include "Components/Image.h"      // UImage（黑幕）
#include "Components/TextBlock.h"  // UTextBlock（"你死了"）
#include "Components/Button.h"     // UButton（重生按钮）
#include "UARPGDeathScreenWidget.generated.h"

/**
 * 死亡界面：黑幕淡入 -> 文字浮现 -> 重生按钮出现。
 *
 * 为什么不用 UMG 的动画系统：动画的关键帧存在 Widget 蓝图内部，
 * 只能在编辑器时间轴上手点，版本管理里也看不出改了什么。
 * 这里改用定时器每 0.03 秒插值一次，效果一样，但全部是代码、可 review。
 */
UCLASS()
class ARPGDEMO_API UUARPGDeathScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 不声明构造函数：UUserWidget 的构造函数要吃 FObjectInitializer，
	// 子类声明无参构造会报 C2512（没有合适的默认构造函数可用）
protected:
	// 一显示出来就开始演（AddToViewport 之后引擎会调这里）
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ---- 可调参数（蓝图 Class Defaults 里也能改）----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death Screen")
	float FadeDuration = 1.2f;    // 黑幕从全透明到 TargetOpacity 用多久

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death Screen")
	float TargetOpacity = 0.88f;  // 不全黑，留一点能看见场景

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death Screen")
	float TextDelay = 0.5f;       // 文字比黑幕晚多久开始出现

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death Screen")
	float ButtonDelay = 1.5f;     // 按钮多久后冒出来（防止手快误点）

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Death Screen")
	float TickInterval = 0.03f;   // 约 33 帧/秒，肉眼已经是平滑渐变

	// ---- 蓝图里的控件，按名字绑定 ----

	// BindWidget：蓝图里必须有一个叫 FadeImage 的 Image，否则 WBP 编译报错
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> FadeImage;

	// 文字是可选的：没有就只变黑不显示字
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DeadText;

	// BindWidget：逼着蓝图里必须放一个叫 RebirthButton 的 Button
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RebirthButton;

private:
	float Elapsed = 0.f;
	FTimerHandle FadeTimerHandle;

	void OnFadeTick();

	// AddDynamic 绑定的回调必须是 UFUNCTION
	UFUNCTION()
	void OnRebirthClicked();
};
