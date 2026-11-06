# ARPG Demo

UE 5.7 的单机动作 RPG 练习项目。

作者：黄晨奚（Lucian）

写它主要是想弄明白三件事：GAS 里伤害到底怎么进到属性上、行为树和动画要怎样才能对得上、以及网上那些教程里的写法在 5.7 上还成不成立。所以战斗逻辑我尽量写在 C++ 里，蓝图只用来配资产（蒙太奇、GE 数值、特效、WBP 布局）。这样出问题能顺着调用栈找，不用在蓝图连线里猜。

- 引擎：UE 5.7（源码版）
- 语言：C++20
- 仓库：https://github.com/Lucian-CTLA/ARPGGame

## 怎么玩

从 `Lvl_MainMenu` 启动，点开始进战斗关卡。

| 输入 | 行为 |
|---|---|
| `W` `A` `S` `D` | 移动 |
| 鼠标 | 视角 |
| 鼠标左键 | 攻击，可连击 |
| `P` | 火球 |

翻滚和格挡也接了 Enhanced Input，键位在 `Content/Input/IMC_Hero` 里。这个映射我改过几版，具体按键以资产为准。

## 关卡流程

一条路走到底：先过几只小怪，中间有个存档点，尽头是 Boss。打赢之后去点路口那盏灯（`BP_Outside_Lamp3` 的位置上放了个看不见的碰撞盒），会召唤 StoneGolem 当第二个 Boss。

死亡界面给两个选择。「重新开始」是整关重载，敌人和 Boss 标记全部复位。「最近存档点」只重造玩家 Pawn，场景原样保留，所以已经打死的小怪不会回来。没存过档时后者会自动退回前者，按钮点了永远有反应。

打完最后的 Boss 弹胜利结算。

## 做完了什么

### 属性与伤害

属性集里六个属性：Health、MaxHealth、Stamina、MaxStamina、AttackPower、Damage。

伤害走 SetByCaller 的瞬时 GE，数值由技能在运行时算好塞进去，不在 GE 里写死死数。原因是不同技能要乘不同倍率，写死了就得给每个技能复制一份 GE。

`PreAttributeChange` 里做钳制，注意 MaxHealth 必须先于 Health 改，否则读到的是旧上限。

`PostGameplayEffectExecute` 里先把 Damage 清零再往下走。不清的话它会留在 Meta Attribute 里一直累加，下一次伤害就会带着上一次的余量。

UI 全部由属性变化委托驱动，没有一处 Tick 轮询。

### 玩家

轻攻击最多四段。命中不靠动画自带的碰撞，而是在蒙太奇的第 N 帧挂一个 `AnimNotify_MeleeHit`，Notify 触发时才做球形重叠。段和段之间用定时器控制时长，没有走 Section 的 Next 链，这样换一套蒙太奇不用重新连 Section。

翻滚带位移、无敌帧和体力消耗。无敌帧是在 `PostGameplayEffectExecute` 里查 Tag 实现的，查到就先 `SetDamage(0)` 再 return。必须清零，直接 return 会把伤害攒在桶里，下次挨打一次性爆出来。

格挡是按住式的，举盾期间挂 `State.Guard`。正面来的伤害乘 0.2，正面的判定用伤害 GE 的 Causer 和自身前向量的点积做，阈值 0.25，大概 ±75 度。体力被打空会破防硬直，挂 `State.Stunned`，硬直期间移动输入直接丢掉。

火球绑 `P`，生成一个 `AARPGFireProjectile` 走直线，冷却用 GE 管，不写计时器。

### 敌人

行为树加一个自定义 Service 做感知，按攻击、追击、警戒三档距离切状态。每只敌人的黑板是独立的，多放几只不会互相干扰。属性差异靠 GE 配，小怪和 Boss 各一套，不碰代码。

Boss 多两层东西。

一是多段连招。`ComboMontages` 是蒙太奇数组，`ComboMaxSegments` 限制最多打几段，`ComboSegmentGap` 是段间隔。推进逻辑在 `StartComboAttack()` 里，用定时器一段一段走。

二是随机格挡。玩家进到 900 距离内之后，每 3 秒掷一次骰子，50% 概率举盾 1.5 秒。举盾时播躲闪动画加一段后撤位移。一开始我给它配的是个防御阵特效，跑起来像原地放法术，完全不像在挡，就换成现在这个。

### 打击感

命中的一瞬间做三件事：喷 Niagara 特效、播音效、把 Actor 级的 `CustomTimeDilation` 压到 0.05 持续 0.08 秒做卡肉。

卡肉这里不能用 `SetGlobalTimeDilation`。它会把恢复用的定时器也一起拖慢，看起来是 0.08 秒，实际按游戏时间算要等好几秒，而且中途再命中一次就可能永远恢复不了。

震屏用 `UCameraShakeBase` 加自己写的正弦 `CameraShakePattern`。5.x 已经没有 `ULegacyCameraShake` 了，教程里那套在 5.7 编不过。

另外玩家在一场战斗里有音乐切换：`UARPGCombatMusicSubsystem` 每 0.25 秒扫一次最近的敌人，800 以内淡入 BGM，1500 以外淡出。两个阈值不一样是为了做回滞，不然站在边界上音乐会一直开关。

### UI 和流程

HUD 血条体力条、敌人头顶血条、Boss 大血条、死亡界面、主菜单、胜利结算，都是 C++ 类加 WBP 子类。

死亡界面的淡入是代码定时器推的，没做动画。存档点那个按钮在 WBP 里没有对应控件，代码里用 `ConstructWidget` 现造一个挂在下面，这样老 WBP 不重编也能用。

敌人头顶血条是 `WidgetComponent`，Screen 空间，自动朝向相机。

### 存档点

`AARPGCheckpoint` 中间一个隐藏的球，玩家 Overlap 就记位置，然后炸一下特效当反馈。复活点比 Actor 本身高 100，因为直接放在地面高度的话，新生成的胶囊体常常卡进地板，一开局就被推出来。

存的位置放在 `UARPGCheckpointSubsystem`（WorldSubsystem）里。数据不能挂在 Pawn 上，Pawn 死了是要被销毁的。

`RespawnAtCheckpoint` 的顺序是先 UnPossess 再 Destroy 旧 Pawn，最后 `RestartPlayerAtTransform`。不能直接调 Restart，它对已经有 Pawn 的情况是原地返回，什么都不做。

### 关卡进度

`UARPGCombatStateSubsystem` 记两个 bool：Boss 死没死、StoneGolem 召没召过。

这么绕是因为 Boss 死亡后 3 秒就被 `SetLifeSpan` 销毁了。之后再去场景里找 `BP_Enemy_Boss`，既找不到，也分不清是「还没打」还是「已经打完了」。让它在死之前先打个标记，召唤点只查标记，跟 Actor 的生灭脱钩。

## 目录

```
Source/ARPGDemo/
├── AbilitySystem/   技能（轻攻击 / 翻滚 / 格挡 / 火球）与命中 Notify
├── AI/              行为树 Service 与 Task
├── AnimInstance/    动画实例、命中窗口 NotifyState
├── AttributeSet/    属性集、钳制、格挡减伤
├── Audio/           战斗音乐状态机
├── Carema/          震屏（Shake + Pattern）
├── Characters/      角色基类、玩家、敌人
├── Combat/          火球投射物
├── Controller/      玩家 / 敌人 / 主菜单控制器
├── GameMode/        游戏模式与两种重生
├── UI/              HUD、敌人血条、Boss 血条、死亡界面、主菜单
└── World/           存档点、召唤点、关卡状态子系统
```

`Carema` 是早期拼错的 Camera，一直没改。改名要动一堆 include，等哪天顺手再说。

## 编译

用 VS 打开 `ARPGDemo.sln`，配置选 `Development Editor`，`Ctrl+Shift+B` 完整编译。或者直接双击 `ARPGDemo.uproject` 让引擎编。

改头文件、`Build.cs`、新增类，必须关编辑器完整编译。只有改 `.cpp` 的函数体才能用 Live Coding（`Ctrl+Alt+F11`）。在 Live Coding 里加 UPROPERTY 会让编辑器直接崩，日志里先有一行 `data type changes may cause packaging to fail`，看到这行就停下来完整编译。

## 踩过的坑

挑几个印象深的记着，省得以后重踩。

- SetByCaller 在 5.7 只能用 Data Tag。选了 Tag 却用 FName 那个版本的话不会报错，伤害恒为 0，查起来非常难受。
- `BlockAbilitiesWithTag` 匹配的是对方技能的 `AbilityTags`，不是 Loose Tag。攻击技能忘了在 Class Defaults 里挂标签，格挡就静默失效，一点提示都没有。
- `BTTask` 在攻击刚开始就返回 `Succeeded` 的话，行为树会立刻接着跑 MoveTo，表现就是第 4、5 段攻击变成边打边滑。要返回 `InProgress`，然后 `TickTask` 里轮询状态。
- `AnimNotify` 对象是资产级共享的，同一帧多个 Actor 命中会共用同一个实例。状态不能放成员变量。
- `CreateWidget` 的 Owner 必须是 PlayerController，传错了 HUD 就是不显示，也不报错。
- 敌人的 `AIControllerClass` 和 `AutoPossessAI` 在 Pawn 的分类下，不在 Controller 那边。
- `EGameplayModOp` 里没有 `Additive`。老教程说的 Additive 对应现在的 `AddFinal`。
- 5.3 之后 GE 是组件化的，`ApplicationTagRequirements`、`GrantedTags` 那些都废弃了，要用 `UTargetTagRequirementsGameplayEffectComponent` 之类的组件。面板上显示的名字和类名对不上，容易找错。

## 关于资源

有两套第三方资源没有入库，clone 下来会缺引用：

- `Content/StoneGolem`（约 900MB，Boss 模型和动画）
- `Content/_rainstoryRPGUI`（约 45MB，UI 素材）

`Content/Assets`、`Content/Anim`、`Content/UI` 这些引擎和商城内容也在 `.gitignore` 里，理由一样，体积太大。

关卡用的是 World Partition，Actor 实际存在 `Content/__ExternalActors__` 下。这个目录也没入库，所以只 clone 代码的话打开 `Lvl_ThirdPerson` 是一张空地图。要完整跑起来得拿到这几个目录。

## 还没做的

- 攻击判定是单次球形重叠，没有 Sweep，快速移动的目标可能漏判，也没做命中去重
- 没有网络同步，纯单机
- 没有单元测试。本地决策那部分是纯函数，其实挺适合补
- StoneGolem 的血量只有 80，打起来太脆，GE 里还没调

## AI 决策插件

`Plugins/ARPGAIAgent` 是一个独立插件，挂个组件就能给任意角色加一层异步决策，不依赖游戏模块，靠 GAS 的标准接口和反射读属性。

决策分两层：本地规则先给结果，远程 LLM 回来之后只做修正。超时、请求失败、返回的东西解析不出来，一律静默留在本地结果上，所以断网也能跑，只是没有那层修正。

HTTP 那部分把收包和 JSON 解析挪到了 Http 线程。UE 默认收完包回主线程再解析，解析这一段是占着主线程的。

接的是 DeepSeek 的 chat completions。实测能拿到返回并改到行为上，后来账户欠费一直 402，压测就没做。API Key 走环境变量，不进版本库。

细节和设计文档在 [`Plugins/ARPGAIAgent/README.md`](Plugins/ARPGAIAgent/README.md)。
