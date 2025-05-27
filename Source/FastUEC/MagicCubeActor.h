#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Math/Vector2D.h" // 引入 FVector2D 的头文件

// 前向声明枚举类和魔方Actor类，避免直接包含 MagicCubeActor.h 带来的耦合
enum class ECubeAxis : uint8;
enum class EMagicCubeFace : uint8; // 添加 EMagicCubeFace 前向声明
class AMagicCubeActor;

#include "ACustomPawn.generated.h"

UCLASS()
class FASTUEC_API ACustomPawn : public APawn
{
    GENERATED_BODY()

public:
    // 构造函数
    ACustomPawn();

protected:
    // BeginPlay 函数
    virtual void BeginPlay() override;

    // 设置输入组件
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    // Tick 函数
    virtual void Tick(float DeltaTime) override;

private:
    // 鼠标右键控制摄像机
    void OnRightMousePressed();
    void OnRightMouseReleased();
    void Turn(float AxisValue);
    void LookUp(float AxisValue);

    // 鼠标左键拖拽旋转魔方各面各层
    void OnLeftMousePressedCube();
    void OnLeftMouseReleasedCube();

private:
    // 鼠标拖拽状态
    bool bIsDraggingCube;
    FVector2D TotalMouseMovement;
    float TotalDragDistance;
    FVector2D InitialMousePosition; // 记录初始鼠标位置
    float CurrentRotationAngle; // 当前旋转的角度

    // 拖动阈值
    float DragThreshold;
    bool bThresholdReached;

    // 是否击中魔方
    bool bIsMagicCubeHit;

    // 缓存击中的魔方 Actor
    AMagicCubeActor* CachedMagicCube;

    // 缓存击中的魔方块所属的面集合
    TArray<EMagicCubeFace> CachedFaces;

    // 缓存击中的魔方块目标面集合 (用于计算旋转方向)
    EMagicCubeFace RotationFace; // 确定旋转的面
    TArray<EMagicCubeFace> CachedTargetFaces;

public:
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