#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/Image.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UARPGMainMenuWidget.generated.h"

/**
 * 主菜单逻辑：开始游戏 / 音量 / 亮度。
 *
 * 写法照抄工程里已有的 UUARPGDeathScreenWidget：
 * 回调全部在 C++ 里用 AddDynamic 绑死，蓝图图表不用连一根线。
 *
 * 注意：WBP_MainMenu 的 Parent Class 必须改成 UARPGMainMenuWidget，
 * 且 Hierarchy 里这 4 个控件的名字必须一字不差（meta=(BindWidget)）。
 */
UCLASS()
class ARPGDEMO_API UUARPGMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	// AddToViewport 之后引擎会调这里
	virtual void NativeConstruct() override;

	// ---- WBP_MainMenu 里必须存在、且名字一字不差的 4 个控件 ----
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> StartButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> VolumeSlider;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> BrightnessSlider;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> DarknessOverlay;

	// 最暗时黑幕的不透明度：0.6 表示不全黑，留一点看得见场景
	UPROPERTY(EditDefaultsOnly, Category = "Main Menu")
	float MaxDarkness = 0.6f;

private:
	// UPROPERTY 保活，避免运行时被 GC
	UPROPERTY()
	TObjectPtr<USoundClass> MasterSoundClass;

	UPROPERTY()
	TObjectPtr<USoundMix> MasterSoundMix;

	// AddDynamic 绑定的回调必须是 UFUNCTION
	UFUNCTION()
	void OnStartButtonClicked();

	UFUNCTION()
	void OnVolumeChanged(float Value);

	UFUNCTION()
	void OnBrightnessChanged(float Value);
};
