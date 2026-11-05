#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "ARPGHitCameraShake.generated.h"

UCLASS(Blueprintable)
class ARPGDEMO_API UUARPGHitCameraShake : public UCameraShakeBase
{
    GENERATED_BODY()
public:
    UUARPGHitCameraShake(const FObjectInitializer& ObjectInitializer);
};
