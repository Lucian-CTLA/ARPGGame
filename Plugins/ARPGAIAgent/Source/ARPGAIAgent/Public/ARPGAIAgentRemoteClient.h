// Copyright Lucian. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include "Interfaces/IHttpRequest.h"
#include "ARPGAIAgentTypes.h"

/**
 * 存活令牌。
 *
 * 为什么需要它：异步回包时，持有客户端的组件可能已经被销毁了（关卡切换 / 敌人死亡）。
 * lambda 捕获裸 this 就会踩野指针。这里用一个线程安全的共享标志，
 * lambda 只捕获这个 token（强引用，保证它自己活着），回调时先看一眼 bAlive。
 * 这是「不用 TWeakObjectPtr 也能安全跨线程判活」的通用手法。
 */
struct FARPGAIAgentAliveToken
{
	std::atomic<bool> bAlive{ true };
};

/**
 * 在飞请求列表。
 *
 * 为什么单独拎出来做成共享对象：回包发生在 HTTP 线程，那时 RemoteClient 本身
 * 可能已经被销毁了（组件 EndPlay）。如果直接在回调里访问 this->InFlightRequests
 * 就是野指针。把它做成一个独立对象，lambda 捕获它的 TSharedPtr（强引用），
 * 无论 RemoteClient 在不在，这个列表都活着 —— 回调里就能安全地加锁摘除自己。
 */
struct FARPGAIAgentInFlightList
{
	FCriticalSection Lock;
	TArray<TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> Requests;
};


/**
 * 远程（LLM）决策客户端。
 *
 * 这是整个插件「异步 + 线程安全」的核心，四条铁律：
 *
 *  ① 发起必须在 GameThread
 *     FHttpModule::Get() 拿到的是引擎全局对象，CreateRequest / ProcessRequest
 *     都只能在 GameThread 调。所以 SendAsync() 的调用方必须站在主线程。
 *
 *  ② 回调默认也在 GameThread —— 必须手动改
 *     UE 5.x 的 IHttpRequest 默认 DelegateThreadPolicy = CompleteOnGameThread
 *     （源码 GenericPlatformHttp.cpp:129，内部走 HttpManager.AddGameThreadTask 排队回主线程）。
 *     默认行为意味着 JSON 解析会占用主线程 —— 几十个敌人同时回包就会掉帧。
 *     所以这里显式 SetDelegateThreadPolicy(CompleteOnHttpThread)，
 *     让「收包 + 解析」发生在 HTTP 线程，主线程只拿到一个解析好的 POD。
 *
 *  ③ 跨线程只能搬 POD，不能搬 UObject
 *     解析结果是一个纯值类型 FARPGDecision，MoveTemp 进 lambda 丢回 GameThread。
 *     全程不持有 AActor / UObject 指针（那是 TWeakObjectPtr 的活，见组件）。
 *
 *  ④ 判活不靠 this，靠共享令牌
 *     回调只捕获 token（强引用）和 request（强引用），绝不捕获 this。
 *
 * 对外的承诺：OnResult 一定在 GameThread 被调用。
 * 所以调用方（组件）可以完全按单线程写，不用自己 AsyncTask。
 */
class FARPGAIAgentRemoteClient
{
public:
	/** RequestId, bSucceeded, Decision —— 都在 GameThread 回调 */
	using FOnResult = TFunction<void(int32 /*RequestId*/, bool /*bSucceeded*/, FARPGDecision /*Decision*/)>;

	FARPGAIAgentRemoteClient();
	~FARPGAIAgentRemoteClient();

	/**
	 * 发起一次异步决策请求。必须在 GameThread 调。
	 * @return 本次请求的 Id；<=0 表示没发出去（未配置 / Key 缺失 / ProcessRequest 失败）
	 */
	int32 SendAsync(const FARPGAIWorldSnapshot& Snapshot, FOnResult&& OnResult);

	/** 取消所有在飞的请求。组件 EndPlay / 关卡切换时调，防止回包打到已销毁对象。 */
	void CancelAll();

	/** 当前在飞数量（调试用） */
	int32 GetInFlightCount() const;

private:
	/** 构造请求体（prompt + JSON）。几百字节，放在主线程做，开销可忽略。 */
	FString BuildRequestBody(const FARPGAIWorldSnapshot& Snapshot) const;

	/** 在 HTTP 线程上解析响应文本 → POD。这里绝不能碰任何 UObject。 */
	static bool ParseDecisionFromResponse(const FString& ResponseBody, FARPGDecision& OutDecision);

	/** 把 markdown 代码围栏去掉。真实 LLM 经常返回 ```json ... ``` */
	static FString StripCodeFence(const FString& In);

private:
	/** 单调自增的请求序号。用于丢弃过期回包（见组件侧的 LastIssuedRequestId）。 */
	std::atomic<int32> NextRequestId{ 1 };

	/** 本客户端的存活标志。CancelAll / 析构时置 false，异步回调据此丢弃。 */
	TSharedPtr<FARPGAIAgentAliveToken, ESPMode::ThreadSafe> AliveToken;

	/** 在飞请求。做成共享对象，让回调能安全地自己摘除，不碰 this。 */
	TSharedPtr<FARPGAIAgentInFlightList, ESPMode::ThreadSafe> InFlightList;
};
