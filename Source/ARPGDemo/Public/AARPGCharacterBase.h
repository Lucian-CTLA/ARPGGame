// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"  
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "Sound/SoundBase.h"
#include "AARPGCharacterBase.generated.h"

class UAbilitySystemComponent;
class UUARPGAttributeSet;

UCLASS()
class ARPGDEMO_API AAARPGCharacterBase : public ACharacter,public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AAARPGCharacterBase();
	virtual void BeginPlay() override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;//AbilitySystemComponentPointer

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ability")
	TObjectPtr<UUARPGAttributeSet> AttributeSet;//Health,MaxHealth,Satmina,MaxSatmina,AttackPower,DefensePower,Damage set

	virtual void PossessedBy(AController* NewController)override;
	//The moment it's taken over by a controller, the engine calls it automatically


	virtual void OnHealthChanged(const FOnAttributeChangeData& ChangeData);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death")
	TObjectPtr<UAnimMontage> DeathMontage;

	// 死亡音效。基类只用一条通用的（SC_Character_Collapse），
	// 各敌人 BP 可以覆盖成自己的（SC_Troll_Death / SC_Guardian_Death / SC_GiantDeath）
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death")
	TObjectPtr<USoundBase> DeathSound;

	virtual void HandleDeath() ;

	bool bIsDead = false;
};
