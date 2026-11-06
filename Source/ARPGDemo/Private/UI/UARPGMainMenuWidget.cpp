#include "UI/UARPGMainMenuWidget.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

void UUARPGMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 资产按路径加载，WBP 的 Class Defaults 里不用再拖一次
	MasterSoundClass = LoadObject<USoundClass>(nullptr, TEXT("/Game/Sound/SC_Master.SC_Master"));
	MasterSoundMix   = LoadObject<USoundMix>(nullptr, TEXT("/Game/Sound/SM_Master.SM_Master"));

	// ---- 三个回调，全部在 C++ 绑死 ----
	if (StartButton)
	{
		StartButton->OnClicked.AddDynamic(this, &UUARPGMainMenuWidget::OnStartButtonClicked);
	}
	if (VolumeSlider)
	{
		VolumeSlider->SetValue(1.f);   // 默认满音量（Min/Max 在 Designer 里设 0 / 1）
		VolumeSlider->OnValueChanged.AddDynamic(this, &UUARPGMainMenuWidget::OnVolumeChanged);
	}
	if (BrightnessSlider)
	{
		BrightnessSlider->SetValue(1.f);  // 默认最亮
		BrightnessSlider->OnValueChanged.AddDynamic(this, &UUARPGMainMenuWidget::OnBrightnessChanged);
	}
	if (DarknessOverlay)
	{
		// 必须让黑幕不吃鼠标点击，否则 StartButton / 两个 Slider 永远点不动
		DarknessOverlay->SetVisibility(ESlateVisibility::HitTestInvisible);
		DarknessOverlay->SetRenderOpacity(0.f);
	}

	// 先推一次混音，否则第一次拖滑块之前音量不受控
	if (MasterSoundMix && MasterSoundClass)
	{
		UGameplayStatics::SetSoundMixClassOverride(this, MasterSoundMix, MasterSoundClass,
			1.f, 1.f, 0.f, true);
		UGameplayStatics::PushSoundMixModifier(this, MasterSoundMix);
	}
}

void UUARPGMainMenuWidget::OnStartButtonClicked()
{
	// 先把输入还给游戏，否则进关卡后鼠标还锁在 UI 上
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}

	// OpenLevel 的 bAbsolute 默认 true，LevelName 要写完整包路径
	// （GameplayStatics.h:341）
	// 正式流程进主线关卡 Lvl_ThirdPerson（themap 只是复制出来录视频用的）
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/Lvl_ThirdPerson")));
}

void UUARPGMainMenuWidget::OnVolumeChanged(float Value)
{
	if (!MasterSoundMix || !MasterSoundClass) return;

	// UE 5.7 签名（GameplayStatics.h:969）：
	// (WorldContext, SoundMix, SoundClass, Volume, Pitch, FadeInTime, bApplyToChildren)
	// 5.7 已经没有 FadeOutTime 参数，蓝图里也找不到这个引脚。
	UGameplayStatics::SetSoundMixClassOverride(this, MasterSoundMix, MasterSoundClass,
		Value, 1.f, 0.f, true);
	UGameplayStatics::PushSoundMixModifier(this, MasterSoundMix);
}

void UUARPGMainMenuWidget::OnBrightnessChanged(float Value)
{
	if (DarknessOverlay)
	{
		// Value = 1（最亮）-> 黑幕透明；Value = 0（最暗）-> 黑幕 0.6
		DarknessOverlay->SetRenderOpacity(FMath::Lerp(MaxDarkness, 0.f, Value));
	}
}
