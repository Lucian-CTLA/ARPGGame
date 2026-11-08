# ARPG Demo

基于 Unreal Engine 5.7 的单机动作 RPG 项目，核心战斗逻辑使用 C++ 实现，蓝图主要负责蒙太奇、GameplayEffect 数值、UI 布局和资产配置。

项目重点解决了三个问题：

- 如何用 GAS 统一管理技能、属性、伤害和状态；
- 如何在攻击动画持续期间进行可靠的连续命中检测；
- 如何通过状态回滞和模块化 AI 逻辑，降低行为抖动和系统耦合。

- 引擎：Unreal Engine 5.7
- 语言：C++20
- 架构：GAS、Enhanced Input、Behavior Tree、Niagara、World Subsystem
- 仓库：https://github.com/Lucian-CTLA/ARPGGame

## 技术亮点

### 1. 集中式 GAS 伤害结算

角色基类 `AAARPGCharacterBase` 持有 `UAbilitySystemComponent` 和 `UUARPGAttributeSet`。

属性集定义了：

- `Health` / `MaxHealth`
- `Stamina` / `MaxStamina`
- `AttackPower`
- `DefensePower`
- `Damage`

其中 `Damage` 是 Meta Attribute，用作一次性伤害缓存。

近战攻击和火球都会创建 GameplayEffect，通过 `SetByCaller.Damage` 写入伤害值，最终统一进入：

```text
Source/ARPGDemo/Private/AttributeSet/UARPGAttributeSet.cpp
```

`UUARPGAttributeSet::PostGameplayEffectExecute()` 负责：

1. 读取并立即清空 `Damage`，避免伤害残留到下一次攻击；
2. 检查 `State.Rolling`，处理翻滚无敌帧；
3. 检查 `State.Blocking`，处理玩家格挡、体力消耗和破防；
4. 检查 `State.Guard`，处理敌人正面格挡；
5. 统一计算最终伤害并写回 `Health`。

当前项目没有额外的 `GameplayEffectExecutionCalculation`。在当前伤害公式较简单的情况下，使用 AttributeSet 回调可以让不同攻击入口共享同一套最终结算逻辑。

`DefensePower` 已作为属性预留，后续可以扩展护甲减伤、穿透和等级成长等公式。

### 2. 持续近战命中窗口

主角连招蒙太奇使用：

```text
Source/ARPGDemo/Public/AnimInstance/ARPGAnimNSMeleeHitWindow.h
```

`UARPGAnimNSMeleeHitWindow` 继承自 `UAnimNotifyState`，只在动画中的有效命中区间执行检测。

每帧检测流程如下：

1. 根据武器 Socket 或角色前方偏移计算当前判定球中心；
2. 保存上一帧判定球中心；
3. 从上一帧位置向当前帧位置执行球体扫掠；
4. 收集命中目标；
5. 过滤自身、尸体和非敌对目标；
6. 通过 GAS 应用伤害；
7. 触发 Niagara、音效、震屏和 Hit Stop。

移动时使用：

```cpp
UKismetSystemLibrary::SphereTraceMultiForObjects(...)
```

当上一帧和当前帧的球心几乎没有移动时，退化为：

```cpp
UKismetSystemLibrary::SphereOverlapActors(...)
```

这样既能覆盖静止目标，又能避免武器高速移动时只检测单点造成的帧间漏判。

检测只存在于 NotifyState 窗口期间，不需要给角色增加常驻 Tick 查询。

### 3. 基于 TSet 的攻击窗口去重

每个攻击窗口都有独立的运行时状态：

```cpp
TSet<TWeakObjectPtr<AActor>> HitTargets;
```

状态由 `TMap` 按 `USkeletalMeshComponent` 分组：

```cpp
TMap<
    TWeakObjectPtr<USkeletalMeshComponent>,
    FARPGMeleeHitWindowState> WindowStates;
```

这样做有两个目的：

- 同一次攻击中，同一个目标只结算一次；
- 同一个 NotifyState 资产被 Hero、Boss 和其他敌人共享时，不会互相污染命中记录。

`NotifyBegin()` 会重置当前角色的窗口状态，清空 `HitTargets`；`NotifyEnd()` 会删除当前 Mesh 对应的状态。目标和 Mesh 都使用弱引用，避免角色销毁后留下悬空指针。

扫掠结果本身使用 `TArray<AActor*>`，并通过 `AddUnique()` 清理同一帧的重复结果；跨帧的攻击去重由 `TSet` 完成。

旧的 `UUARPGAnimNotify_MeleeHit` 仍保留在源码中，但它是早期的单帧球形重叠实现。当前主角连招使用的是持续窗口 `UARPGAnimNSMeleeHitWindow`。

### 4. 集中的命中反馈

单次命中由 `UARPGAnimNSMeleeHitWindow::ApplyHit()` 统一处理：

- 创建并应用伤害 GameplayEffect；
- 在目标位置生成 Niagara 命中特效；
- 播放命中音效；
- 触发玩家 Camera Shake；
- 对攻击者和受击者施加局部 Hit Stop。

Hit Stop 使用 Actor 级别的 `CustomTimeDilation`：

```cpp
Target->CustomTimeDilation = HitStopDilation;
Attacker->CustomTimeDilation = HitStopDilation;
```

默认参数为：

- 时间倍率：`0.05`
- 持续时间：`0.08s`

恢复通过 World Timer 完成，没有使用 `SetGlobalTimeDilation`，避免全局时间膨胀影响其他角色和恢复计时器。

震屏使用 `UCameraShakeBase` 和自定义的正弦震屏模式：

```text
Source/ARPGDemo/Public/Carema/ARPGHitCameraShake.h
Source/ARPGDemo/Public/Carema/ARPGSineCameraShakePattern.h
```

### 5. GameplayTag 驱动的状态逻辑

项目使用 GameplayTag 表达短期战斗状态：

- `State.Rolling`：翻滚和无敌帧；
- `State.Blocking`：玩家持续格挡；
- `State.BlockBroken`：格挡被打破的一次性标记；
- `State.Stunned`：破防后的短暂硬直；
- `State.Guard`：敌人举盾状态；
- `State.Dead`：角色死亡。

破防流程为：

```text
体力不足
  -> 添加 State.BlockBroken
  -> 取消 Ability.Block
  -> GA_Block::EndAbility()
  -> 添加 State.Stunned
  -> 播放硬直动画
  -> 定时移除 State.Stunned
```

短时状态直接挂在 ASC 上，技能激活阻塞和伤害结算都通过 GameplayTag 查询完成。

### 6. Enhanced Input 与 GAS Ability

玩家输入通过 Enhanced Input 绑定：

- 移动
- 视角
- 轻攻击
- 翻滚
- 格挡
- 火球

翻滚技能：

```text
Source/ARPGDemo/Private/AbilitySystem/UARPGGameplayAbility_Roll.cpp
```

使用 `LaunchCharacter()` 实现一次性位移，而不是 Root Motion 或物理 `AddImpulse`：

```cpp
Avatar->LaunchCharacter(
    Direction * RollSpeed,
    true,
    true);
```

翻滚方向优先取最后一次移动输入，没有输入时使用角色前向量。

轻攻击、格挡、翻滚和火球都通过 ASC 激活。技能的消耗和冷却通过 `CommitAbility()` 接入 GAS，具体 GameplayEffect 参数由技能资产配置。

### 7. 行为树距离分档与 AI 回滞

主项目的 `UBTService_ARPGPreception` 每 `0.2` 秒更新一次目标距离和黑板状态：

- `<= 300cm`：攻击；
- `> 300cm && <= 800cm`：追击；
- `> 800cm && <= 1500cm`：警戒；
- `> 1500cm`：脱战。

黑板只负责保存目标、距离和状态，距离阈值由 Service 的 C++ 默认值提供，并可在行为树节点详情中覆盖。

可复用的攻击距离回滞位于：

```text
Plugins/ARPGAIAgent/Source/ARPGAIAgent/Private/ARPGAIAgentComponent.cpp
```

插件本地决策默认使用：

- 进入攻击距离：`250cm`
- 退出攻击距离：`312.5cm`
- 追击距离：`800cm`

退出阈值通过：

```cpp
AttackRange * AttackExitRatio
```

计算，默认 `AttackExitRatio = 1.25`。

插件使用 `bWasInAttackRange` 保存上一轮状态，在进入和退出之间形成死区，减少角色在临界距离附近反复切换攻击和追击的情况。

插件采用 Local First 设计：

1. 本地规则立即产生决策；
2. 远程 AI 只负责异步修正；
3. 超时、失败或低置信度结果自动丢弃；
4. 游戏继续使用本地决策。

插件通过 GAS 标准接口和反射读取属性，不依赖 `ARPGDemo` 游戏模块，便于跨项目复用。

### 8. Boss 连招与行为树任务同步

Boss 支持两种攻击路线：

- 通过 GameplayAbility 播放传统攻击；
- 通过 `ComboMontages` 和定时器播放多段 C++ 连招。

`UBTTask_ARPGAttack` 在连招模式下不会立即返回 `Succeeded`，而是返回 `InProgress`，并在 `TickTask()` 中等待整段连招完成。

这样可以避免行为树提前执行 `MoveTo`，导致敌人在攻击动画还没结束时一边挥拳一边滑向玩家。

### 9. C++ 与蓝图职责分离

C++ 负责：

- GAS 技能激活和结束；
- 属性修改和伤害结算；
- 近战检测与目标去重；
- AI 状态策略；
- 重生、存档点和关卡状态；
- UI 数据更新；
- 命中特效和时间控制。

蓝图负责：

- GameplayEffect 数值；
- 技能和动画资产；
- 蒙太奇配置；
- Niagara 和音效资产；
- WBP 布局；
- 敌人和 Boss 的具体参数。

这种拆分让运行时逻辑可以顺着 C++ 调用链调试，同时保留蓝图配置资产的效率。

## 游戏流程

从 `Lvl_MainMenu` 开始游戏：

1. 进入战斗关卡；
2. 击败沿途敌人；
3. 触发并记录存档点；
4. 击败第一个 Boss；
5. 触发 StoneGolem；
6. 击败最终 Boss；
7. 显示胜利结算。

死亡后可以选择：

- 重新开始整关；
- 从最近存档点重生。

存档点数据保存在 `UARPGCheckpointSubsystem` 中，不依赖已经销毁的 Pawn。

Boss 和 StoneGolem 的关卡进度保存在 `UARPGCombatStateSubsystem` 中，避免 Boss 销毁后无法判断其历史状态。

## 操作

| 输入 | 行为 |
|---|---|
| `W A S D` | 移动 |
| 鼠标 | 控制视角 |
| 鼠标左键 | 轻攻击和连招 |
| 翻滚键 | 翻滚和无敌帧 |
| 格挡键 | 持续格挡 |
| `P` | 火球 |

实际键位以 `Content/Input` 和 `Content/Input_Action` 中的资产配置为准。

## 目录结构

```text
Source/ARPGDemo/
├── AbilitySystem/   技能、格挡、翻滚、火球与旧版 Notify
├── AI/              行为树 Service 与 Task
├── AnimInstance/    持续近战命中窗口 NotifyState
├── AttributeSet/    属性、伤害桶、格挡和伤害结算
├── Audio/           战斗音乐与距离回滞
├── Carema/          Camera Shake 与正弦震屏模式
├── Characters/      玩家、敌人和角色基类
├── Combat/          火球投射物
├── Controller/      玩家和敌人控制器
├── GameMode/        重生和游戏流程
├── UI/              HUD、血条、死亡和胜利界面
└── World/           存档点、Boss 状态和召唤逻辑

Plugins/ARPGAIAgent/
└── Source/ARPGAIAgent/
    ├── Public/      插件接口、配置和数据结构
    └── Private/     本地决策、异步请求和远程 AI 客户端
```

## 编译

项目使用 Unreal Engine 5.7 源码版和 C++20。

使用 Unreal Engine 生成项目文件后，打开 `ARPGDemo.sln`，选择：

```text
Development Editor
```

修改头文件、`Build.cs` 或新增 UObject 类后，需要关闭编辑器进行完整编译。只有修改 `.cpp` 函数体时，才适合使用 Live Coding。

## 资源边界

仓库为了控制体积，没有提交部分第三方资源：

- `Content/StoneGolem`
- `Content/_rainstoryRPGUI`
- `Content/__ExternalActors__`
- `Content/__ExternalObjects__`
- 部分商城资源和派生缓存

缺少这些内容时，源码、配置和核心逻辑仍然可以阅读和开发，但完整的关卡视觉表现和 Boss 资产需要额外资源。

## 当前边界

- 当前项目是单机逻辑，没有完整的多人网络同步；
- 主角当前使用持续 NotifyState 进行近战判定；
- 旧的单帧 `UUARPGAnimNotify_MeleeHit` 仅作为遗留兼容代码保留；
- `DefensePower` 已定义，但尚未参与最终伤害公式；
- 项目暂时没有完整的自动化战斗测试；
- 远程 AI 为可选能力，未配置 API Key 时自动使用本地决策。

## AI 插件

`Plugins/ARPGAIAgent` 是一个独立的可复用 AI 决策插件。

插件特点：

- 不依赖 `ARPGDemo` 游戏模块；
- 通过 GAS 标准接口和反射读取属性；
- 本地决策优先，远程 AI 异步修正；
- 具备请求并发限制、失败熔断和响应去乱序；
- 使用距离回滞和决策保持时间减少行为抖动；
- API Key 通过环境变量读取，不写入版本库。

详细设计见：

[Plugins/ARPGAIAgent/README.md](Plugins/ARPGAIAgent/README.md)
