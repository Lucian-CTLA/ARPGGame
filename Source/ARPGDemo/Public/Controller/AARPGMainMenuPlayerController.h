#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "AARPGMainMenuPlayerController.generated.h"

/**
 * 菜单关卡专用 PC：只负责把 WBP_MainMenu 创建出来并显示。
 *
 * 用法：把现有的 BP_MainMenuPC 的 Parent Class 改成这个类即可，
 * BP_MainMenuGameMode / Lvl_MainMenu 里已经配好的引用都不用动。
 */
UCLASS()
class ARPGDEMO_API AAARPGMainMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	// 必须 UPROPERTY 保活：只放局部变量的话 Widget 会被 GC 掉，菜单一闪就没
	UPROPERTY()
	TObjectPtr<UUserWidget> MenuWidget;
};
