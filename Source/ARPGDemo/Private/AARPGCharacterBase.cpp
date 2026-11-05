// Fill out your copyright notice in the Description page of Project Settings.


#include "AARPGCharacterBase.h"
#include "AbilitySystemComponent.h" 
#include "AttributeSet/UARPGAttributeSet.h" 
#include "AbilitySystemComponent.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Kismet/GameplayStatics.h"   // PlaySoundAtLocation
#include "Characters/AARPGHeroCharacter.h"    




// Sets default values
AAARPGCharacterBase::AAARPGCharacterBase()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

    AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
    AttributeSet = CreateDefaultSubobject<UUARPGAttributeSet>(TEXT("AttributeSet"));

}

void AAARPGCharacterBase::BeginPlay()
{
    Super::BeginPlay();

    if (AbilitySystemComponent && AttributeSet)
    {
        AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
            UUARPGAttributeSet::GetHealthAttribute()
        ).AddUObject(this, &AAARPGCharacterBase::OnHealthChanged);
    }
}

UAbilitySystemComponent* AAARPGCharacterBase::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}

void AAARPGCharacterBase::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);

    AbilitySystemComponent->InitAbilityActorInfo(this, this);
}

void AAARPGCharacterBase::OnHealthChanged(const FOnAttributeChangeData& ChangeData)
{
    if (ChangeData.NewValue <= 0.f)
    {
        HandleDeath();
        return;
    }
    if (ChangeData.NewValue < ChangeData.OldValue)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Hit]%sHP%.0f->%.0f"),
            *GetName(), ChangeData.OldValue, ChangeData.NewValue);
    }
}

void AAARPGCharacterBase::HandleDeath()
{

    if (bIsDead)
    {
        return;
    }
    bIsDead = true;

    // 死亡音：放在 bIsDead 判重之后，保证一次死亡只响一次
    // （血量归零可能被多次 OnHealthChanged 进来，放前面会叠好几层）
    if (DeathSound)
    {
        UGameplayStatics::PlaySoundAtLocation(this, DeathSound, GetActorLocation());
    }

    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->DisableMovement();
        MoveComp->StopMovementImmediately();
    }

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    if (AbilitySystemComponent)
    {
        AbilitySystemComponent->CancelAbilities();
        AbilitySystemComponent->AddLooseGameplayTag(
            FGameplayTag::RequestGameplayTag(FName("State.Dead"))
        );
    }
    if (AAIController* AIController = Cast<AAIController>(GetController()))
    {
        if (UBrainComponent* BrainComp = AIController->GetBrainComponent())
        {
            BrainComp->StopLogic(TEXT("Dead"));
        }

        
    }
    if (DeathMontage)
    {
        if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
        {
            AnimInst->Montage_Play(DeathMontage);
        }
    }
    SetLifeSpan(3.2f);
}







