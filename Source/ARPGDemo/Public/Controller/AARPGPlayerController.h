// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"  
#include "InputMappingContext.h"  
#include "AARPGPlayerController.generated.h"

/**
 * 
 */
UCLASS()
class ARPGDEMO_API AAARPGPlayerController : public APlayerController  

{
	GENERATED_BODY()
protected:
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	virtual void BeginPlay() override;
};
