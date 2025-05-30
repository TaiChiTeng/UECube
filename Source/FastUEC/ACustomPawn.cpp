#include "ACustomPawn.h"
#include "MagicCubeActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h" // 包含 UGameplayStatics 头文件

ACustomPawn::ACustomPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    // 设置初始位置与根组件
    SetActorLocation(FVector::ZeroVector);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

    // 弹簧臂
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->TargetArmLength = 550.f;
    SpringArm->bUsePawnControlRotation = false;
    SpringArm->SetRelativeLocation(FVector(0.f, 0.f, 50.f));
    SpringArm->SetRelativeRotation(FRotator(0.f, -40.f, 0.f));
    SpringArm->bDoCollisionTest = false;

    // 摄像机
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;

    // 初始状态
    bCameraIsRotating = false;
    MinPitch = -75.f;
    MaxPitch = 75.f;

    // 初始化鼠标拖拽状态
    bIsDraggingCube = false;
    TotalMouseMovement = FVector2D::ZeroVector;
    TotalDragDistance = 0.f;
    DragThreshold = 18.f; // 设置拖动阈值
    bThresholdReached = false; // 初始化阈值是否达到标志
    CurrentRotationAngle = 0.f; // 初始化当前旋转角度

    bIsMagicCubeHit = false; // 初始化是否击中魔方
}

void ACustomPawn::BeginPlay()
{
    Super::BeginPlay();
    SetActorLocation(FVector::ZeroVector);
    // 获取玩家控制器
    PC = Cast<APlayerController>(GetController());
    if (PC)
    {
        PC->bShowMouseCursor = true;
    }
}

void ACustomPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // 右键控制摄像机
    PlayerInputComponent->BindAction("RightMouse", IE_Pressed, this, &ACustomPawn::OnRightMousePressed);
    PlayerInputComponent->BindAction("RightMouse", IE_Released, this, &ACustomPawn::OnRightMouseReleased);

    // 左键拖拽交互：绑定鼠标左键按下与释放
    PlayerInputComponent->BindAction("LeftMouse", IE_Pressed, this, &ACustomPawn::OnLeftMousePressedCube);
    PlayerInputComponent->BindAction("LeftMouse", IE_Released, this, &ACustomPawn::OnLeftMouseReleasedCube);

    PlayerInputComponent->BindAxis("Turn", this, &ACustomPawn::Turn);
    PlayerInputComponent->BindAxis("LookUp", this, &ACustomPawn::LookUp);

    // 添加触摸输入绑定
    PlayerInputComponent->BindTouch(IE_Pressed, this, &ACustomPawn::OnTouchPressed);
    PlayerInputComponent->BindTouch(IE_Released, this, &ACustomPawn::OnTouchReleased);
    PlayerInputComponent->BindTouch(IE_Repeat, this, &ACustomPawn::OnTouchMoved);
}

//////////////////////////////////////////////////////////////////////////
// 摄像机控制
//////////////////////////////////////////////////////////////////////////
void ACustomPawn::OnRightMousePressed() { bCameraIsRotating = true; }
void ACustomPawn::OnRightMouseReleased() { bCameraIsRotating = false; }

void ACustomPawn::Turn(float AxisValue)
{
    if (bCameraIsRotating && FMath::Abs(AxisValue) > KINDA_SMALL_NUMBER)
    {
        FRotator CurrRot = SpringArm->GetComponentRotation();
        CurrRot.Yaw += AxisValue;
        SpringArm->SetWorldRotation(CurrRot);
    }
}

void ACustomPawn::LookUp(float AxisValue)
{
    if (bCameraIsRotating && FMath::Abs(AxisValue) > KINDA_SMALL_NUMBER)
    {
        FRotator CurrRot = SpringArm->GetComponentRotation();
        float NewPitch = FMath::Clamp(CurrRot.Pitch + AxisValue, MinPitch, MaxPitch);
        CurrRot.Pitch = NewPitch;
        SpringArm->SetWorldRotation(CurrRot);
    }
}

// 当鼠标左键按下时：记录屏幕坐标、射线检测选中魔方块，构造候选面集合，并利用 HitNormal 计算射线碰撞平面
void ACustomPawn::OnLeftMousePressedCube()
{
    float MouseX, MouseY;
    if (PC && PC->GetMousePosition(MouseX, MouseY))
    {
        BeginDrag(FVector2D(MouseX, MouseY));
    }
}

// 触摸按下事件处理
void ACustomPawn::OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location)
{
    if (FingerIndex == ETouchIndex::Touch1)
    {
        BeginDrag(FVector2D(Location.X, Location.Y));
    }
}

//
// 在 Tick() 中实时更新拖拽旋转
//
void ACustomPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    tempDeltaTime = DeltaTime;
    // 只有在击中魔方且正在拖拽时才执行
    if (bIsMagicCubeHit && bIsDraggingCube)
    {
        float MouseX, MouseY;
        if (PC && PC->GetMousePosition(MouseX, MouseY))
        {
            UpdateDrag(FVector2D(MouseX, MouseY));
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// 鼠标左键释放时完成最终旋转吸附，并输出详细调试信息，然后结束拖拽
//////////////////////////////////////////////////////////////////////////
void ACustomPawn::OnLeftMouseReleasedCube()
{
    EndDrag();
}

// 触摸移动事件处理
void ACustomPawn::OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location)
{
    if (FingerIndex == ETouchIndex::Touch1 && bIsDraggingCube)
    {
        UpdateDrag(FVector2D(Location.X, Location.Y));
    }
}

// 触摸释放事件处理
void ACustomPawn::OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location)
{
    if (FingerIndex == ETouchIndex::Touch1)
    {
        EndDrag();
    }
}

bool ACustomPawn::DetectMagicCubeHit(const FVector2D& ScreenPosition, AMagicCubeActor*& OutMagicCube, TArray<EMagicCubeFace>& OutCachedFaces, TArray<EMagicCubeFace>& OutCachedTargetFaces)
{
    if (PC)
    {
        // 执行射线检测
        FHitResult HitResult;
        bool bHit = PC->GetHitResultUnderCursorByChannel(
            UEngineTypes::ConvertToTraceType(ECC_Visibility),
            true, // 复杂碰撞检测
            HitResult
        );

        bIsMagicCubeHit = false; // 重置击中魔方标志

        if (bHit && HitResult.GetActor())
        {
            // 尝试获取魔方Actor
            CachedMagicCube = Cast<AMagicCubeActor>(HitResult.GetActor());
            if (CachedMagicCube)
            {
                bIsMagicCubeHit = true; // 设置击中魔方标志

                // 计算点击到的块索引
                const FVector LocalPosition = CachedMagicCube->GetActorTransform().InverseTransformPosition(HitResult.ImpactPoint);
                const int32 x = FMath::RoundToInt((LocalPosition.X + (CachedMagicCube->Dimensions[0] - 1) * 0.5f * CachedMagicCube->BlockSize) / CachedMagicCube->BlockSize);
                const int32 y = FMath::RoundToInt((LocalPosition.Y + (CachedMagicCube->Dimensions[1] - 1) * 0.5f * CachedMagicCube->BlockSize) / CachedMagicCube->BlockSize);
                const int32 z = FMath::RoundToInt((LocalPosition.Z + (CachedMagicCube->Dimensions[2] - 1) * 0.5f * CachedMagicCube->BlockSize) / CachedMagicCube->BlockSize);

                const int32 BlockIndex = CachedMagicCube->GetLinearIndex(x, y, z);

                // 获取归属面集合
                CachedFaces = CachedMagicCube->GetCubeFacesForBlock(x, y, z);

                // 找到射线击中面
                EMagicCubeFace HitFace = EMagicCubeFace::Top; // 默认值
                float MaxDot = -1.0f;
                for (EMagicCubeFace Face : CachedFaces) // 遍历归属面集合
                {
                    FVector FaceNormal = CachedMagicCube->GetFaceNormal(Face);
                    float DotProduct = FVector::DotProduct(FaceNormal, HitResult.ImpactNormal);
                    if (DotProduct > MaxDot)
                    {
                        MaxDot = DotProduct;
                        HitFace = Face;
                    }
                }

                // 找到射线击中面的反面
                EMagicCubeFace OppositeFace = CachedMagicCube->GetOppositeFace(HitFace);

                // 计算目标面集合
                CachedTargetFaces = CachedFaces;
                CachedTargetFaces.Remove(HitFace);
                CachedTargetFaces.Remove(OppositeFace);

                // 设置输出参数
                OutMagicCube = CachedMagicCube;
                OutCachedFaces = CachedFaces;
                OutCachedTargetFaces = CachedTargetFaces;

                return true;
            }
        }
    }
    return false;
}

void ACustomPawn::BeginDrag(const FVector2D& InitialPosition)
{
    bIsDraggingCube = false; // 初始化为false
    InitialMousePosition = InitialPosition;
    TotalMouseMovement = FVector2D::ZeroVector;
    TotalDragDistance = 0.f;
    bThresholdReached = false;
    CurrentRotationAngle = 0.f;

    AMagicCubeActor* HitMagicCube = nullptr;
    TArray<EMagicCubeFace> HitFaces;
    TArray<EMagicCubeFace> HitTargetFaces;

    bIsMagicCubeHit = DetectMagicCubeHit(InitialPosition, HitMagicCube, HitFaces, HitTargetFaces);

    if (bIsMagicCubeHit)
    {
        CachedMagicCube = HitMagicCube;
        CachedFaces = HitFaces;
        CachedTargetFaces = HitTargetFaces;
        bIsDraggingCube = true; // 只有当击中魔方时才设置为true
    }
}

void ACustomPawn::UpdateDrag(const FVector2D& CurrentPosition)
{
    if (!bIsMagicCubeHit || !bIsDraggingCube)
    {
        return;
    }

    // 计算位移增量
    FVector2D MouseDelta = CurrentPosition - InitialMousePosition;
    TotalMouseMovement += MouseDelta;
    TotalDragDistance += MouseDelta.Size();
    InitialMousePosition = CurrentPosition;

    // 检查是否达到阈值
    if (!bThresholdReached && TotalDragDistance >= DragThreshold)
    {
        bThresholdReached = true;

        // 获取鼠标位移向量，不再标准化
        FVector2D MouseMovementVector = TotalMouseMovement;

        // 初始化最大点积绝对值和对应的面
        float MaxDotProductAbs = 0.0f;
        RotationFace = EMagicCubeFace::Top; // 默认值

        // 获取魔方中心的世界坐标
        FVector MagicCubeCenter = CachedMagicCube->GetActorLocation();

        // 将魔方中心的世界坐标转换为屏幕坐标
        FVector2D MagicCubeCenterScreenSpace;
        if (!UGameplayStatics::ProjectWorldToScreen(PC, MagicCubeCenter, MagicCubeCenterScreenSpace, false))
        {
            return;
        }

        // 遍历目标面集合，计算鼠标位移和各目标面元素的点积
        for (EMagicCubeFace Face : CachedTargetFaces)
        {
            // 获取面的旋转方向
            FVector FaceRotateDirection = CachedMagicCube->GetFaceRotateDirection(Face);
            FaceRotateDirection.Normalize(); // 标准化旋转方向

            // 将旋转方向转换为世界坐标系中的一个点
            FVector RotateDirectionWorldSpace = MagicCubeCenter + FaceRotateDirection * 100.0f; // 乘以一个系数，使其远离魔方中心

            // 将世界坐标转换为屏幕坐标
            FVector2D RotateDirectionScreenSpace;

            if (UGameplayStatics::ProjectWorldToScreen(PC, RotateDirectionWorldSpace, RotateDirectionScreenSpace, false))
            {
                // 计算屏幕空间旋转方向向量
                FVector2D ScreenSpaceRotateDirectionVector = RotateDirectionScreenSpace - MagicCubeCenterScreenSpace;
                ScreenSpaceRotateDirectionVector.Normalize(); // 标准化屏幕空间旋转方向

                // 计算点积
                float DotProduct = FVector2D::DotProduct(MouseMovementVector, ScreenSpaceRotateDirectionVector);

                // 使用点积的绝对值
                float DotProductAbs = FMath::Abs(DotProduct);

                // 更新最大点积绝对值和对应的面
                if (DotProductAbs > MaxDotProductAbs)
                {
                    MaxDotProductAbs = DotProductAbs;
                    RotationFace = Face;
                }
            }
        }
    }
    // 达到阈值后处理旋转
    else if (bThresholdReached)
    {
        // 获取旋转轴
        ECubeAxis RotateAxis = CachedMagicCube->GetRotateAxis(RotationFace);

        // 获取旋转轴的向量形式
        FVector RotationAxisVector;
        switch (RotateAxis)
        {
        case ECubeAxis::X:
            RotationAxisVector = CachedMagicCube->GetActorForwardVector();
            break;
        case ECubeAxis::Y:
            RotationAxisVector = CachedMagicCube->GetActorRightVector();
            break;
        case ECubeAxis::Z:
            RotationAxisVector = CachedMagicCube->GetActorUpVector();
            break;
        default:
            RotationAxisVector = FVector::UpVector;
            break;
        }

        // 将2D鼠标位移向量转换为3D世界空间向量
        FVector WorldSpaceMouseDirection = Camera->GetForwardVector() +
                                            Camera->GetRightVector() * MouseDelta.X -
                                            Camera->GetUpVector() * MouseDelta.Y;
        WorldSpaceMouseDirection.Normalize();

        // 计算旋转轴和鼠标位移向量的叉积
        FVector CrossProduct = FVector::CrossProduct(RotationAxisVector, WorldSpaceMouseDirection);
        CrossProduct.Normalize();

        // 计算旋转角度增量（直接使用鼠标位移量）
        float RotationSpeed = 0.5f; // 调整旋转速度
        float AngleDelta = MouseDelta.Size() * RotationSpeed;

        // 旋转角度的符号取决于叉积的方向
        AngleDelta *= (CrossProduct | Camera->GetForwardVector()) > 0 ? 1 : -1;

        // 累加当前旋转角度
        CurrentRotationAngle += AngleDelta;

        // 设置图层旋转
        int32 LayerIndex = CachedMagicCube->GetLayerIndex(RotationFace);
        CachedMagicCube->SetLayerRotation(RotateAxis, LayerIndex, CurrentRotationAngle);
    }
}

void ACustomPawn::EndDrag()
{
    if (bIsDraggingCube && bIsMagicCubeHit && CachedMagicCube)
    {
        // 根据是否达到阈值设置不同的消息和颜色
        if (bThresholdReached)
        {
            // 旋转回弹
            ECubeAxis RotateAxis = CachedMagicCube->GetRotateAxis(RotationFace);
            int32 LayerIndex = CachedMagicCube->GetLayerIndex(RotationFace);

            // 计算最近的 90 度倍数
            float NearestRotation = FMath::RoundToFloat(CurrentRotationAngle / 90.0f) * 90.0f;

            // 计算回弹角度
            float SnapAngle = NearestRotation - CurrentRotationAngle;

            // 执行旋转
            CachedMagicCube->RotateLayer(RotateAxis, LayerIndex, SnapAngle);
        }
        else
        {
            // 重置图层旋转
            ECubeAxis RotateAxis = CachedMagicCube->GetRotateAxis(RotationFace);
            int32 LayerIndex = CachedMagicCube->GetLayerIndex(RotationFace);
            CachedMagicCube->SetLayerRotation(RotateAxis, LayerIndex, 0.f);
        }

        // 重置拖拽状态
        bIsDraggingCube = false;
        TotalMouseMovement = FVector2D::ZeroVector;
        TotalDragDistance = 0.f;
        bThresholdReached = false;
        bIsMagicCubeHit = false;
        CachedMagicCube = nullptr;
        CachedFaces.Empty();
        CachedTargetFaces.Empty();
        CurrentRotationAngle = 0.f;
    }
}