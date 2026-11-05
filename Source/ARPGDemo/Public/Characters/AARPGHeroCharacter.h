// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"  
#include "InputAction.h" 
#include "InputActionValue.h" 
#include "CoreMinimal.h"
#include "AARPGCharacterBase.h"
#include "Blueprint/UserWidget.h"
#include "UI/UARPGHUDWidget.h"
#include "Components/StaticMeshComponent.h"   // UStaticMeshComponent
#include "Abilities/GameplayAbility.h"

#include "AARPGHeroCharacter.generated.h"

/**
 * 
 */
UCLASS()
class ARPGDEMO_API AAARPGHeroCharacter : public AAARPGCharacterBase
{

	GENERATED_BODY()
public:
	AAARPGHeroCharacter();
	virtual void BeginPlay() override;
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;//Move
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;//look
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> AttackAction;//Attack
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> RollAction;
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> BlockAction;   // 格挡：按住举盾，松手放下
	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> AttackAbilityClass;//Skill to activate
	
	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> RollAbilityClass;

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> BlockAbilityClass;   // 格挡技能（GA_Block）

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayEffect> StaminaRegenEffectClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUARPGHUDWidget> HUDWidgetClass;
	UPROPERTY()
	TObjectPtr<UUARPGHUDWidget> HUDWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> DeathScreenClass;

	virtual void OnHealthChanged(const FOnAttributeChangeData& ChangeData) override;
	void OnStaminaChanged(const FOnAttributeChangeData& ChangeData);   // 只给玩家 HUD 用，不用走基类虚函数

	// HUD 状态文字：Tag 变化 / 体力变化时调 RefreshStatusText，由它统一决定显示哪一条
	void OnBlockingTagChanged(const FGameplayTag Tag, int32 NewCount);
	void OnStunnedTagChanged(const FGameplayTag Tag, int32 NewCount);
	void RefreshStatusText();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
private:
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_Attack(const FInputActionValue& Value);
	void Input_Roll(const FInputActionValue& Value);
	void Input_Block_Start(const FInputActionValue& Value);   // 按下格挡键：举盾
	void Input_Block_End(const FInputActionValue& Value);     // 松开格挡键：放下盾
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	// DeathMontage 不在这里声明：基类 AARPGCharacterBase.h:40 已经有了，
	// UHT 不允许子类成员遮蔽（shadowing is not allowed），直接用基类的即可

	virtual void HandleDeath() override;

};
