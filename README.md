# ARPG Demo

基于 **Unreal Engine 5.7 + 纯 C++** 的单机动作角色扮演 Demo。核心战斗、属性、AI 决策全部用 C++ 实现，蓝图只承担资产配置（技能、特效、动画、数值），不含游戏逻辑。

- 引擎：`UE 5.7`（源码版）
- 语言：C++20
- 仓库：https://github.com/Lucian-CTLA/ARPGGame.git

---

## 操作方式

| 输入 | 行为 |
|---|---|
| `W` `A` `S` `D` | 移动 |
| 鼠标移动 | 视角 |
| 鼠标左键 | 攻击（可连击） |
| `空格` | 翻滚（消耗体力，带无敌帧） |

---

## 已实现功能

### 战斗与属性（GAS）
- 基于 `GameplayAbilitySystem` 的属性系统：Health / MaxHealth / Stamina / MaxStamina / AttackPower / Damage
- 伤害走 `SetByCaller` + 瞬时 `GameplayEffect`，伤害数值由技能在运行时注入
- `PreAttributeChange` 做钳制（MaxHealth 必须先于 Health，否则读到的上限是旧值）
- `PostGameplayEffectExecute` 里先把 Damage 清零再处理，避免伤害在 Meta Attribute 里累积
- 属性变化广播委托驱动 UI，UI 不轮询

### 技能
- **轻攻击**：可连击，命中由 `AnimNotify_MeleeHit` 在动画的确定帧触发
- **翻滚**：位移 + 无敌帧 + 体力消耗；体力不足则无法触发
- **体力回复**：无限时长 GE，周期 0.2 秒回 2 点

### 敌人 AI
- 行为树 + 黑板驱动；`BTService` 做感知，按 **攻击 / 追击 / 警戒** 三档距离切换状态
- 每个敌人持有独立黑板，多敌人开箱即用
- 敌人差异化靠 `GameplayEffect` 配属性（小怪 / Boss 两套），不改代码
- Boss 使用三连击蒙太奇

### 打击感三件套
- **命中反馈**：Niagara 特效 + 音效，在命中帧触发
- **卡肉**：命中瞬间用 Actor 级 `CustomTimeDilation` 做 0.08 秒顿帧
  （不用 `SetGlobalTimeDilation` —— 它会连恢复用的定时器一起拖慢，永远恢复不了）
- **震屏**：`UCameraShakeBase` + 自定义正弦 `CameraShakePattern`
  （5.x 已移除 `ULegacyCameraShake` / `UPerlinCameraShake`，老教程那套在 5.7 不存在）

### UI
- HUD：血条 + 体力条，由属性委托驱动
- 敌人头顶血条：`WidgetComponent`，Screen 空间自动朝向相机
- 死亡界面：C++ 定时器驱动淡入（黑幕 → 文字 → 重生按钮），点击重生调用 `RestartGame`

---

## AI 决策插件（Plugins/ARPGAIAgent）

独立插件，提供组件化的异步 AI 决策层，详细设计见 [`Plugins/ARPGAIAgent/README.md`](Plugins/ARPGAIAgent/README.md)。

四个设计要点：

| 要点 | 做法 |
|---|---|
| 组件化接入 | `UARPGAIAgentComponent` 可挂任意角色；靠 GAS 标准接口 + 反射读属性，**不依赖游戏模块**；pimpl 隔离 HTTP 依赖 |
| 异步决策 | `CompleteOnHttpThread` 把收包与 JSON 解析挪出主线程（UE 默认是回主线程，会让解析占着主线程） |
| 线程安全 | 跨线程只搬 POD；`TWeakObjectPtr` 在主线程构造后再进 lambda；请求 Id 去乱序；锁保护共享状态 |
| LocalFirst | 本地规则**立即生效**，远程 LLM 只做升级修正；超时 / 失败 / 返回非法值一律静默降级 |

不开远程也能跑 —— 纯本地规则，完全离线。远程需要的 API Key 走环境变量，不进版本库。

---

## 目录结构

```
Source/ARPGDemo/
├── AbilitySystem/   技能、命中通知（AnimNotify）
├── AI/              行为树 Service / Task
├── AnimInstance/    动画实例
├── AttributeSet/    属性集与钳制逻辑
├── Carema/          震屏（Shake + Pattern）
├── Characters/      角色基类、玩家、敌人
├── Controller/      玩家 / 敌人控制器
├── GameMode/        游戏模式与重生
└── UI/              HUD、敌人血条、死亡界面

Plugins/ARPGAIAgent/
├── Public/          类型、配置、子系统、组件、远程客户端
└── Private/
```

---

## 构建

1. 用 VS 打开 `ARPGDemo.sln`，配置选 `Development Editor`，**Ctrl+Shift+B** 完整编译
2. 或双击 `ARPGDemo.uproject` 打开编辑器

> 改了 `Build.cs`、头文件、新增类，必须**关掉编辑器完整编译**；
> 只改 `.cpp` 函数体可以用 Live Coding（`Ctrl+Alt+F11`）。

---

## 演示

<!-- TODO: 录制 60~90 秒演示并放到 docs/demo.mp4 -->

---

## 已知问题与后续

- 死亡界面重生的输入模式在极端情况下可能残留鼠标锁定，已做 `SetInputMode(GameOnly)` 兜底
- AI 插件尚未接入游戏侧（需在 `Build.cs` 加依赖并在蓝图挂组件）
- **AI 插件尚未完成编译与真实 HTTP 请求联调**：当前为代码实现与设计描述，不代表已通过运行或压力测试；本地决策策略可完全离线运行
- 攻击判定为单次球形重叠查询，未实现连续检测（Sweep）与命中去重，快速移动目标存在漏判可能
- 未做网络同步，当前为纯单机
- 未做单元测试；本地决策策略是纯函数，适合补单测
- 尚未建立可复现的性能基线，README 中不宣称任何优化比例
