// Copyright Lucian. All Rights Reserved.

#include "ARPGAIAgentSettings.h"
#include "HAL/PlatformMisc.h"

UARPGAIAgentSettings::UARPGAIAgentSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("ARPG AI Agent");
}

FString UARPGAIAgentSettings::ResolveApiKey() const
{
	// 从环境变量读，避免把密钥写进 ini / git。
	// 没配就返回空串，调用方把它当成「远程不可用」，自动走 LocalFirst 降级。
	FString Key = FPlatformMisc::GetEnvironmentVariable(*ApiKeyEnvVar);
	Key.TrimStartAndEndInline();
	return Key;
}

bool UARPGAIAgentSettings::IsRemoteConfigured() const
{
	if (!bRemoteEnabled)
	{
		return false;
	}
	if (EndpointUrl.IsEmpty() || ModelName.IsEmpty())
	{
		return false;
	}
	return !ResolveApiKey().IsEmpty();
}
