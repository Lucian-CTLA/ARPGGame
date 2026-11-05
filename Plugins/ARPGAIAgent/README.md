# ARPG AI Agent

组件化的异步 AI 决策层。**LocalFirst**：本地规则永远先落地，远程 LLM 只做升级修正，
超时/失败/返回非法值时静默降级，绝不阻塞、绝不影响手感。

## 四个设计点

| 要求 | 实现方式 | 关键位置 |
|---|---|---|
| 组件化接入 | `UARPGAIAgentComponent`（`BlueprintSpawnableComponent`），挂谁身上谁有大脑；靠 `IAbilitySystemInterface` + 反射读属性，**不依赖游戏模块** | `ARPGAIAgentComponent.h` |
| 异步决策 | `SetDelegateThreadPolicy(CompleteOnHttpThread)` 把收包与 JSON 解析挪出主线程 | `ARPGAIAgentRemoteClient.cpp` |
| 线程安全 | 跨线程只搬 POD；`TWeakObjectPtr` 在 GameThread 构造后再进 lambda；`AsyncTask(GameThread)` 回主线程；`RequestId` 去乱序；`FCriticalSection` 保护共享状态 | 同上 + 组件 `HandleRemoteResult` |
| LocalFirst | 一次循环：本地决策**立即生效** → 异步问远程 → 回来校验通过才升级 | `RunDecisionCycle` |

## 一次决策的完整时序

```
Tick (节流到 DecisionInterval)
  │
  ├─ SampleWorldSnapshot()          [GameThread] 反射读 GAS 属性 + 算距离 → POD
  │
  ├─ MakeLocalDecision()            [GameThread] 纯规则，0 网络延迟
  ├─ ApplyDecision()                [GameThread] ★立即生效★ ← LocalFirst 的灵魂
  │
  └─ TryIssueRemoteRequest()        [GameThread] 抢额度 → 发 HTTP
        │
        ├─ HTTP 线程：收包 + JSON 解析 → FARPGDecision（POD）
        └─ AsyncTask(GameThread)：判活 → 去乱序 → 校验 → 升级或丢弃
```

## 安全约定

- **API Key 走环境变量**，不写进 ini、不进版本库：`ARPG_AI_API_KEY`。
  取不到 → 自动降级为纯本地。
- **动作白名单**：模型返回的 action 不在枚举里 → 判非法 → 保留本地决策。
- **置信度门槛** `MinAcceptConfidence` 以下的远程建议不采纳。
- **并发额度 + 熔断**：由 `UARPGAIAgentSubsystem`（World 级）统一管，
  服务挂了全场一起降级，而不是每个敌人各试三次。

## 接入（对游戏侧只有两步）

1. 项目 `Build.cs` 的 `PublicDependencyModuleNames` 加一项 `"ARPGAIAgent"`。
2. 在角色蓝图里 Add Component → `ARPG AI Agent`，填 `ActionAbilityMap`：
   Key 用 `Attack` / `HeavyAttack` / `Block` / `Roll` / `Chase`，Value 是对应的 GA 类。
   没配映射的动作只会广播 `OnDecisionReady`，不会自己动 —— 移动类交给行为树即可。

可选：在 Project Settings → Plugins → ARPG AI Agent 调参。

## 调试

```
ARPGAIAgent.Stats        // 本地/远程/采纳/拒绝/超时/失败计数与平均延迟
```
把 `bVerboseLog` 打开可看到每次决策的来源与耗时。
