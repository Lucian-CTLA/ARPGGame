// Fill out your copyright notice in the Description page of Project Settings.

#include "Carema/ARPGHitCameraShake.h"
#include "Carema/ARPGSineCameraShakePattern.h"

UUARPGHitCameraShake::UUARPGHitCameraShake(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // 引擎原话（CameraShakeBase.cpp:30）：默认子对象必须用 CreateDefaultSubobject 带名字创建。
    // 不能在构造函数里用 NewObject 且名字为空 —— UObjectGlobals.cpp:4998 会直接 Fatal。
    // ChangeRootShakePattern<T>() 内部就是无名字的 NewObject，所以在构造函数里会崩。
    UUARPGSineCameraShakePattern* SinePattern =
        ObjectInitializer.CreateDefaultSubobject<UUARPGSineCameraShakePattern>(
            this, TEXT("SineShakePattern"));

    SetRootShakePattern(SinePattern);
}
