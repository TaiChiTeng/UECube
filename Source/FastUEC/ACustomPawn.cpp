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
        // 执行射线检测
        FHitResult HitResult;
        bool bHit = PC->GetHitResultUnderCursorByChannel(
            UEngineTypes::ConvertToTraceType(ECC_Visibility),
            true, // 复杂碰撞检测
            HitResult
        );

        if (bHit && HitResult.GetActor())
        {
            // 尝试获取魔方Actor
            if (AMagicCubeActor* MagicCube = Cast<AMagicCubeActor>(HitResult.GetActor()))
            {
                // 计算点击到的块索引
                const FVector LocalPosition = MagicCube->GetActorTransform().InverseTransformPosition(HitResult.ImpactPoint);
                const int32 x = FMath::RoundToInt((LocalPosition.X + (MagicCube->Dimensions[0] - 1) * 0.5f * MagicCube->BlockSize) / MagicCube->BlockSize);
                const int32 y = FMath::RoundToInt((LocalPosition.Y + (MagicCube->Dimensions[1] - 1) * 0.5f * MagicCube->BlockSize) / MagicCube->BlockSize);
                const int32 z = FMath::RoundToInt((LocalPosition.Z + (MagicCube->Dimensions[2] - 1) * 0.5f * MagicCube->BlockSize) / MagicCube->BlockSize);

                const int32 BlockIndex = MagicCube->GetLinearIndex(x, y, z);

                // 获取归属面集合
                TArray<EMagicCubeFace> Faces = MagicCube->GetCubeFacesForBlock(x, y, z);

                // 定义面与法向量的映射
                // Teng：这里AI容易错，UE是左手坐标系，X是食指红色（对准屏幕），Y是中指绿色（对准屏幕右方），Z是大拇指蓝色（对准屏幕上方）
                TMap<EMagicCubeFace, FVector> FaceNormals = {
                    {EMagicCubeFace::Top, FVector(0, 0, 1)},
                    {EMagicCubeFace::Bottom, FVector(0, 0, -1)},
                    {EMagicCubeFace::Front, FVector(-1, 0, 0)},
                    {EMagicCubeFace::Back, FVector(1, 0, 0)},
                    {EMagicCubeFace::Left, FVector(0, -1, 0)},
                    {EMagicCubeFace::Right, FVector(0, 1, 0)},
                };

                // 找到射线击中面
                EMagicCubeFace HitFace = EMagicCubeFace::Top; // 默认值
                float MaxDot = -1.0f;
                for (const auto& Pair : FaceNormals)
                {
                    float DotProduct = FVector::DotProduct(Pair.Value, HitResult.ImpactNormal);
                    if (DotProduct > MaxDot)
                    {
                        MaxDot = DotProduct;
                        HitFace = Pair.Key;
                    }
                }

                // 找到射线击中面的反面
                EMagicCubeFace OppositeFace = EMagicCubeFace::Top; // 默认值
                for (const auto& Pair : FaceNormals)
                {
                    float DotProduct = FVector::DotProduct(Pair.Value, -HitResult.ImpactNormal);
                    if (FMath::IsNearlyEqual(DotProduct, 1.0f)) // 完全相反
                    {
                        OppositeFace = Pair.Key;
                        break;
                    }
                }

                // 计算目标面集合
                TArray<EMagicCubeFace> TargetFaces = Faces;
                TargetFaces.Remove(HitFace);
                TargetFaces.Remove(OppositeFace);

                // 构造归属面集合字符串
                FString FaceNames;
                for (EMagicCubeFace Face : Faces)
                {
                    FaceNames += StaticEnum<EMagicCubeFace>()->GetNameStringByValue(static_cast<int64>(Face)) + TEXT(" ");
                }

                // 构造目标面集合字符串
                FString TargetFaceNames;
                for (EMagicCubeFace Face : TargetFaces)
                {
                    TargetFaceNames += StaticEnum<EMagicCubeFace>()->GetNameStringByValue(static_cast<int64>(Face)) + TEXT(" ");
                }

                // 打印信息
                FString Message = FString::Printf(TEXT("点击到魔方块，所属魔方Actor: %s，块索引: %d，归属面集合: %s\n射线击中面: %s\n射线击中面的反面: %s\n目标面集合: %s"),
                    *MagicCube->GetName(), BlockIndex, *FaceNames,
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

}

//////////////////////////////////////////////////////////////////////////
// 鼠标左键释放时完成最终旋转吸附，并输出详细调试信息，然后结束拖拽
//////////////////////////////////////////////////////////////////////////
void ACustomPawn::OnLeftMouseReleasedCube()
{

}