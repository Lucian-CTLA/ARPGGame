// Copyright Lucian. All Rights Reserved.

#include "ARPGAIAgentRemoteClient.h"

#include "ARPGAIAgentSettings.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
//  构造 / 析构
// ============================================================================

FARPGAIAgentRemoteClient::FARPGAIAgentRemoteClient()
	: AliveToken(MakeShared<FARPGAIAgentAliveToken, ESPMode::ThreadSafe>())
	, InFlightList(MakeShared<FARPGAIAgentInFlightList, ESPMode::ThreadSafe>())
{
}

FARPGAIAgentRemoteClient::~FARPGAIAgentRemoteClient()
{
	CancelAll();
}

// ============================================================================
//  发起请求
// ============================================================================

int32 FARPGAIAgentRemoteClient::SendAsync(const FARPGAIWorldSnapshot& Snapshot, FOnResult&& OnResult)
{
	// 铁律①：FHttpModule::Get() 与 ProcessRequest 都必须站在 GameThread。
	check(IsInGameThread());

	const UARPGAIAgentSettings* Settings = GetDefault<UARPGAIAgentSettings>();
	if (!Settings || !Settings->IsRemoteConfigured())
	{
		return 0;
	}

	const FString ApiKey = Settings->ResolveApiKey();
	if (ApiKey.IsEmpty())
	{
		return 0;
	}

	// 请求序号在这里（主线程）分配，单调自增。
	// 组件侧记住自己最后一次发出的 Id，回包 Id 不匹配就丢弃 —— 防乱序覆盖。
	const int32 RequestId = NextRequestId.fetch_add(1, std::memory_order_relaxed);
	const double StartTime = FPlatformTime::Seconds();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();

	Request->SetVerb(TEXT("POST"));
	Request->SetURL(Settings->EndpointUrl);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	Request->SetTimeout(Settings->RequestTimeoutSec);

	// 铁律②：默认是 CompleteOnGameThread，会让 JSON 解析占用主线程。
	// 显式改成 HTTP 线程，把解析开销挪出主线程。
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

	// 请求体很短（几百字节），在主线程拼装的开销可忽略；
	// 真正重的「收包 + 建 JSON 树 + 解析」放在 HTTP 线程。
	Request->SetContentAsString(BuildRequestBody(Snapshot));

	// 只捕获「值」和「线程安全的共享对象」，绝不捕获 this。
	TWeakPtr<FARPGAIAgentAliveToken, ESPMode::ThreadSafe> WeakToken = AliveToken;
	TSharedPtr<FARPGAIAgentInFlightList, ESPMode::ThreadSafe> StrongList = InFlightList;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> StrongRequest = Request;

	{
		FScopeLock Lock(&StrongList->Lock);
		StrongList->Requests.Add(StrongRequest);
	}

	Request->OnProcessRequestComplete().BindLambda(
		[WeakToken, StrongList, StrongRequest, RequestId, StartTime, OnResult = MoveTemp(OnResult)]
		(FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bConnectedSuccessfully) mutable
		{
			// ================= 从这里开始：HTTP 线程 =================
			// 绝不碰任何 UObject / AActor / UWorld。

			FARPGDecision Decision;
			Decision.RequestId = RequestId;
			Decision.Source = EARPGDecisionSource::Remote;

			bool bOk = bConnectedSuccessfully && HttpResponse.IsValid();
			if (bOk)
			{
				const int32 Code = HttpResponse->GetResponseCode();
				// 注意：连上了不代表成功，必须看状态码。
				bOk = (Code >= 200 && Code < 300);
				if (bOk)
				{
					const FString Body = HttpResponse->GetContentAsString();
					bOk = ParseDecisionFromResponse(Body, Decision);
				}
			}

			Decision.LatencyMs = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);

			// 把自己从在飞列表里摘掉。列表是共享对象，就算 RemoteClient 已经没了它还在。
			{
				FScopeLock Lock(&StrongList->Lock);
				StrongList->Requests.Remove(StrongRequest);
			}

			// ================= 回到 GameThread =================
			AsyncTask(ENamedThreads::GameThread,
				[WeakToken, RequestId, bOk, Decision = MoveTemp(Decision), OnResult = MoveTemp(OnResult)]() mutable
				{
					// 先看主人还在不在。TWeakPtr::Pin 只在这里（GameThread）调，安全。
					TSharedPtr<FARPGAIAgentAliveToken, ESPMode::ThreadSafe> Token = WeakToken.Pin();
					if (Token.IsValid() && Token->bAlive.load(std::memory_order_acquire))
					{
						OnResult(RequestId, bOk, MoveTemp(Decision));
					}
					// 走到这里说明客户端已经销毁了 —— 什么都不做，静默丢弃。
				});
		});

	if (!Request->ProcessRequest())
	{
		FScopeLock Lock(&StrongList->Lock);
		StrongList->Requests.Remove(StrongRequest);
		return 0;
	}

	return RequestId;
}

void FARPGAIAgentRemoteClient::CancelAll()
{
	// 置死令牌：这一步是原子的，任何线程做都安全。
	// 已经在飞的回调回来时看到 false，就不会再往下走了。
	if (AliveToken.IsValid())
	{
		AliveToken->bAlive.store(false, std::memory_order_release);
	}

	// CancelRequest 本身要在 GameThread 调。若不在主线程（理论上不该发生），
	// 至少令牌已经置死，回包会被丢弃，不会有安全问题。
	if (!IsInGameThread())
	{
		AliveToken = MakeShared<FARPGAIAgentAliveToken, ESPMode::ThreadSafe>();
		return;
	}

	// 把当前列表整个换掉：旧列表可能还被某些 lambda 持有，让它自然消亡即可。
	TSharedPtr<FARPGAIAgentInFlightList, ESPMode::ThreadSafe> OldList = InFlightList;
	InFlightList = MakeShared<FARPGAIAgentInFlightList, ESPMode::ThreadSafe>();

	if (OldList.IsValid())
	{
		TArray<TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>> Snapshot;
		{
			FScopeLock Lock(&OldList->Lock);
			Snapshot = MoveTemp(OldList->Requests);
			OldList->Requests.Reset();
		}
		for (const TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>& Req : Snapshot)
		{
			if (Req.IsValid())
			{
				Req->CancelRequest();
			}
		}
	}

	// 换一个新令牌，万一这个客户端还要继续用（组件被复用时）。
	AliveToken = MakeShared<FARPGAIAgentAliveToken, ESPMode::ThreadSafe>();
}

int32 FARPGAIAgentRemoteClient::GetInFlightCount() const
{
	if (!InFlightList.IsValid())
	{
		return 0;
	}
	FScopeLock Lock(&InFlightList->Lock);
	return InFlightList->Requests.Num();
}

// ============================================================================
//  请求体 / 解析
// ============================================================================

FString FARPGAIAgentRemoteClient::BuildRequestBody(const FARPGAIWorldSnapshot& S) const
{
	const UARPGAIAgentSettings* Settings = GetDefault<UARPGAIAgentSettings>();

	// 给模型的输入只放「它决策需要的量」，不放引擎对象。
	// 距离换算成米再给模型 —— 它对厘米没概念，容易给出离谱的建议。
	const FString UserContent = FString::Printf(
		TEXT("You are the combat brain of one enemy in an action RPG.\n"
			 "Pick exactly ONE action.\n"
			 "Allowed actions: Idle, Chase, Retreat, Attack, HeavyAttack, Block, Roll.\n"
			 "Rules: melee reach is about %.1f meters; below %.0f%% health consider Retreat or Block; "
			 "HeavyAttack and Roll cost stamina, do not spam them.\n"
			 "Situation:\n"
			 "- self health: %.0f%%\n"
			 "- self stamina: %.0f%%\n"
			 "- target health: %.0f%%\n"
			 "- distance to target: %.1f meters\n"
			 "- seconds since your last attack: %.1f\n"
			 "- allies nearby: %d\n"
			 "- has target: %s\n"
			 "Reply with JSON only, no prose: {\"action\":\"<one of the allowed>\",\"confidence\":<0..1>,\"hold\":<seconds>,\"reason\":\"<short>\"}"),
		Settings ? Settings->AttackRangeCm / 100.f : 2.5f,
		(Settings ? Settings->LowHealthPct : 0.3f) * 100.f,
		S.SelfHealthPct * 100.f,
		S.SelfStaminaPct * 100.f,
		S.TargetHealthPct * 100.f,
		S.DistanceToTarget / 100.f,
		S.TimeSinceLastAttack,
		S.AllyCount,
		S.bHasTarget ? TEXT("yes") : TEXT("no"));

	// 走 FJsonObject 序列化，避免手写引号把 JSON 弄坏。
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Settings ? Settings->ModelName : TEXT("gpt-4o-mini"));
	Root->SetNumberField(TEXT("temperature"), 0.2);

	TArray<TSharedPtr<FJsonValue>> Messages;
	{
		TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
		Msg->SetStringField(TEXT("role"), TEXT("user"));
		Msg->SetStringField(TEXT("content"), UserContent);
		Messages.Add(MakeShared<FJsonValueObject>(Msg));
	}
	Root->SetArrayField(TEXT("messages"), Messages);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

FString FARPGAIAgentRemoteClient::StripCodeFence(const FString& In)
{
	FString Out = In;
	Out.TrimStartAndEndInline();

	if (Out.StartsWith(TEXT("```")))
	{
		// 去掉开头那一行（```json 或 ```）
		int32 NewlineIdx = INDEX_NONE;
		if (Out.FindChar(TEXT('\n'), NewlineIdx) && NewlineIdx != INDEX_NONE)
		{
			Out = Out.Mid(NewlineIdx + 1);
		}
		if (Out.EndsWith(TEXT("```")))
		{
			Out = Out.Left(Out.Len() - 3);
		}
	}
	return Out.TrimStartAndEnd();
}

bool FARPGAIAgentRemoteClient::ParseDecisionFromResponse(const FString& ResponseBody, FARPGDecision& OutDecision)
{
	if (ResponseBody.IsEmpty())
	{
		return false;
	}

	// 第一层：chat completions 信封
	TSharedPtr<FJsonObject> Root;
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Root->TryGetArrayField(TEXT("choices"), Choices) || Choices == nullptr || Choices->Num() == 0)
	{
		return false;
	}

	const TSharedPtr<FJsonValue>& FirstChoice = (*Choices)[0];
	if (!FirstChoice.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* MessageObj = nullptr;
	if (!FirstChoice->TryGetObject(MessageObj) || MessageObj == nullptr || !MessageObj->IsValid())
	{
		return false;
	}

	// OpenAI 风格是 {message:{content:...}}
	FString Content;
	if (!(*MessageObj)->TryGetStringField(TEXT("content"), Content))
	{
		return false;
	}

	// 第二层：模型返回的那段 JSON（可能裹着 markdown 代码围栏）
	Content = StripCodeFence(Content);

	TSharedPtr<FJsonObject> DecisionObj;
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
		if (!FJsonSerializer::Deserialize(Reader, DecisionObj) || !DecisionObj.IsValid())
		{
			return false;
		}
	}

	// ---- 白名单校验：模型说了不在枚举里的动作，一律判非法 ----
	FString ActionName;
	if (!DecisionObj->TryGetStringField(TEXT("action"), ActionName) || ActionName.IsEmpty())
	{
		return false;
	}

	EARPGAIAction ParsedAction = EARPGAIAction::Idle;
	if (!ARPGAIAgentUtil::TryParseAction(ActionName, ParsedAction))
	{
		return false;
	}

	OutDecision.Action = ParsedAction;
	OutDecision.Source = EARPGDecisionSource::Remote;

	double Confidence = 0.0;
	DecisionObj->TryGetNumberField(TEXT("confidence"), Confidence);
	// 防御性钳制：模型可能给 0~100，也可能给 0~1
	OutDecision.Confidence = FMath::Clamp(static_cast<float>(Confidence), 0.f, 1.f);

	double Hold = 0.5;
	DecisionObj->TryGetNumberField(TEXT("hold"), Hold);
	OutDecision.HoldSec = FMath::Clamp(static_cast<float>(Hold), 0.f, 5.f);

	FString Reason;
	DecisionObj->TryGetStringField(TEXT("reason"), Reason);
	OutDecision.Reason = Reason;

	return true;
}
