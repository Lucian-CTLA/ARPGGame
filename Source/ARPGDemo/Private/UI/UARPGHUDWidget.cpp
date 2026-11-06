// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/UARPGHUDWidget.h"
#include "Internationalization/Text.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

namespace
{
	// 球体 HUD（_rainstoryRPGUI/UI/V2）：液面靠材质参数 Percentage 控制，
	// 不是 ProgressBar 的 SetPercent，所以这里额外喂一次。
	// 控件名优先找 FillImage，找不到就退回 Image（原始 Orb 资产里液面就叫 Image）。
	// 再找不到就遍历整棵控件树（Orb_02 / Orb_07 内部命名不一样），
	// 对每个 Image 的材质实例都写一次 —— 材质里没这个参数就什么也不发生，无害。
	void SetOrbPercent(UUserWidget* Owner, const TCHAR* OrbName, float Percent)
	{
		if (!Owner) { return; }

		UUserWidget* Orb = Cast<UUserWidget>(Owner->GetWidgetFromName(FName(OrbName)));
		if (!Orb) { return; }

		const float Value = FMath::Clamp(Percent, 0.f, 1.f);

		UImage* Fill = Cast<UImage>(Orb->GetWidgetFromName(TEXT("FillImage")));
		if (!Fill) { Fill = Cast<UImage>(Orb->GetWidgetFromName(TEXT("Image"))); }

		// Image 的 Brush 必须是材质（MI_UI_V3_Orb_xx），是贴图的话这里拿不到 MID，返回 null
		if (Fill)
		{
			if (UMaterialInstanceDynamic* MID = Fill->GetDynamicMaterial())
			{
				MID->SetScalarParameterValue(TEXT("Percentage"), Value);
			}
			return;
		}

		if (!Orb->WidgetTree) { return; }
		Orb->WidgetTree->ForEachWidgetAndDescendants([Value](UWidget* Widget)
		{
			if (UImage* Img = Cast<UImage>(Widget))
			{
				if (UMaterialInstanceDynamic* MID = Img->GetDynamicMaterial())
				{
					MID->SetScalarParameterValue(TEXT("Percentage"), Value);
				}
			}
		});
	}
}

void UUARPGHUDWidget::UpdateHealth(float Current, float Max)
{
	const float Percent = (Max > 0.f) ? (Current / Max) : 0.f;

	if (HealthBar)
	{
		HealthBar->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));
	}

	// WBP_HUD 里嵌了球体（HealthOrb），球在动才看得出血量
	SetOrbPercent(this, TEXT("HealthOrb"), Percent);
}

void UUARPGHUDWidget::UpdateStamina(float Current, float Max)
{
	const float Percent = (Max > 0.f) ? (Current / Max) : 0.f;

	// BindWidgetOptional：蓝图里没拖 StaminaBar 就跳过，不会崩
	if (StaminaBar)
	{
		StaminaBar->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));
	}

	SetOrbPercent(this, TEXT("StaminaOrb"), Percent);
}

void UUARPGHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 绑定（BindWidget）在这一步才完成，所以最早只能在这里初始化显隐
	SetStatus(0);

	// 右下角火球图标：蓝图里没摆就现造一个
	BuildFireIcon();
	UpdateFireCooldown(0.f, 1.f);   // 开局是可用状态：无遮罩、无数字、图标满亮
}

void UUARPGHUDWidget::BuildFireIcon()
{
	if (FireIcon)
	{
		return;   // 蓝图里已经手摆好了（名字叫 FireIcon），直接用，不重复建
	}

	UCanvasPanel* Canvas = Cast<UCanvasPanel>(GetRootWidget());
	if (!Canvas || !WidgetTree)
	{
		return;   // 根不是 CanvasPanel 就放弃（WBP_HUD 的根就是 CanvasPanel）
	}

	UTexture2D* IconTex = FireIconTexture;
	if (!IconTex)
	{
		// T_RageSlash：和 Designer 里拖进来的效果一样，只是这里是运行时加载
		IconTex = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/Assets/Textures/UI/Skills/T_RageSlash.T_RageSlash"));
	}
	if (!IconTex)
	{
		return;
	}

	const FAnchors CornerAnchors(1.f, 1.f);          // 锚在屏幕右下角
	const FVector2D CornerAlign(1.f, 1.f);           // 控件右下角贴锚点
	const FVector2D CornerPos(-FireIconMargin, -FireIconMargin);
	const FVector2D IconSize(FireIconSize, FireIconSize);

	auto PlaceInCorner = [&](UWidget* Widget, int32 ZOrder)
	{
		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget))
		{
			Slot->SetAnchors(CornerAnchors);
			Slot->SetAlignment(CornerAlign);
			Slot->SetPosition(CornerPos);
			Slot->SetSize(IconSize);
			Slot->SetZOrder(ZOrder);
		}
	};

	// 图层 1：图标本体
	FireIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FireIcon"));
	FireIcon->SetBrushFromTexture(IconTex, false);
	PlaceInCorner(FireIcon, 10);

	// 图层 2：冷却遮罩。纯黑半透明，靠 Y 方向缩放表示"还剩多少没转好"，
	// pivot 放顶部（0.5, 0）→ 遮罩是从上往下退的
	FireCooldownMask = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FireCooldownMask"));
	if (UTexture2D* White = LoadObject<UTexture2D>(nullptr,
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")))
	{
		FireCooldownMask->SetBrushFromTexture(White, false);
	}
	FireCooldownMask->SetColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.72f));
	FireCooldownMask->SetRenderTransformPivot(FVector2D(0.5f, 0.f));
	PlaceInCorner(FireCooldownMask, 11);
	FireCooldownMask->SetVisibility(ESlateVisibility::Collapsed);

	// 图层 3：剩余秒数
	FireCooldownText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FireCooldownText"));
	FireCooldownText->SetJustification(ETextJustify::Center);
	FireCooldownText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 34));
	FireCooldownText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	PlaceInCorner(FireCooldownText, 12);
	FireCooldownText->SetVisibility(ESlateVisibility::Collapsed);
}

void UUARPGHUDWidget::UpdateFireCooldown(float Remaining, float Total)
{
	if (!FireIcon)
	{
		return;
	}

	const float Ratio = (Total > 0.f) ? FMath::Clamp(Remaining / Total, 0.f, 1.f) : 0.f;
	const bool bReady = Ratio <= KINDA_SMALL_NUMBER;

	if (FireCooldownMask)
	{
		FireCooldownMask->SetVisibility(bReady ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		if (!bReady)
		{
			FireCooldownMask->SetRenderScale(FVector2D(1.f, Ratio));
		}
	}

	if (FireCooldownText)
	{
		FireCooldownText->SetVisibility(bReady ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		if (!bReady)
		{
			FireCooldownText->SetText(FText::AsNumber(FMath::CeilToInt(Remaining)));
		}
	}

	// 冷却中整体压暗，一眼能看出"这技能现在按不了"
	FireIcon->SetRenderOpacity(bReady ? 1.f : 0.45f);
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
