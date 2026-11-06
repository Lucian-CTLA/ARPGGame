// Fill out your copyright notice in the Description page of Project Settings.

#include "Combat/AARPGFireProjectile.h"

#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"

#include "AARPGCharacterBase.h"
#include "AttributeSet/UARPGAttributeSet.h"
#include "UObject/ConstructorHelpers.h"

AARPGFireProjectile::AARPGFireProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	// 碰撞球当根：Block 一切，靠 Hit 事件结算伤害（Pawn 的胶囊对 WorldDynamic 是 Block）
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(16.f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetNotifyRigidBodyCollision(true);
	SetRootComponent(Collision);

	// 拖尾特效：跟着 Actor 走，所以直接挂根上
	Trail = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Trail"));
	Trail->SetupAttachment(Collision);
	Trail->bAutoActivate = true;

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> TrailAsset(
		TEXT("/Game/Assets/Niagara/Projectiles/NS_FireyProjectile.NS_FireyProjectile"));
	if (TrailAsset.Succeeded())
	{
		Trail->SetAsset(TrailAsset.Object);
	}

	// 命中特效：蓝图里可以覆盖，C++ 先给个默认值，省得每次都要手配
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ImpactAsset(
		TEXT("/Game/Assets/Niagara/Projectiles/Impact/NS_FireyImpact.NS_FireyImpact"));
	if (ImpactAsset.Succeeded())
	{
		ImpactVFX = ImpactAsset.Object;
	}

	// GE_Damage：和近战共用同一个伤害 GE（里面读 SetByCaller.Damage）
	static ConstructorHelpers::FClassFinder<UGameplayEffect> DamageGE(
		TEXT("/Game/Blueprint/Ability/GE_Damage.GE_Damage_C"));
	if (DamageGE.Succeeded())
	{
		DamageEffectClass = DamageGE.Class;
	}

	// 直线飞行，不受重力：改成抛物线只要把 GravityScale 调大
	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->InitialSpeed = 2400.f;
	Movement->MaxSpeed = 2400.f;
	Movement->bRotationFollowsVelocity = true;
	Movement->ProjectileGravityScale = 0.f;
}

void AARPGFireProjectile::BeginPlay()
{
	Super::BeginPlay();

	// 别一出生就撞到施法者自己
	if (AActor* Shooter = GetOwner())
	{
		Collision->IgnoreActorWhenMoving(Shooter, true);
	}

	Collision->OnComponentHit.AddDynamic(this, &AARPGFireProjectile::OnProjectileHit);

	SetLifeSpan(LifeSeconds);
}

void AARPGFireProjectile::OnProjectileHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bExploded)
	{
		return;
	}

	// 打到自己不算
	if (OtherActor && OtherActor != this && OtherActor != GetOwner())
	{
		Explode(OtherActor);
	}
}

void AARPGFireProjectile::Explode(AActor* DirectHit)
{
	bExploded = true;

	// ---- 伤害：和近战 MeleeHit 完全同一套（GE_Damage + SetByCaller.Damage）----
	AAARPGCharacterBase* Target = Cast<AAARPGCharacterBase>(DirectHit);
	AAARPGCharacterBase* Shooter = Cast<AAARPGCharacterBase>(GetOwner());

	if (Target && Shooter)
	{
		UAbilitySystemComponent* SourceASC = Shooter->GetAbilitySystemComponent();
		UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();

		if (SourceASC && TargetASC && DamageEffectClass)
		{
			// 伤害值：写了 Damage 就用它，没写就取施法者攻击力（近战的算法）
			float DamageValue = Damage;
			if (DamageValue <= 0.f)
			{
				bool bFound = false;
				const float AttackPower = UAbilitySystemBlueprintLibrary::GetFloatAttributeFromAbilitySystemComponent(
					SourceASC, UUARPGAttributeSet::GetAttackPowerAttribute(), bFound);
				DamageValue = bFound ? AttackPower : 20.f;
			}

			FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
			Context.AddSourceObject(this);

			FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(DamageEffectClass, 1.f, Context);
			if (Spec.IsValid())
			{
				// 必须是 Data Tag 版：SetByCaller.Damage（用 FName 版会静默变成 0 伤害）
				Spec.Data->SetSetByCallerMagnitude(
					FGameplayTag::RequestGameplayTag(FName("SetByCaller.Damage")),
					DamageValue
				);
				SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data, TargetASC);
			}
		}
	}

	// ---- 命中特效 ----
	if (ImpactVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, ImpactVFX, GetActorLocation(), GetActorRotation(),
			FVector(1.f), true, true);
	}

	Destroy();
}
