// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/UARPGDeathScreenWidget.h"
#include "GameMode/AARPGGameMode.h"
#include "World/UARPGCheckpointSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"                     // UWorld::GetSubsystem（模板定义在这里）
#include "GameFramework/PlayerController.h"   // FInputModeGameOnly / FInputModeUIOnly
#include "Components/CanvasPanel.h"           // 根面板，用来在代码里补按钮
#include "Components/CanvasPanelSlot.h"       // UCanvasPanelSlot（定位靠它）
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"

void UUARPGDeathScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 初始状态：黑幕全透明、文字全透明、按钮藏起来
	if (FadeImage)       { FadeImage->SetRenderOpacity(0.f); }
	if (DeadText)        { DeadText->SetRenderOpacity(0.f); }
	if (RebirthButton)   { RebirthButton->SetVisibility(ESlateVisibility::Hidden); }
	if (MainMenuButton)  { MainMenuButton->SetVisibility(ESlateVisibility::Hidden); }

	// 存档点按钮：WBP 里没建就现场补一个，省掉改资产的步骤
	BuildCheckpointButtonIfMissing();
	if (CheckpointButton)
	{
		CheckpointButton->SetVisibility(ESlateVisibility::Hidden);
	}

	// 有没有存档点，决定第二个选项最后出不出来。
	// 没存过档还摆一个灰按钮，玩家点了没反应反而困惑，所以干脆不显示
	if (UWorld* World = GetWorld())
	{
		if (const UARPGCheckpointSubsystem* CheckpointState = World->GetSubsystem<UARPGCheckpointSubsystem>())
		{
			bHasCheckpoint = CheckpointState->HasCheckpoint();
		}
	}

	// 现在菜单里有两个选项了，原来的"重生"改成语义明确的"重新开始"
	SetButtonLabel(RebirthButton, FText::FromString(TEXT("重新开始")));

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
	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.AddDynamic(this, &UUARPGDeathScreenWidget::OnMainMenuClicked);
	}
	if (CheckpointButton)
	{
		CheckpointButton->OnClicked.AddDynamic(this, &UUARPGDeathScreenWidget::OnCheckpointClicked);
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
	if (MainMenuButton && Elapsed >= ButtonDelay)
	{
		MainMenuButton->SetVisibility(ESlateVisibility::Visible);
	}
	// 没有存档点就一直藏着（上面已经把 bHasCheckpoint 定好了）
	if (bHasCheckpoint && CheckpointButton && Elapsed >= ButtonDelay)
	{
		CheckpointButton->SetVisibility(ESlateVisibility::Visible);
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

void UUARPGDeathScreenWidget::OnMainMenuClicked()
{
	// 先把输入还给游戏；进菜单关卡后由 AARPGMainMenuPlayerController
	// 重新设成 UI Only + 显示鼠标，两边不用抢
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}

	// bAbsolute=true，LevelName 要写完整包路径（GameplayStatics.h:341）
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/Lvl_MainMenu")));
}

void UUARPGDeathScreenWidget::BuildCheckpointButtonIfMissing()
{
	if (CheckpointButton)
	{
		return;   // WBP 里已经有同名控件了，直接用，不造第二个
	}

	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget());
	if (!RootCanvas || !WidgetTree)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DeathUI] 根面板不是 CanvasPanel，没法自动补存档点按钮；"
			     "请在 WBP_DeathScreen 里手动建一个名为 CheckpointButton 的按钮"));
		return;
	}

	CheckpointButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("CheckpointButton_Auto"));

	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("CheckpointLabel_Auto"));
	Label->SetText(FText::FromString(TEXT("从存档点复活")));
	Label->SetJustification(ETextJustify::Center);

	// 按钮只能有一个内容控件，文字直接挂上去
	CheckpointButton->AddChild(Label);

	// 样式照抄重生按钮，两个按钮看起来才是一套；连字体和颜色也一起抄
	if (RebirthButton)
	{
		CheckpointButton->SetStyle(RebirthButton->GetStyle());

		if (const UTextBlock* RefLabel = Cast<UTextBlock>(RebirthButton->GetChildAt(0)))
		{
			Label->SetFont(RefLabel->GetFont());
			Label->SetColorAndOpacity(RefLabel->GetColorAndOpacity());
		}
	}

	UCanvasPanelSlot* NewSlot = RootCanvas->AddChildToCanvas(CheckpointButton);
	if (!NewSlot)
	{
		return;
	}

	// 定位：复制重生按钮的锚点/对齐/尺寸，垂直往下让出一条按钮高度，
	// 这样不管 WBP 里原来摆在哪个位置，新按钮都跟在它下面
	if (const UCanvasPanelSlot* RefSlot = RebirthButton ? Cast<UCanvasPanelSlot>(RebirthButton->Slot) : nullptr)
	{
		NewSlot->SetAnchors(RefSlot->GetAnchors());
		NewSlot->SetAlignment(RefSlot->GetAlignment());
		NewSlot->SetAutoSize(RefSlot->GetAutoSize());
		NewSlot->SetSize(RefSlot->GetSize());
		NewSlot->SetZOrder(RefSlot->GetZOrder());

		// 量不出高度（AutoSize）时就按 80 像素的常规按钮高估计
		const float Step = (RefSlot->GetSize().Y > 1.f) ? RefSlot->GetSize().Y + 16.f : 80.f;
		NewSlot->SetPosition(RefSlot->GetPosition() + FVector2D(0.f, Step));
	}
	else
	{
		// 兜底：屏幕正中偏下一点
		NewSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		NewSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		NewSlot->SetAutoSize(true);
		NewSlot->SetPosition(FVector2D(0.f, 120.f));
	}
}

void UUARPGDeathScreenWidget::SetButtonLabel(UButton* Target, const FText& Label)
{
	if (!Target)
	{
		return;
	}

	// 按钮的内容控件是第一个子控件；不是 TextBlock（比如换成图片按钮）就跳过
	if (UTextBlock* Text = Cast<UTextBlock>(Target->GetChildAt(0)))
	{
		Text->SetText(Label);
	}
}

void UUARPGDeathScreenWidget::OnCheckpointClicked()
{
	// 先把 PlayerController 和 GameMode 抓在手上：下面 RemoveFromParent 之后，
	// 这个 Widget 已经不在视图里了，再回头去问 OwningPlayer 容易拿到空
	APlayerController* PC = GetOwningPlayer();
	AAARPGGameMode* GM = Cast<AAARPGGameMode>(UGameplayStatics::GetGameMode(this));

	if (PC)
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}

	// 黑幕必须自己撤掉：复活只是换了 Pawn，这个 Widget 挂在 PlayerController 名下，
	// 不主动移除的话复活后玩家看到的还是一片黑
	RemoveFromParent();

	if (GM)
	{
		GM->RespawnAtCheckpoint();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[DeathUI] 没找到 AAARPGGameMode，存档点复活没执行"));
	}
}
