// Fill out your copyright notice in the Description page of Project Settings.
#include "Characters/AARPGHeroCharacter.h"
#include "EnhancedInputComponent.h"  
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"   
#include "AARPGCharacterBase.h"
#include "AttributeSet/UARPGAttributeSet.h"    
#include "Blueprint/UserWidget.h"
#include "UI/UARPGHUDWidget.h"
#include "GameFramework/PlayerController.h"
#include "Characters/AARPGEnemyCharacter.h"



AAARPGHeroCharacter::AAARPGHeroCharacter()
{
	//Cameraboom
CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
CameraBoom->SetupAttachment(GetRootComponent());
CameraBoom->TargetArmLength = 250.f;//SpringArm how long to Actor 
CameraBoom->SocketOffset = FVector(0.f, 50.f, 70.f);//offset to actor
CameraBoom->bUsePawnControlRotation = true;//Hero Control Rotation
CameraBoom->bEnableCameraLag = true;//Camera Lag
CameraBoom->bDoCollisionTest = true;//Collision test
CameraBoom->ProbeSize = 12.f;//ptobesize
//follow camera
FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
FollowCamera->bUsePawnControlRotation = false;//Camera to springArm
FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);//take carema to springarm last
FollowCamera->bUsePawnControlRotation = false;//no with colntrol rotation

bUseControllerRotationYaw = false;//don't with mouse left/right
//Characte movement
GetCharacterMovement()->bOrientRotationToMovement = true;//move to rotation
GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);//rotation rate
GetCharacterMovement()->MaxWalkSpeed = 600.f;//max walk speed
GetCharacterMovement()->AirControl = 0.35f;//on sky control
//Weapon
WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
WeaponMesh->SetupAttachment(GetMesh(), FName("hand_r"));
WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AAARPGHeroCharacter::BeginPlay()
{
	Super::BeginPlay();//father class finish initialization first

	if (AbilitySystemComponent && AttackAbilityClass.Get())
	{
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AttackAbilityClass, 1, INDEX_NONE, this));
		//Parameter two 1: skill level; 
		//parameter three INDEX_NONE: don't bind InputID,
		// because we trigger it directly with Enhanced Input; 
		// parameter four this: source object
	}
	if (AbilitySystemComponent && RollAbilityClass.Get())
	{
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(RollAbilityClass, 1, INDEX_NONE, this));
	}
	if (AbilitySystemComponent && BlockAbilityClass.Get())
	{
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(BlockAbilityClass, 1, INDEX_NONE, this));
	}
	if (HUDWidgetClass && IsLocallyControlled())
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC)
		{
			HUDWidget = CreateWidget<UUARPGHUDWidget>(PC, HUDWidgetClass);
			if (HUDWidget)
			{
				HUDWidget->AddToViewport();
				HUDWidget->UpdateHealth(AttributeSet->GetHealth(), AttributeSet->GetMaxHealth());
				HUDWidget->UpdateStamina(AttributeSet->GetStamina(), AttributeSet->GetMaxStamina());   // 开局先把两条都刷成当前值
			}

		}
	}
	// 绑 Stamina 委托：和基类绑 Health 的写法一字不差，只是回调换成自己的 OnStaminaChanged
	if (AbilitySystemComponent && AttributeSet)
	{
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UUARPGAttributeSet::GetStaminaAttribute()
		).AddUObject(this, &AAARPGHeroCharacter::OnStaminaChanged);
	}

	// 状态文字的驱动源：两个 Tag 谁变了都重新算一遍该显示哪条
	// NewOrRemoved = 只在 Tag 从无到有 / 从有到无时触发一次，不像 AnyCountChange 会重复触发
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->RegisterGameplayTagEvent(
			FGameplayTag::RequestGameplayTag(FName("State.Blocking")),
			EGameplayTagEventType::NewOrRemoved
		).AddUObject(this, &AAARPGHeroCharacter::OnBlockingTagChanged);

		AbilitySystemComponent->RegisterGameplayTagEvent(
			FGameplayTag::RequestGameplayTag(FName("State.Stunned")),
			EGameplayTagEventType::NewOrRemoved
		).AddUObject(this, &AAARPGHeroCharacter::OnStunnedTagChanged);
	}
	if (AbilitySystemComponent && StaminaRegenEffectClass)
	{
		AbilitySystemComponent->ApplyGameplayEffectToSelf(
			StaminaRegenEffectClass.GetDefaultObject(), 1.f,
			AbilitySystemComponent->MakeEffectContext()
		);
	}
	
}

void AAARPGHeroCharacter::OnHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	Super::OnHealthChanged(ChangeData);
	if (HUDWidget)
	{
		HUDWidget->UpdateHealth(ChangeData.NewValue, AttributeSet->GetMaxHealth());
	}
}

void AAARPGHeroCharacter::OnStaminaChanged(const FOnAttributeChangeData& ChangeData)
{
	if (HUDWidget && AttributeSet)
	{
		HUDWidget->UpdateStamina(ChangeData.NewValue, AttributeSet->GetMaxStamina());
		// NewValue 是从属性重读的实际值（不是"扣掉的增量"），直接当当前值用

		RefreshStatusText();   // 体力回满 / 见底都要重算「体力不足」那条要不要显示
	}
}

void AAARPGHeroCharacter::OnBlockingTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	RefreshStatusText();
}

void AAARPGHeroCharacter::OnStunnedTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	RefreshStatusText();
}

void AAARPGHeroCharacter::RefreshStatusText()
{
	if (!HUDWidget || !AbilitySystemComponent || !AttributeSet)
	{
		return;
	}

	// 优先级：破防(2) > 格挡中(1) > 体力不足(3)
	// 破防用 State.Stunned 而不是 State.BlockBroken：BlockBroken 只是个瞬间标记，
	// GA_Block 的 EndAbility 里加完立刻就清掉了（UARPGGameplayAbility_Block.cpp:115），
	// 拿它做显示源的话文字只闪一帧；Stunned 才是真正持续 0.6 秒的那段硬直
	if (AbilitySystemComponent->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Stunned"))))
	{
		HUDWidget->SetStatus(2);
		return;
	}
	if (AbilitySystemComponent->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Blocking"))))
	{
		HUDWidget->SetStatus(1);
		return;
	}

	// 剩下的情况只看体力够不够挡一次
	const bool bOutOfStamina = AttributeSet->GetStamina() < AttributeSet->BlockStaminaCost;
	HUDWidget->SetStatus(bOutOfStamina ? 3 : 0);
}

void AAARPGHeroCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* EIC = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);//input component =>Enhance component
	EIC->BindAction(AttackAction, ETriggerEvent::Started, this, &ThisClass::Input_Attack);
	//Call Input_Attack when the attack button is pressed,Started：按下那一瞬间只触发一次
	EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Input_Move);//bandage MoveActor trigger input_Move
	EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Input_Look);//bandage LookActor trigger input_Look
	EIC->BindAction(RollAction, ETriggerEvent::Started, this, &ThisClass::Input_Roll);

	// 格挡：一个动作绑两个事件（按下举盾 / 松手放下）
	EIC->BindAction(BlockAction, ETriggerEvent::Started, this, &ThisClass::Input_Block_Start);
	EIC->BindAction(BlockAction, ETriggerEvent::Completed, this, &ThisClass::Input_Block_End);

}

void AAARPGHeroCharacter::Input_Move(const FInputActionValue& Value)
{
	// 破防硬直中走不动：State.Stunned 由 GA_Block 破防时挂上，到时间自动撤
	if (AbilitySystemComponent &&
		AbilitySystemComponent->HasMatchingGameplayTag(FGameplayTag::RequestGameplayTag(FName("State.Stunned"))))
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();//value of Vector2d to Axis,and it has X and Y

	const FRotator YawRot(0.f, GetControlRotation().Yaw, 0.f);//control of rotation right/left
	const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);//rotation Vector.X=forward
	const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);//rotation Vector.Y=right

	AddMovementInput(Forward, Axis.Y);//forward add input,value=Axis.Y
	AddMovementInput(Right, Axis.X);//right add input,value=Axis.X
}

void AAARPGHeroCharacter::Input_Look(const FInputActionValue& Value)

{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);//mouse down head up

}

void AAARPGHeroCharacter::Input_Attack(const FInputActionValue& Value)
{
	if (!AbilitySystemComponent)   // 没有技能系统组件就什么都做不了，直接返回
	{
		return;
	}

	FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(AttackAbilityClass);   // 找到这个攻击技能对应的那份 Spec；Spec 里记着它现在是不是在激活中
	const bool bRunning = Spec && Spec->IsActive();   // Spec 找得到并且处于激活状态，才算"这一招正在放"；不能再靠 TryActivateAbilityByClass 的返回值猜，那个值说明不了技能在不在跑

	if (bRunning)   // 正在攻击中：不去重新激活技能，改成发一个事件，让正在跑的那个技能自己去接下一刀
	{
		FGameplayEventData Payload;   // 事件要携带的数据包
		Payload.EventTag = FGameplayTag::RequestGameplayTag(FName("Event.Combo.Next"));   // 事件的标签；必须在 ini 里登记过，否则拿到的是空标签
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, Payload.EventTag, Payload);   // 发给自己的 ASC；正在跑的那个技能里挂着 WaitGameplayEvent，会收到它并触发接招
		return;   // 接招这一步到此结束，不再往下走
	}

	const bool bActivated = AbilitySystemComponent->TryActivateAbilityByClass(AttackAbilityClass);   // 没在攻击中，才正常起手第一刀
	UE_LOG(LogTemp, Warning, TEXT("[C2] TryActivate = %s"), bActivated ? TEXT("OK") : TEXT("BLOCKED"));   // 打日志确认起手有没有成功
}

void AAARPGHeroCharacter::Input_Roll(const FInputActionValue& Value)
{
	if (!AbilitySystemComponent)return;
	FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(RollAbilityClass);
	if (Spec && Spec->IsActive())return;
	AbilitySystemComponent->TryActivateAbilityByClass(RollAbilityClass);
}

void AAARPGHeroCharacter::Input_Block_Start(const FInputActionValue& Value)
{
	if (!AbilitySystemComponent) return;

	FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(BlockAbilityClass);
	if (Spec && Spec->IsActive()) return;   // 已经在举盾，别重复激活

	AbilitySystemComponent->TryActivateAbilityByClass(BlockAbilityClass);
	// 注意：技能激活后不会自己结束（ActivateAbility 里没调 EndAbility），
	// 所以盾会一直举着，直到 Input_Block_End 来取消它
}

void AAARPGHeroCharacter::Input_Block_End(const FInputActionValue& Value)
{
	if (!AbilitySystemComponent) return;

	FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(BlockAbilityClass);
	if (Spec && Spec->IsActive())
	{
		// 取消技能 -> 会走 EndAbility -> 那里撤掉 State.Blocking
		AbilitySystemComponent->CancelAbilityHandle(Spec->Handle);
	}
}

void AAARPGHeroCharacter::HandleDeath()
{
	Super::HandleDeath();   // 基类里已经播 DeathMontage（AARPGCharacterBase.cpp:103）
	SetLifeSpan(0.f);       // 覆盖基类的 SetLifeSpan(3.2f)：玩家尸体不销毁（0 = 取消定时销毁）
	
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC && DeathScreenClass)
	{
		if (UUserWidget* Screen = CreateWidget<UUserWidget>(PC, DeathScreenClass))
		{
			Screen->AddToViewport(100);
			PC->SetInputMode(FInputModeUIOnly());
			PC->bShowMouseCursor = true;
		}
	}

}
