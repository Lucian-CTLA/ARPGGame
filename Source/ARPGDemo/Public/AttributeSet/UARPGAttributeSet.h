#pragma once
 
#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h" 
#include "UARPGAttributeSet.generated.h"
#define ATTRIBUTE_ACCESSORS(ClassName,PropertyName)\
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)
/**
 * 
 */
UCLASS()
class ARPGDEMO_API UUARPGAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
public:
	UUARPGAttributeSet();

	UPROPERTY(BlueprintReadOnly, Category = "Attributs|Vital")
	FGameplayAttributeData Health;//the health
	ATTRIBUTE_ACCESSORS(UUARPGAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Attributs|Vital")
	FGameplayAttributeData MaxHealth;//max health
	ATTRIBUTE_ACCESSORS(UUARPGAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category = "Attributs|Vital")
	FGameplayAttributeData Stamina;//Attack and Roll Stamina
	ATTRIBUTE_ACCESSORS(UUARPGAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, Category = "Attributs|Vital")
	FGameplayAttributeData MaxStamina;//max Stamina
	ATTRIBUTE_ACCESSORS(UUARPGAttributeSet,MaxStamina)

	UPROPERTY(BlueprintReadOnly, Category = "Attributs|Offensive")
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UUARPGAttributeSet, AttackPower)

	UPROPERTY(BlueprintReadOnly, Category = "Attributs|Defensive")
	FGameplayAttributeData DefensePower;//
	ATTRIBUTE_ACCESSORS(UUARPGAttributeSet, DefensePower)

	UPROPERTY(BlueprintReadOnly, Category = "Attributs|Meta")
	FGameplayAttributeData Damage;
	ATTRIBUTE_ACCESSORS(UUARPGAttributeSet, Damage)

	// ===== 格挡配置：这两个是普通常量，不是属性，不要加 ATTRIBUTE_ACCESSORS =====
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attributs|Defensive")
	float BlockStaminaCost = 20.f;   // 挡一次扣多少体力；体力不够就破防

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attributs|Defensive")
	float BlockDamageScale = 0.3f;   // 挡住后实际吃到的比例，0.3 = 减伤 70%

	// 敌人举盾（State.Guard）时正面吃到的比例，0.2 = 挡掉 80%。
	// 跟上面的 BlockDamageScale 分开：玩家举盾要扣体力、会破防，敌人这套只管减伤
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attributs|Defensive")
	float GuardDamageScale = 0.2f;


protected:
	//Attribute:Which attribute is being changed this round?
	//NewValue:
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

};
