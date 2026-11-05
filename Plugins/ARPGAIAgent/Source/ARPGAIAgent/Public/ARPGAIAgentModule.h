// Copyright Lucian. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * 插件模块入口。
 * 这里什么都不做，只是给 UBT 一个加载锚点；真正的逻辑在组件 / 子系统里。
 */
class FARPGAIAgentModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
