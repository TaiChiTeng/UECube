#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

// 前向声明枚举类和魔方Actor类，避免直接包含 MagicCubeActor.h 带来的耦合
enum class ECubeAxis : uint8;
class AMagicCubeActor;

#include "ACustomPawn.generated.h"

UCLASS()
class FASTUEC_API ACustomPawn : public APawn
{
	GENERATED_BODY()

public:
	ACustomPawn();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaTime) override;

	// 鼠标右键控制摄像机
	void OnRightMousePressed();
	void OnRightMouseReleased();
	void Turn(float AxisValue);
	void LookUp(float AxisValue);

	// 鼠标左键拖拽旋转魔方各面各层
	void OnLeftMousePressedCube();
	void OnLeftMouseReleasedCube();

	// 摄像机相关
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	bool bCameraIsRotating;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float MinPitch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float MaxPitch;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	USpringArmComponent* SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	UCameraComponent* Camera;
};