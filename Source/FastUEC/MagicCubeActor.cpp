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
    DragThreshold = 20.f; // 设置拖动阈值
    bThresholdReached = false; // 初始化阈值是否达到标志
    CurrentRotationAngle = 0.f; // 初始化当前旋转角度

    bIsMagicCubeHit = false; // 初始化是否击中魔方
}

void ACustomPawn::BeginPlay()
{
    Super::BeginPlay();
    SetActorLocation(FVector::ZeroVector);
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
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
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        // 初始化鼠标拖拽状态
        bIsDraggingCube = true;
        TotalMouseMovement = FVector2D::ZeroVector;
        TotalDragDistance = 0.f;
        bThresholdReached = false; // 重置阈值达到标志
        CurrentRotationAngle = 0.f; // 重置当前旋转角度

        // 获取初始鼠标位置
        PC->GetMousePosition(InitialMousePosition.X, InitialMousePosition.Y);

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

                // 构造归属面集合字符串
                FString FaceNames;
                for (EMagicCubeFace Face : CachedFaces)
                {
                    FaceNames += StaticEnum<EMagicCubeFace>()->GetNameStringByValue(static_cast<int64>(Face)) + TEXT(" ");
                }

                // 构造目标面集合字符串
                FString TargetFaceNames;
                for (EMagicCubeFace Face : CachedTargetFaces)
                {
                    TargetFaceNames += StaticEnum<EMagicCubeFace>()->GetNameStringByValue(static_cast<int64>(Face)) + TEXT(" ");
                }

                // 打印信息
                FString Message = FString::Printf(TEXT("点击到魔方块，所属魔方Actor: %s，块索引: %d，归属面集合: %s\n射线击中面: %s\n射线击中面的反面: %s\n目标面集合: %s"),
                    *CachedMagicCube->GetName(), BlockIndex, *FaceNames,
                    *StaticEnum<EMagicCubeFace>()->GetNameStringByValue(static_cast<int64>(HitFace)),
                    *StaticEnum<EMagicCubeFace>()->GetNameStringByValue(static_cast<int64>(OppositeFace)),
                    *TargetFaceNames);
                GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, Message);
                UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
            }
        }
    }
}

//
// 在 Tick() 中实时更新拖拽旋转
//
void ACustomPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 只有在击中魔方且正在拖拽时才执行
    if (bIsMagicCubeHit && bIsDraggingCube)
    {
        APlayerController* PC = Cast<APlayerController>(GetController()); // 将PC的定义放在这里
        if (PC && CachedMagicCube && Camera)
        {
            // 获取当前鼠标位置
            float CurrentMouseX, CurrentMouseY;
            PC->GetMousePosition(CurrentMouseX, CurrentMouseY);

            // 计算鼠标位移
            FVector2D MouseDelta = FVector2D(CurrentMouseX, CurrentMouseY) - InitialMousePosition;

            // 累加位移
            TotalMouseMovement += MouseDelta;

            // 计算距离
            float DistanceThisFrame = MouseDelta.Size();
            TotalDragDistance += DistanceThisFrame;

            // 更新初始鼠标位置
            InitialMousePosition = FVector2D(CurrentMouseX, CurrentMouseY);

            // 检查是否达到阈值
            if (!bThresholdReached && TotalDragDistance >= DragThreshold)
            {
                bThresholdReached = true;
                FString Message = FString::Printf(TEXT("鼠标拖动已经达到检测阈值"));
                GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, Message);
                UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);

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
                    UE_LOG(LogTemp, Warning, TEXT("魔方中心世界坐标到屏幕坐标转换失败！"));
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

                    // **关键修改：确保使用当前摄像机信息进行投影**
                    if (UGameplayStatics::ProjectWorldToScreen(PC, RotateDirectionWorldSpace, RotateDirectionScreenSpace, false))
                    {
                        // 计算屏幕空间旋转方向向量
                        FVector2D ScreenSpaceRotateDirectionVector = RotateDirectionScreenSpace - MagicCubeCenterScreenSpace;
                        ScreenSpaceRotateDirectionVector.Normalize(); // 标准化屏幕空间旋转方向

                        // 计算点积
                        float DotProduct = FVector2D::DotProduct(MouseMovementVector, ScreenSpaceRotateDirectionVector);

                        // 使用点积的绝对值
                        float DotProductAbs = FMath::Abs(DotProduct);

                        // 打印当前面的点积绝对值
                        FString FaceName = StaticEnum<EMagicCubeFace>()->GetNameStringByValue(static_cast<int64>(Face));
                        FString DotProductMessage = FString::Printf(TEXT("  面: %s, 点积绝对值: %.2f"), *FaceName, DotProductAbs);
                        GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Blue, DotProductMessage);
                        UE_LOG(LogTemp, Warning, TEXT("%s"), *DotProductMessage);

                        // 更新最大点积绝对值和对应的面
                        if (DotProductAbs > MaxDotProductAbs)
                        {
                            MaxDotProductAbs = DotProductAbs;
                            RotationFace = Face;
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning, TEXT("世界坐标到屏幕坐标转换失败！"));
                    }
                }

                // 打印旋转面
                FString RotationFaceName = StaticEnum<EMagicCubeFace>()->GetNameStringByValue(static_cast<int64>(RotationFace));
                FString RotationFaceMessage = FString::Printf(TEXT("旋转面: %s, 最大点积绝对值: %.2f"), *RotationFaceName, MaxDotProductAbs);
                GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Purple, RotationFaceMessage);
                UE_LOG(LogTemp, Warning, TEXT("%s"), *RotationFaceMessage);
            }
            else
            {
                // 打印调试信息（可选，但建议保留）
                FString Message = FString::Printf(TEXT("鼠标位移: X=%.2f, Y=%.2f, 累计距离: %.2f"), MouseDelta.X, MouseDelta.Y, TotalDragDistance);
                GEngine->AddOnScreenDebugMessage(-1, DeltaTime, FColor::Yellow, Message);

                // 判断旋转方向并实时旋转
                if (bThresholdReached)
                {
                    // 获取旋转轴
                    ECubeAxis RotateAxis = CachedMagicCube->GetRotateAxis(RotationFace);

                    // 获取旋转轴的向量形式
                    FVector RotationAxisVector;
                    switch (RotateAxis)
                    {
                    case ECubeAxis::X:
                        RotationAxisVector = CachedMagicCube->GetActorRightVector();
                        break;
                    case ECubeAxis::Y:
                        RotationAxisVector = CachedMagicCube->GetActorForwardVector();
                        break;
                    case ECubeAxis::Z:
                        RotationAxisVector = CachedMagicCube->GetActorUpVector();
                        break;
                    default:
                        RotationAxisVector = FVector::UpVector;
                        break;
                    }

                    // 获取鼠标位移向量，不再标准化
                    FVector2D MouseMovementVector = TotalMouseMovement;

                    // 将2D鼠标位移向量转换为3D世界空间向量
                    FVector WorldSpaceMouseDirection = Camera->GetForwardVector() + Camera->GetRightVector() * MouseMovementVector.X + Camera->GetUpVector() * MouseMovementVector.Y;
                    WorldSpaceMouseDirection.Normalize();

                    // 计算旋转轴和鼠标位移向量的叉积
                    FVector CrossProduct = FVector::CrossProduct(RotationAxisVector, WorldSpaceMouseDirection);
                    CrossProduct.Normalize();

                    // 计算旋转角度（使用点积）
                    float DotProduct = FVector::DotProduct(RotationAxisVector, WorldSpaceMouseDirection);
                    float Angle = FMath::Acos(DotProduct);

                    // 旋转角度的符号取决于叉积的方向
                    float RotationSpeed = 0.1f; //调整旋转速度
                    float AngleDelta = Angle * RotationSpeed * (CrossProduct | Camera->GetForwardVector()) > 0 ? 1 : -1;

                    // 累加旋转角度
                    CurrentRotationAngle += AngleDelta * DistanceThisFrame;

                    // 设置图层旋转
                    int32 LayerIndex = CachedMagicCube->GetLayerIndex(RotationFace);
                    CachedMagicCube->SetLayerRotation(RotateAxis, LayerIndex, CurrentRotationAngle);
                }
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// 鼠标左键释放时完成最终旋转吸附，并输出详细调试信息，然后结束拖拽
//////////////////////////////////////////////////////////////////////////
void ACustomPawn::OnLeftMouseReleasedCube()
{
    if (bIsDraggingCube && bIsMagicCubeHit && CachedMagicCube)
    {
        FString Message;
        FColor TextColor;

        // 根据是否达到阈值设置不同的消息和颜色
        if (bThresholdReached)
        {
            Message = FString::Printf(TEXT("拖拽结束，累计鼠标位移: X=%.2f, Y=%.2f, 累计距离: %.2f"), TotalMouseMovement.X, TotalMouseMovement.Y, TotalDragDistance);
            TextColor = FColor::Cyan;

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
            Message = FString::Printf(TEXT("拖动距离过小不触发魔方旋转交互，累计鼠标位移: X=%.2f, Y=%.2f, 累计距离: %.2f"), TotalMouseMovement.X, TotalMouseMovement.Y, TotalDragDistance);
            TextColor = FColor::Orange;

            // 重置图层旋转
            ECubeAxis RotateAxis = CachedMagicCube->GetRotateAxis(RotationFace);
            int32 LayerIndex = CachedMagicCube->GetLayerIndex(RotationFace);
            CachedMagicCube->SetLayerRotation(RotateAxis, LayerIndex, 0.f);
        }

        // 打印最终的累计位移和距离
        GEngine->AddOnScreenDebugMessage(-1, 5.f, TextColor, Message);
        UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);

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
