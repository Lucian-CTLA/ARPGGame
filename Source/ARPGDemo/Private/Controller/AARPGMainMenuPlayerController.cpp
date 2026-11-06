#include "Controller/AARPGMainMenuPlayerController.h"
#include "Blueprint/UserWidget.h"

void AAARPGMainMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 按路径加载 WBP，Class Defaults 里不用再指定 Widget Class
	UClass* WidgetClass = LoadClass<UUserWidget>(nullptr,
		TEXT("/Game/Blueprint/Widget/WBP_MainMenu.WBP_MainMenu_C"));
	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[MainMenu] 加载 WBP_MainMenu_C 失败，检查资产路径"));
		return;
	}

	// CreateWidget 的 Owner 必须是 APlayerController，这里就是 this
	MenuWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (!MenuWidget) return;

	MenuWidget->AddToViewport(10);

	// 菜单要能点：UI Only + 显示鼠标
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MenuWidget->TakeWidget());
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}
