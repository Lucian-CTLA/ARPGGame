// Copyright Lucian. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Slate 调试面板（零资产依赖，不引 UMG）。
 *
 * 用控制台命令 ARPGAIAgent.Debug 开关，挂在 GameViewport 上，每帧刷新显示
 * 子系统统计（本地决策数 / 远程数 / 接受 / 拒绝 / 超时 / 并发 slot / 熔断状态）。
 * 好处：不依赖任何蓝图 Widget，纯 C++ 就能看 AI 在干什么，且不影响打包体积。
 */
class SARPGAIAgentDebugPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SARPGAIAgentDebugPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget 每帧回调：刷新文本（只有挂在视口里且可见的 widget 才会被 tick）
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedPtr<STextBlock> TextBlock;
};
