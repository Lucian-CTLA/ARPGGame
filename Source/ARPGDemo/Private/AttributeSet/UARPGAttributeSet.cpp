#include "AttributeSet/UARPGAttributeSet.h"      // 自己的头文件排第一

#include "GameplayEffectExtension.h"                  // FGameplayEffectModCallbackData 的定义

UUARPGAttributeSet::UUARPGAttributeSet()
{
	InitHealth(100.f);          // 血量初始 100
	InitMaxHealth(100.f);       // 上限 100，表示满血出场
	InitStamina(100.f);         // 体力初始 100
	InitMaxStamina(100.f);      // 体力上限 100
	InitAttackPower(20.f);      // 攻击力 20
	InitDefensePower(5.f);      // 防御力 5
	InitDamage(0.f);            // 伤害桶 0，用完必清
}

void UUARPGAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);   // 先让父类跑完

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());     // 夹在 0 ~ 上限
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
	}
}

void UUARPGAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();

	if (Data.EvaluatedData.Attribute == GetDamageAttribute())   // 这一轮改的是伤害桶
	{
		const float LocalDamage = GetDamage();                  
		SetDamage(0.f);     // 立刻清零，否则下一刀会叠加

		if (GetOwningAbilitySystemComponent() && GetOwningAbilitySystemComponent()->
			HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Rolling"))))
		{
			return;
		}
		float FinalDamage = LocalDamage;
		if (ASC && ASC->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Blocking"))))
		{
			if (GetStamina() >= BlockStaminaCost)        // 体力够 → 挡住
			{
				FinalDamage = LocalDamage * BlockDamageScale;   // 减伤 70%
				SetStamina(GetStamina() - BlockStaminaCost);
			}
			else                                          // 体力不够 → 破防
			{
				// 1) 打标记：GA_Block 的 EndAbility 读到它，才知道这次结束是破防而不是松手
				ASC->AddLooseGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.BlockBroken")));

				// 2) 撤掉格挡：不撤的话同一帧后续伤害还会继续被减
				ASC->RemoveLooseGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Blocking")));

				// 3) 取消举盾技能 -> 走 EndAbility -> 那里统一挂硬直 + 播硬直动画
				//    这里匹配的是 GA_Block 的 AbilityTags（Ability.Block），不是 Loose Tag
				FGameplayTagContainer CancelTags;
				CancelTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability.Block")));
				ASC->CancelAbilities(&CancelTags);

				// FinalDamage 保持全额
			}
		}


		const float NewHealth = GetHealth() - FinalDamage;

		SetHealth(FMath::Clamp(NewHealth, 0.f, GetMaxHealth())); // 写回并再夹一次
	}
}

