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

	// 重生时 Pawn 是"世界已在运行"之后才 Spawn 的：引擎先 Spawn 再 Possess
	//（GameModeBase.cpp:1355-1368），BeginPlay 里 GetController() 还是空的，
	// 那一帧建不了 HUD。所以在被接管时补一次。
	virtual void PossessedBy(AController* NewController) override;
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
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;    // P 键：发射火球（IA_Fire）
	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> AttackAbilityClass;//Skill to activate
	
	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> RollAbilityClass;

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> BlockAbilityClass;   // 格挡技能（GA_Block）

	// ---- P 键火球 ----
	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> FireAbilityClass;    // 不填默认就是 C++ 的 UUARPGGameplayAbility_FireProjectile

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	float FireCooldownSeconds = 5.f;                   // 冷却 5 秒

	// 冷却还剩几秒（0 = 可用）。UFUNCTION 是为了 HUD / 蓝图都能直接问
	UFUNCTION(BlueprintPure, Category = "Ability")
	float GetFireCooldownRemaining() const;

	float LastFireTime = -1000.f;                      // 上一次成功发射的游戏时间
	float LastShownCooldown = -1.f;                    // 上次推给 HUD 的值，避免每帧重复刷 Slate

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayEffect> StaminaRegenEffectClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUARPGHUDWidget> HUDWidgetClass;
	UPROPERTY()
	TObjectPtr<UUARPGHUDWidget> HUDWidget;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> DeathScreenClass;

	// 建血条/体力条。幂等：已经有就不再建，所以 BeginPlay 和 PossessedBy 都可以随便调。
	// 拿不到 Controller 或 AttributeSet 时会自动放弃，由另一条路径补上
	void InitHUD();

	virtual void OnHealthChanged(const FOnAttributeChangeData& ChangeData) override;
	void OnStaminaChanged(const FOnAttributeChangeData& ChangeData);   // 只给玩家 HUD 用，不用走基类虚函数

	// HUD 状态文字：Tag 变化 / 体力变化时调 RefreshStatusText，由它统一决定显示哪一条
	void OnBlockingTagChanged(const FGameplayTag Tag, int32 NewCount);
	void OnStunnedTagChanged(const FGameplayTag Tag, int32 NewCount);
	void RefreshStatusText();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaSeconds) override;   // 只用来把火球冷却推给 HUD
private:
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_Attack(const FInputActionValue& Value);
	void Input_Roll(const FInputActionValue& Value);
	void Input_Block_Start(const FInputActionValue& Value);   // 按下格挡键：举盾
	void Input_Block_End(const FInputActionValue& Value);     // 松开格挡键：放下盾
	void Input_Fire(const FInputActionValue& Value);          // P 键：发射火球
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	// DeathMontage 不在这里声明：基类 AARPGCharacterBase.h:40 已经有了，
	// UHT 不允许子类成员遮蔽（shadowing is not allowed），直接用基类的即可

	virtual void HandleDeath() override;

	// 角色被销毁时（关卡重载 / 存档点复活前拆尸体）把自己建的 HUD 带走，
	// 否则新 Pawn 的 BeginPlay 会再建一个，屏幕上叠出两条血条
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

};
