// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UARPGBossHealthBarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SlateWrapperTypes.h"

void UUARPGBossHealthBarWidget::SetBossName(const FText& InName)
{
	// 先缓存：CreateWidget 到 AddToViewport 之间控件可能还没建出来，
	// 缓存后在 BuildDefaultLayout / NativeConstruct 里再落到控件上
	CachedBossName = InName;

	if (BossNameText)
	{
		BossNameText->SetText(CachedBossName);
	}
}

void UUARPGBossHealthBarWidget::UpdateBossHealth(float Current, float Max)
{
	CachedPercent = (Max > 0.f) ? FMath::Clamp(Current / Max, 0.f, 1.f) : 0.f;

	// 数字用取整，血条用精确小数（不然 1 点血也会显示成 0 / 100 但条还在）
	CachedHealthText = FText::FromString(
		FString::Printf(TEXT("%d / %d"), FMath::Max(0, FMath::RoundToInt(Current)), FMath::RoundToInt(Max)));

	if (HealthBar)
	{
		HealthBar->SetPercent(CachedPercent);
	}
	if (HealthText)
	{
		HealthText->SetText(CachedHealthText);
	}
}

TSharedRef<SWidget> UUARPGBossHealthBarWidget::RebuildWidget()
{
	// 只有"没建 WBP、WidgetTree 里也是空的"才自己搭一套。
	// 如果以后做了 WBP 子类，那里已经有 RootWidget，这里就什么都不做，直接用 WBP 的布局。
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}

	return Super::RebuildWidget();
}

void UUARPGBossHealthBarWidget::BuildDefaultLayout()
{
	// 布局原则：所有尺寸都按像素写死，位置用 Canvas 绝对坐标，
	// 完全不依赖 Fill / 自动尺寸。之前那版把"文字行"和"血条"叠在同一个 Overlay 里，
	// 一旦父级给到的宽度不理想，名字和数字就会挤到一块儿、条也缩成一小截。
	// 现在改成两行：第一行文字、第二行血条，各行高度固定，物理上不可能重叠。
	const float W = FMath::Max(BarWidth, 200.f);   // 整条宽度（可配）
	const float H = FMath::Max(BarHeight, 8.f);    // 血条本身的粗细（可配）
	const float TextH = 28.f;                      // 文字行高
	const float Gap = 4.f;                         // 文字行和血条之间的缝

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("BossBarRoot"));
	WidgetTree->RootWidget = Root;

	// 外层盒子：宽 = BarWidth，高 = 文字行 + 缝 + 血条
	USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BossBarSizeBox"));
	SizeBox->SetWidthOverride(W);
	SizeBox->SetHeightOverride(TextH + Gap + H);
	{
		UCanvasPanelSlot* BoxSlot = Root->AddChildToCanvas(SizeBox);
		BoxSlot->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));   // 锚在屏幕上边缘正中
		BoxSlot->SetAlignment(FVector2D(0.5f, 0.f));           // 自己的中心对准锚点，才是真正居中
		BoxSlot->SetPosition(FVector2D(0.f, TopMargin));
		BoxSlot->SetAutoSize(false);
	}

	// 里面再用一层 Canvas：三个元素各自占一块矩形，互不干扰
	UCanvasPanel* Inner = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("BossBarInner"));
	SizeBox->AddChild(Inner);

	// ---- 第一行左：Boss 名字 ----
	UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BossNameText"));
	Name->SetText(CachedBossName);
	Name->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Name->SetJustification(ETextJustify::Left);
	Name->SetShadowOffset(FVector2D(1.f, 1.f));
	Name->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	{
		FSlateFontInfo Font = Name->GetFont();
		Font.Size = 20;
		Name->SetFont(Font);

		UCanvasPanelSlot* NameSlot = Inner->AddChildToCanvas(Name);
		NameSlot->SetAnchors(FAnchors(0.f, 0.f));
		NameSlot->SetPosition(FVector2D(0.f, 0.f));
		NameSlot->SetSize(FVector2D(W * 0.65f, TextH));
		NameSlot->SetAutoSize(false);
	}
	BossNameText = Name;

	// ---- 第一行右：100 / 100 ----
	UTextBlock* Value = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HealthText"));
	Value->SetText(CachedHealthText);
	Value->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.9f, 0.9f, 1.f)));
	Value->SetJustification(ETextJustify::Right);   // 右对齐，靠右边缘
	Value->SetShadowOffset(FVector2D(1.f, 1.f));
	Value->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	{
		FSlateFontInfo Font = Value->GetFont();
		Font.Size = 20;
		Value->SetFont(Font);

		UCanvasPanelSlot* ValueSlot = Inner->AddChildToCanvas(Value);
		ValueSlot->SetAnchors(FAnchors(0.f, 0.f));
		ValueSlot->SetPosition(FVector2D(W * 0.35f, 0.f));       // 和名字那块有 35% 的重叠区，够长也不打架
		ValueSlot->SetSize(FVector2D(W * 0.65f, TextH));
		ValueSlot->SetAutoSize(false);
	}
	HealthText = Value;

	// ---- 第二行：血条本体，横跨整个 BarWidth ----
	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BossBarBg"));
	Bg->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.65f));   // 半透明黑底，兼作 2px 描边
	Bg->SetPadding(FMargin(2.f));
	Bg->SetHorizontalAlignment(HAlign_Fill);
	Bg->SetVerticalAlignment(VAlign_Fill);
	{
		UCanvasPanelSlot* BgSlot = Inner->AddChildToCanvas(Bg);
		BgSlot->SetAnchors(FAnchors(0.f, 0.f));
		BgSlot->SetPosition(FVector2D(0.f, TextH + Gap));
		BgSlot->SetSize(FVector2D(W, H));
		BgSlot->SetAutoSize(false);
	}

	UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthBar"));
	Bar->SetPercent(CachedPercent);
	// 深红：Boss 血条习惯用红，跟玩家自己的绿条区分开
	Bar->SetFillColorAndOpacity(FLinearColor(0.72f, 0.07f, 0.07f, 1.f));
	Bg->SetContent(Bar);
	HealthBar = Bar;   // 直接赋给成员，后面 UpdateBossHealth 就能刷到
}

void UUARPGBossHealthBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 血条纯展示，不要吃鼠标点击（否则会挡住后面的 UI）
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// WBP 子类那条路：控件是 WBP 给的，把缓存值刷一遍
	if (BossNameText)
	{
		BossNameText->SetText(CachedBossName);
	}
	if (HealthBar)
	{
		HealthBar->SetPercent(CachedPercent);
	}
	if (HealthText)
	{
		HealthText->SetText(CachedHealthText);
	}
}
