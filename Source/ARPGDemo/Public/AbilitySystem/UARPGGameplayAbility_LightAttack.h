#pragma once
#include "CoreMinimal.h"                        // UE 基础类型
#include "Abilities/GameplayAbility.h"          // UGameplayAbility 技能基类的定义
#include "Animation/AnimMontage.h"
#include "TimerManager.h"                       // FTimerHandle 的定义；下面要用定时器来控制"这一刀播多久"

#include "UARPGGameplayAbility_LightAttack.generated.h"   // 反射生成代码，放最后

class UAbilityTask_WaitGameplayEvent; 
UCLASS()             // 让这个类进入反射系统

class ARPGDEMO_API UUARPGGameplayAbility_LightAttack : public UGameplayAbility   // 继承技能基类
{
	GENERATED_BODY()                            // 生成代码插入点

public:                                         // 对外开放
	UUARPGGameplayAbility_LightAttack();        // 构造函数的声明

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")   // EditDefaultsOnly：只能在类默认面板里设置，运行中的实例改不了
		TObjectPtr<UAnimMontage> AttackMontage;                               // 这个技能要播的动作蒙太奇；具体播哪个在蓝图子类里选，C++ 里不写死路径

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")   // 同上，在 GA_LightAttack 蓝图的 Class Defaults 里填
		TArray<float> ComboDurations;                                         // 每一刀要播多久，单位秒；按顺序对应 Combo1~Combo4。填了就以这里为准，没填才自动按段的区间长度算

protected:                                      // 子类可见
	virtual void ActivateAbility(               // 技能被激活时引擎调用这个函数
		const FGameplayAbilitySpecHandle Handle,            // 这次激活的唯一编号，结束技能时要原样传回去
		const FGameplayAbilityActorInfo* ActorInfo,         // 谁释放的、作用在谁身上
		const FGameplayAbilityActivationInfo ActivationInfo, // 这次激活是不是本地预测出来的
		const FGameplayEventData* TriggerEventData) override; // 如果是被事件触发的，事件数据在这里；今天用不到

	UFUNCTION()                                 // 必须加：动态委托 AddDynamic 只能绑反射函数
		void OnMontageCompleted();                  // 蒙太奇完整播完时触发

	UFUNCTION()                                 // 同上
		void OnMontageInterrupted();                // 蒙太奇被别的东西打断时触发

	UFUNCTION()                                 // 同上
		void OnMontageBlendOut();                   // 蒙太奇开始混合出去时触发

	UFUNCTION()                                 // 同上
		void OnMontageCancelled();                  // 蒙太奇压根没播起来时触发（比如段名不对、播放被打回）

private:                                        // 只有自己能访问
	void FinishAttack();   // 统一收尾：不管什么原因，最后都走这里结束技能

	float FuseTime = 8.f;   // 保险丝时长，单位秒；要设得比"四刀连完"的总时间还长，否则正常连击会被它误伤打断

	UFUNCTION()
	void OnFuseExpired();   // 保险丝到期时调用：蒙太奇万一一直不结束，靠它强制收尾

	void RestoreMovement();                     // 统一解锁移动：只在我们自己锁住时才恢复

	int32 ComboCount = 0;                       // 当前打到了第几刀；0 表示第一刀，1 表示第二刀；每次激活技能都要重置成 0

	FTimerHandle SectionStopTimer;              // "到点就停"的定时器；用它代替蒙太奇里 Section 的 Next 链，这样资产怎么配都不会连播

	void ScheduleSectionStop();                 // 按当前这一刀的时长，安排一个到点就停的定时器

	void OnSectionTimeUp();                     // 定时器到点：主动停掉蒙太奇，这一刀就算打完了

	float LastComboAdvanceTime = -999.f;        // 上一次接招的游戏时间；初始给一个很大的负数，保证第一次接招一定不会被节流挡住
	float ComboInputWindow = 0.15f;             // 两次接招之间至少要隔这么久，单位秒；防止同一帧塞进多个事件导致一次点击跳好几刀


	FGameplayAbilitySpecHandle CachedSpecHandle;            // 缓存这次激活的编号；回调是后来才触发的，那时拿不到参数
	const FGameplayAbilityActorInfo* CachedActorInfo = nullptr;  // 缓存执行者信息，初始给空指针避免野值
	FGameplayAbilityActivationInfo CachedActivationInfo;    // 缓存激活信息，同样是为了回调里能用

	bool bFinished = false;        // 是否已经结束过；初始为 false
	UFUNCTION()
		void OnComboNextReceived(FGameplayEventData Payload);
		TArray<FName> ComboSections;
		FName EndSectionName;// 结束段的名字，播到这里就等于蒙太奇放完了
		UAbilityTask_WaitGameplayEvent* ComboTask = nullptr; // 存一下等待事件的那个任务，技能结束时要用它
	
};

