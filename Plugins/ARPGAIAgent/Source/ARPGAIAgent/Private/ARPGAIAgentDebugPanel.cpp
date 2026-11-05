// Copyright Lucian. All Rights Reserved.

#include "ARPGAIAgentDebugPanel.h"

#include "ARPGAIAgentSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Widgets/Text/STextBlock.h"

void SARPGAIAgentDebugPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SBorder)
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.6f)))
			.Padding(FMargin(8.f))
			[
				SAssignNew(TextBlock, STextBlock)
				.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 1.f)))
				.Text(FText::FromString(TEXT("[AIAgent] waiting...")))
			]
		];
}

void SARPGAIAgentDebugPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!TextBlock.IsValid())
	{
		return;
	}

	FString Out;
	if (GEngine)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType != EWorldType::Game && Ctx.WorldType != EWorldType::PIE)
			{
				continue;
			}
			UWorld* World = Ctx.World();
			if (!World)
			{
				continue;
			}
			if (UARPGAIAgentSubsystem* Sub = World->GetSubsystem<UARPGAIAgentSubsystem>())
			{
				const FARPGAIAgentStats& S = Sub->GetStats();
				Out += FString::Printf(
					TEXT("%s | slots=%d local=%d remote=%d acc=%d rej=%d to=%d fail=%d avg=%.0fms circuit=%d state=%d\n"),
					*World->GetName(), Sub->GetUsedSlots(), S.LocalDecisions, S.RemoteDecisions,
					S.RemoteAccepted, S.RemoteRejected, S.RemoteTimeouts, S.RemoteFailures,
					S.AverageLatencyMs, S.CircuitOpenCount, static_cast<int32>(Sub->GetRemoteState()));
			}
		}
	}

	if (Out.IsEmpty())
	{
		Out = TEXT("[AIAgent] no active world / subsystem");
	}
	TextBlock->SetText(FText::FromString(Out));
}

// ============================================================================
//  控制台开关：ARPGAIAgent.Debug
// ============================================================================

namespace ARPGAIAgentDebug
{
	static TSharedPtr<SARPGAIAgentDebugPanel> GPanel;
	static bool bShown = false;

	static void Toggle()
	{
		if (!GEngine || !GEngine->GameViewport)
		{
			return;
		}

		if (bShown)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(GPanel.ToSharedRef());
			GPanel.Reset();
			bShown = false;
		}
		else
		{
			GPanel = SNew(SARPGAIAgentDebugPanel);
			GEngine->GameViewport->AddViewportWidgetContent(GPanel.ToSharedRef(), 1000);
			bShown = true;
		}
	}

	static FAutoConsoleCommand GDebugCmd(
		TEXT("ARPGAIAgent.Debug"),
		TEXT("Toggle the ARPG AI Agent Slate debug overlay (live decision stats)."),
		FConsoleCommandDelegate::CreateStatic(&Toggle));
}
