#include "AbilitySystem/UARPGGameplayAbility_LightAttack.h"    // 带子目录名
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"   // WaitDelay 的定义；用 AbilityTask 而不是普通定时器，好处是技能结束时它会被自动回收

#include "Animation/AnimMontage.h"    // UAnimMontage 的定义，AttackMontage 这个 UPROPERTY 用它
#include "Animation/AnimInstance.h"   // UAnimInstance 的定义；Montage_GetCurrentSection / Montage_SetNextSection 都是它的成员函数
#include "Animation/AnimSequenceBase.h"   // UAnimSequenceBase 的定义；下面要靠它拿到每一刀动画片段自身的长度

#include "GameFramework/Character.h"      // ACharacter 的定义；
#include "GameFramework/CharacterMovementComponent.h"   // 移动组件的定义；DisableMovement 和 SetMovementMode 都在这里

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"      // 播放蒙太奇并等待的 AbilityTask 定义
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"       // 等待 GameplayEvent 的 AbilityTask 定义；头文件里只有前置声明，这里必须补全


UUARPGGameplayAbility_LightAttack::UUARPGGameplayAbility_LightAttack()   // 构造函数
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;   // 每个角色一份技能实例；这样技能里可以有自己的成员变量，Day 4 的连击计数就靠它

	ComboSections = { FName("Combo1"),FName("Combo2"),FName("Combo3"),FName("Combo4")};
	EndSectionName = FName("ComboEnd");
}

void UUARPGGameplayAbility_LightAttack::ActivateAbility(    // 技能被激活的入口
	const FGameplayAbilitySpecHandle Handle,                // 这次激活的编号
	const FGameplayAbilityActorInfo* ActorInfo,             // 施放者与受作用者的信息
	const FGameplayAbilityActivationInfo ActivationInfo,    // 是否为预测激活
	const FGameplayEventData* TriggerEventData)             // 触发事件的数据，今天用不到
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);   // 先让基类完成激活流程，它会把 GetActorInfo 之类的准备好

	CachedSpecHandle = Handle;                  // 把编号存起来，等回调触发时才能调用 EndAbility
	CachedActorInfo = ActorInfo;                // 把执行者信息存起来，同理
	CachedActivationInfo = ActivationInfo;      // 把激活信息存起来，同理
	bFinished = false;                          // 每次激活都重置标志位，否则第二次放技能会被上一次的 true 挡住
	ComboCount = 0;                             // 每次激活都从第 0 刀开始；不重置的话上一轮打到第几刀会带到下一轮

	if (!AttackMontage)                         // 检查 montage 有没有配置；没配就什么都做不了
	{
		FinishAttack();                         // 直接收尾，避免技能卡在激活状态
		return;                                 // 提前返回，后面的代码不再执行
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))   // 检查并扣除消耗与冷却；返回 false 表示体力不够或还在冷却
	{
		FinishAttack();                         // 放不出来也必须主动结束，否则这个技能会永远卡在激活状态
		return;                                 // 提前返回
	}
	if (ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo()))//拿到技能作用的那个人物
	{
		AvatarCharacter->GetCharacterMovement()->DisableMovement();// 关掉移动组件，整段攻击期间都不接受移动输入，直到 FinishAttack 才解锁
	}





	UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(   // 创建"播蒙太奇并等待播完"的异步任务
		this,                                   // 参数一 OwningAbility：这个任务属于哪个技能，技能结束时任务会被自动回收
		NAME_None,                              // 参数二 TaskInstanceName：给同类任务去重用的名字，没有就填空的 FName
		AttackMontage,                          // 参数三 MontageToPlay：要播哪一段蒙太奇
		1.f,                                    // 参数四 Rate：播放速度，1 表示原速
		NAME_None);                             // 参数五 StartSection：留空表示从蒙太奇默认的段开始；传错段名会导致播放直接失败、动作完全出不来，所以这里不填

	if (!Task)                                  // 创建失败的情况（比如 montage 为空）
	{
		FinishAttack();                         // 收尾
		return;                                 // 提前返回
	}

	Task->OnCompleted.AddDynamic(this, &UUARPGGameplayAbility_LightAttack::OnMontageCompleted);   // 绑定"播完"的回调：完整播到结尾时通知我
	Task->OnInterrupted.AddDynamic(this, &UUARPGGameplayAbility_LightAttack::OnMontageInterrupted); // 绑定"被打断"的回调：被别的东西顶掉时通知我
	Task->OnBlendOut.AddDynamic(this, &UUARPGGameplayAbility_LightAttack::OnMontageBlendOut);     // 绑定"混合出去"的回调：开始淡出时通知我
	Task->OnCancelled.AddDynamic(this, &UUARPGGameplayAbility_LightAttack::OnMontageCancelled);   // 绑定"没播起来"的回调：蒙太奇播放失败时通知我；不绑的话技能会一直卡着，只能等保险丝

	Task->ReadyForActivation();                 // 这一步才真正把任务挂上去开始运行；不调用的话什么都不播
	UAbilityTask_WaitGameplayEvent* WaitComboTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(// 创建"等一个事件"的异步任务
		this,
		// 参数一 OwningAbility：这个任务属于哪个技能
		FGameplayTag::RequestGameplayTag(FName("Event.Combo.Next")), //参数二 EventTag：等哪个事件；Tag 必须在 ini 里登记过，否则拿到空 Tag
		nullptr,// 参数三 OptionalExternalTarget：监听别人的 ASC 时才填，监听自己就空
		false,// 参数四 OnlyTriggerOnce：false 表示可以反复收到，true 表示收到一次就销毁
		true// 参数五 OnlyMatchExact：true 表示只认这个精确 Tag，不认它的子 Tag
	);
	ComboTask = WaitComboTask;// 存起来，EndAbility 时 GAS 会自动销毁它，这里存是为了将来扩展（比如中途取消监听）

	if (WaitComboTask)
	{
		WaitComboTask->EventReceived.AddDynamic(this, &UUARPGGameplayAbility_LightAttack::OnComboNextReceived); // 收到事件就调用 OnComboNextReceived
		WaitComboTask->ReadyForActivation();// 挂上去开始监听
	}
	if (ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo()))//拿到人物
	{
		UAnimInstance* AnimInst = AvatarCharacter->GetMesh()->GetAnimInstance();   // 拿到骨骼网格体上的动画实例；改名拿到它才能改蒙太奇的走向

		if (AnimInst && AttackMontage)   // 两个都要判空；任何一个为空的话，后面调用就会崩
		{
			UE_LOG(LogTemp, Warning, TEXT("[Combo] start section=%s"), *AnimInst->Montage_GetCurrentSection(AttackMontage).ToString());   // 打日志确认是从哪一段开始播的
		}
	}

	ScheduleSectionStop();   // 安排"这一刀播完就停"的定时器；这是控制攻击时长的关键，不再依赖资产里 Section 的 Next 链

	UAbilityTask_WaitDelay* FuseTask = UAbilityTask_WaitDelay::WaitDelay(this, FuseTime);   // 创建"等 N 秒"的异步任务
	if (FuseTask)
	{
		FuseTask->OnFinish.AddDynamic(this, &UUARPGGameplayAbility_LightAttack::OnFuseExpired);   // 到点就强制收尾，等于给这次攻击上了一道保险
		FuseTask->ReadyForActivation();   // 挂上去开始计时
	}
}

void UUARPGGameplayAbility_LightAttack::OnMontageCompleted()      // 蒙太奇完整播完
{
	FinishAttack();                             // 收尾结束技能
}

void UUARPGGameplayAbility_LightAttack::OnMontageInterrupted()    // 蒙太奇被打断
{
	FinishAttack();                               // 同样收尾，没有这一步会出现"技能永远结束不了"
}

void UUARPGGameplayAbility_LightAttack::OnMontageBlendOut()       // 蒙太奇开始混合出去
{
	FinishAttack();                             // 同样收尾
}

void UUARPGGameplayAbility_LightAttack::OnMontageCancelled()      // 蒙太奇压根没播起来
{
	UE_LOG(LogTemp, Warning, TEXT("[Combo] montage CANCELLED: play failed"));   // 打日志；看到它就说明蒙太奇没播成功，动作自然出不来
	FinishAttack();                             // 播不出来也必须收尾，否则技能会一直卡在激活状态
}

void UUARPGGameplayAbility_LightAttack::FinishAttack()            // 统一的收尾函数
{
	if (bFinished)                              // 如果已经结束过了
	{
		return;                                 // 直接返回；OnCompleted 和 OnBlendOut 往往会都触发，这一步挡住第二次
	}
	bFinished = true;                           // 先立起标志位，防止重入

	if (UWorld* World = GetWorld())   // 拿到世界；定时器挂在世界上
	{
		World->GetTimerManager().ClearTimer(SectionStopTimer);   // 技能要结束了，把"到点就停"的定时器清掉，避免它在技能结束后还触发
	}

	if (ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo()))//拿到人物
	{
		UCharacterMovementComponent* MoveComp = AvatarCharacter->GetCharacterMovement();   // 拿到移动组件
		if (MoveComp->MovementMode == MOVE_None)   // MOVE_None 是我们 DisableMovement 造成的；空中是 MOVE_Falling，不能被误改
		{
			MoveComp->SetMovementMode(MOVE_Walking);   // 恢复成正常行走模式，否则角色会永远卡住不能动
		}

	}

	EndAbility(                                 // 通知 ASC：这个技能结束了
		CachedSpecHandle,                       // 参数一：这次激活的编号，告诉系统在结束哪一个技能实例
		CachedActorInfo,                        // 参数二：执行者信息
		CachedActivationInfo,                   // 参数三：激活信息
		true,                                   // 参数四 bReplicateEndAbility：是否把结束这件事广播出去，单机传 true 无害
		false);                                 // 参数五 bWasCancelled：false 表示是正常结束，不是被取消的
}

void UUARPGGameplayAbility_LightAttack::OnComboNextReceived(FGameplayEventData Payload)
{
	ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());   // 拿到技能作用在哪个角色身上
	if (!AvatarCharacter)   // 拿不到就什么都不做
	{
		return;
	}

	UAnimInstance* AnimInst = AvatarCharacter->GetMesh()->GetAnimInstance();   // 拿到动画实例，改蒙太奇走向全靠它
	if (!AnimInst || !AttackMontage)   // 动画实例或蒙太奇为空，直接放弃这次输入
	{
		return;
	}

	const float NowTime = GetWorld()->GetTimeSeconds();   // 当前游戏时间，单位秒
	if (NowTime - LastComboAdvanceTime < ComboInputWindow)   // 离上一次接招太近，这次输入直接丢弃
	{
		return;
	}

	if (ComboCount + 1 >= ComboSections.Num())   // 已经打到最后一段（第四刀）了，后面没有下一刀，忽略
	{
		return;
	}

	LastComboAdvanceTime = NowTime;   // 记下这次接招的时间，下次要用它做间隔判断
	ComboCount = ComboCount + 1;      // 刀数加一，从当前这一刀前进到下一刀

	const FName NextSection = ComboSections[ComboCount];   // 下一刀对应的段落名字

	UE_LOG(LogTemp, Warning, TEXT("[Combo] advance to %d (%s)"), ComboCount, *NextSection.ToString());   // 打日志：准备接到第几刀

	if (UWorld* World = GetWorld())   // 拿到世界；定时器挂在世界上
	{
		World->GetTimerManager().ClearTimer(SectionStopTimer);   // 取消"到点就停"的旧定时器，否则下一刀刚起手就被掐掉
	}

	AnimInst->Montage_JumpToSection(NextSection, AttackMontage);   // 立刻跳到下一刀开始播；不等当前这段播完，接招要跟手

	ScheduleSectionStop();   // 按新的这一刀的时长，重新安排"到点就停"
}

void UUARPGGameplayAbility_LightAttack::ScheduleSectionStop()     // 按当前这一刀的时长安排停止
{
	UWorld* World = GetWorld();   // 拿到世界；定时器挂在世界上
	if (!World || !AttackMontage)   // 没有世界或没有蒙太奇就放弃，技能靠保险丝兜底
	{
		return;
	}

	float StopDelay = 1.f;   // 默认播 1 秒；下面的两种算法都用不上时用它兜底，保证这一刀一定会结束，不会把角色卡住
	float SectionLength = -1.f;   // 从资产里读到的段区间长度；-1 表示没读到

	if (ComboDurations.IsValidIndex(ComboCount) && ComboDurations[ComboCount] > 0.f)   // 蓝图里填了时长就以填的为准，这是最准的方式
	{
		StopDelay = ComboDurations[ComboCount];
	}
	else   // 没填才回退到按段区间长度自动算
	{
		const int32 Index = AttackMontage->GetSectionIndex(ComboSections[ComboCount]);   // 当前这一刀对应第几段
		if (Index != INDEX_NONE)   // 段存在才去读它的长度
		{
			SectionLength = AttackMontage->GetSectionLength(Index);   // 先取段区间的长度，单位秒

			const TArray<FCompositeSection>& AllSections = AttackMontage->CompositeSections;   // CompositeSections 是 UAnimMontage 的 public 成员，直接访问；引擎没提供 Get 开头的函数
			if (AllSections.IsValidIndex(Index))   // 下标有效才访问
			{
				if (const UAnimSequenceBase* AnimSeq = AllSections[Index].GetLinkedSequence())   // 这一段真正链接的那个动画片段
				{
					SectionLength = AnimSeq->GetPlayLength();   // 改用动画片段自身的长度；段末尾多出来的空白就不会被算进去，动作能完整播完
				}
			}

			StopDelay = FMath::Max(SectionLength, 0.4f) + 0.05f;   // 至少播 0.4 秒，太短会一闪而过看不清；再加 0.05 秒缓冲
		}
		else   // 段名对不上，打一行日志提醒
		{
			UE_LOG(LogTemp, Warning, TEXT("[Combo] section NOT FOUND: %s"), *ComboSections[ComboCount].ToString());
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[Combo] schedule stop: index=%d, sectionLength=%.2f, delay=%.2f"),   // 打日志：这一刀打算播多久
		ComboCount, SectionLength, StopDelay);

	World->GetTimerManager().SetTimer(SectionStopTimer, this, &UUARPGGameplayAbility_LightAttack::OnSectionTimeUp, StopDelay, false);   // 到点调用 OnSectionTimeUp
}

void UUARPGGameplayAbility_LightAttack::OnSectionTimeUp()         // 定时器到点
{
	ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());   // 拿到角色
	if (AvatarCharacter)   // 拿得到角色才去停蒙太奇
	{
		if (UAnimInstance* AnimInst = AvatarCharacter->GetMesh()->GetAnimInstance())   // 拿到动画实例
		{
			UE_LOG(LogTemp, Warning, TEXT("[Combo] section time up, stopping montage"));   // 打日志：这一刀播够了，主动停
			AnimInst->Montage_Stop(0.2f, AttackMontage);   // 主动停掉蒙太奇；它会走"被打断"回调，那里会调用 FinishAttack 结束技能
			return;
		}
	}

	FinishAttack();   // 拿不到动画实例就自己收尾，保证技能一定会结束，不把角色卡住
}

void UUARPGGameplayAbility_LightAttack::OnFuseExpired()   // 保险丝到期
{
	UE_LOG(LogTemp, Warning, TEXT("[Combo] FUSE fired: montage never ended"));   // 打一行日志；看到它就说明蒙太奇链路有问题
	FinishAttack();   // 强制收尾：恢复移动 + 结束技能，避免角色永久卡死
}

void UUARPGGameplayAbility_LightAttack::RestoreMovement()        // 统一的解锁函数
{
	ACharacter* AvatarCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());   // 拿到角色
	if (!AvatarCharacter)                       // 拿不到就直接返回
	{
		return;
	}

	UCharacterMovementComponent* MoveComp = AvatarCharacter->GetCharacterMovement();   // 拿到移动组件
	if (MoveComp && MoveComp->MovementMode == MOVE_None)   // 只在我们自己锁住时才恢复；空中是 MOVE_Falling，不能动它
	{
		MoveComp->SetMovementMode(MOVE_Walking);   // 恢复成正常行走模式
	}
}

