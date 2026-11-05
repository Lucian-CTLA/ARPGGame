// Copyright Lucian. All Rights Reserved.

using UnrealBuildTool;

public class ARPGAIAgent : ModuleRules
{
	public ARPGAIAgent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			// 组件要读属性 / 激活技能，走 GAS 标准接口。
			// 注意：这里刻意【不依赖】游戏模块 ARPGDemo —— 插件靠反射读 AttributeSet，
			// 才能做成可独立交付、不产生循环依赖的插件。
			"GameplayAbilities",
			// Settings 头文件在 Public 下，外部可能 include，所以放 Public 侧
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// 黑板 / AIController，用来找目标 Actor
			"AIModule",
			// 异步 HTTP + JSON 解析。
			// 这三个刻意留在 Private：它们被 pimpl 关在插件内部，不会传染给游戏模块。
			"HTTP",
			"Json",
			"JsonUtilities",
			"Slate",
			"SlateCore",
			
		});

		// std::atomic / TUniqueFunction 在 5.7 默认 C++20 下可用，无需额外开关
	}
}
