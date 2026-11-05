#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "ARPGSineCameraShakePattern.generated.h"


UCLASS(Blueprintable, EditInlineNew)
class ARPGDEMO_API UUARPGSineCameraShakePattern : public UCameraShakePattern
{
    GENERATED_BODY()
public:
    UUARPGSineCameraShakePattern(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(EditAnywhere, Category = "Shake") float Amplitude = 3.f;   // 位移幅度（厘米）
    UPROPERTY(EditAnywhere, Category = "Shake") float Frequency = 25.f; // 抖动快慢
    UPROPERTY(EditAnywhere, Category = "Shake") float Duration = 0.25f;

private:
    float Elapsed = 0.f;

protected:
    virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override
    {
        OutInfo.Duration = FCameraShakeDuration(Duration);   // 必须是 Fixed 类型，否则引擎不知道什么时候停
    }
    virtual void StartShakePatternImpl(const FCameraShakePatternStartParams&) override
    {
        Elapsed = 0.f;
    }
    virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,
        FCameraShakePatternUpdateResult& OutResult) override
    {
        Elapsed += Params.DeltaTime;
        const float Decay = 1.f - FMath::Clamp(Elapsed / Duration, 0.f, 1.f);
        const float T = Elapsed * Frequency;
        OutResult.Location = FVector(
            FMath::Sin(T) * Amplitude * Decay,
            FMath::Sin(T * 1.31f) * Amplitude * Decay,
            FMath::Sin(T * 0.77f) * Amplitude * 0.5f * Decay);
        OutResult.Rotation = FRotator(
            FMath::Sin(T * 0.9f) * 0.6f * Decay,
            FMath::Sin(T * 1.4f) * 0.6f * Decay,
            0.f);
    }
    virtual bool IsFinishedImpl() const override
    {
        return Elapsed >= Duration;   // 基类默认返回 true，不覆写 = 震一下就停
    }
};
