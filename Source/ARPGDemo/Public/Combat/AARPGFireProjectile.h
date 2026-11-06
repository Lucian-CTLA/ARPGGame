// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AARPGFireProjectile.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UGameplayEffect;

/**
 * P 键火球的"子弹"本体。
 * 结构：Sphere（碰撞 + 根）→ Niagara（拖尾特效）→ ProjectileMovement（直线飞行）
 * 命中任何东西就炸：给 AAARPGCharacterBase 结算一次 GE_Damage，播命中特效，然后自毁。
 */
UCLASS()
class ARPGDEMO_API AARPGFireProjectile : public AActor
{
	GENERATED_BODY()

public:
	AARPGFireProjectile();

	// 伤害值；留 0 就用施法者的 AttackPower（和近战一套算法）
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fire")
	float Damage = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fire")
	float LifeSeconds = 4.f;   // 飞 4 秒还没打中就自毁，防止泄漏

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnProjectileHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void Explode(AActor* DirectHit);

	UPROPERTY(VisibleAnywhere, Category = "Fire")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Fire")
	TObjectPtr<UNiagaraComponent> Trail;

	UPROPERTY(VisibleAnywhere, Category = "Fire")
	TObjectPtr<UProjectileMovementComponent> Movement;

	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	TObjectPtr<UNiagaraSystem> ImpactVFX;

	UPROPERTY(EditDefaultsOnly, Category = "Fire")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	bool bExploded = false;   // 命中同一帧可能来两次回调，用它挡掉
};
