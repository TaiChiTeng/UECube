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
    bIsRotating = false;
    bIsCubeDrag = false;
    MinPitch = -50.f;
    MaxPitch = 45.f;
    
    // 初始化连续拖拽变量
    CurrentTargetFace = TEXT("");
    CurrentRotateAxis = ECubeAxis::X;
    CurrentLayerIndex = 0;
    CurrentDragAngle = 0.f;
    
    // 初始化射线碰撞信息
    SelectedHitPlane = TEXT("未知");
    HitNormal = FVector::ZeroVector;
    
    // 初始化选中魔方指针为空，确保每次交互都从射线检测中获取目标魔方Actor
    SelectedCubeActor = nullptr;
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
void ACustomPawn::OnRightMousePressed() { bIsRotating = true; }
void ACustomPawn::OnRightMouseReleased() { bIsRotating = false; }

void ACustomPawn::Turn(float AxisValue)
{
    if (bIsRotating && FMath::Abs(AxisValue) > KINDA_SMALL_NUMBER)
    {
        FRotator CurrRot = SpringArm->GetComponentRotation();
        CurrRot.Yaw += AxisValue;
        SpringArm->SetWorldRotation(CurrRot);
    }
}

void ACustomPawn::LookUp(float AxisValue)
{
    if (bIsRotating && FMath::Abs(AxisValue) > KINDA_SMALL_NUMBER)
    {
        FRotator CurrRot = SpringArm->GetComponentRotation();
        float NewPitch = FMath::Clamp(CurrRot.Pitch + AxisValue, MinPitch, MaxPitch);
        CurrRot.Pitch = NewPitch;
        SpringArm->SetWorldRotation(CurrRot);
    }
}

//////////////////////////////////////////////////////////////////////////
// 魔方旋转交互 - 连续拖拽（候选面扩展及目标面选择）
//////////////////////////////////////////////////////////////////////////

// 当鼠标左键按下时：记录屏幕坐标、射线检测选中魔方块，构造候选面集合，并利用 HitNormal 计算射线碰撞平面
void ACustomPawn::OnLeftMousePressedCube()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;
    
    float MouseX, MouseY;
    if (!PC->GetMousePosition(MouseX, MouseY)) return;
    CubeDragStartScreen = FVector2D(MouseX, MouseY);
    bIsCubeDrag = true;
    
    // 清空状态
    CurrentTargetFace = TEXT("");
    CurrentRotateAxis = ECubeAxis::X;
    CurrentLayerIndex = 0;
    CurrentDragAngle = 0.f;
    SelectedHitPlane = TEXT("未知");
    HitNormal = FVector::ZeroVector;
    SelectedCubeActor = nullptr;
    
    // 执行射线检测
    FVector WorldOrigin, WorldDirection;
    if (!PC->DeprojectScreenPositionToWorld(MouseX, MouseY, WorldOrigin, WorldDirection)) return;
    FVector TraceEnd = WorldOrigin + WorldDirection * 10000.f;
    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);
    
    if (GetWorld()->LineTraceSingleByChannel(HitResult, WorldOrigin, TraceEnd, ECC_Visibility, QueryParams))
    {
        // 记录命中法向量
        HitNormal = HitResult.Normal;
        
        // 直接检查射线检测命中的组件是否是目标魔方的 InstancedMesh
        UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(HitResult.GetComponent());
        if (ISMC && HitResult.Item != INDEX_NONE)
        {
            // 动态获取被射线击中的魔方Actor
            AMagicCubeActor* HitCubeActor = Cast<AMagicCubeActor>(HitResult.GetActor());
            if (HitCubeActor && ISMC == HitCubeActor->InstancedMesh)
            {
                SelectedCubeActor = HitCubeActor;
                SelectedCubeInstanceIndex = HitResult.Item;
                FTransform InstTrans;
                ISMC->GetInstanceTransform(SelectedCubeInstanceIndex, InstTrans, true);
                SelectedBlockLocalPos = InstTrans.GetLocation();
                
                // 根据选中块的 index 与魔方尺寸生成候选面
                const TArray<int32>& CubeDims = SelectedCubeActor->Dimensions;
                if (CubeDims.Num() >= 3)
                {
                    int32 dimX = CubeDims[0], dimY = CubeDims[1], dimZ = CubeDims[2];
                    int32 index = SelectedCubeInstanceIndex;
                    int32 x = index % dimX;
                    int32 y = (index / dimX) % dimY;
                    int32 z = index / (dimX * dimY);
                    
                    SelectedCandidateFaces.Empty();
                    // X 轴
                    if (x == 0)
                        SelectedCandidateFaces.Add(TEXT("前面"));
                    else if (x == dimX - 1)
                        SelectedCandidateFaces.Add(TEXT("后面"));
                    else
                        SelectedCandidateFaces.Add(TEXT("非前非后面"));
                    
                    // Y 轴
                    if (y == 0)
                        SelectedCandidateFaces.Add(TEXT("左面"));
                    else if (y == dimY - 1)
                        SelectedCandidateFaces.Add(TEXT("右面"));
                    else
                        SelectedCandidateFaces.Add(TEXT("非左非右面"));
                    
                    // Z 轴
                    if (z == 0)
                        SelectedCandidateFaces.Add(TEXT("下面"));
                    else if (z == dimZ - 1)
                        SelectedCandidateFaces.Add(TEXT("上面"));
                    else
                        SelectedCandidateFaces.Add(TEXT("非上非下面"));
                }
                
                // 利用 HitNormal 和魔方六个标准面法向量计算射线碰撞平面
                struct FFaceInfo { FString Label; FVector Normal; };
                TArray<FFaceInfo> FaceInfos;
                FaceInfos.Add({ TEXT("上面"), FVector(0.f, 0.f, 1.f) });
                FaceInfos.Add({ TEXT("下面"), FVector(0.f, 0.f, -1.f) });
                FaceInfos.Add({ TEXT("左面"), FVector(0.f, -1.f, 0.f) });
                FaceInfos.Add({ TEXT("右面"), FVector(0.f, 1.f, 0.f) });
                FaceInfos.Add({ TEXT("前面"), FVector(-1.f, 0.f, 0.f) });
                FaceInfos.Add({ TEXT("后面"), FVector(1.f, 0.f, 0.f) });
                
                float BestDot = -1.f;
                FString ComputedHitPlane = TEXT("未知");
                for (const FFaceInfo& Info : FaceInfos)
                {
                    float Dot = FVector::DotProduct(HitNormal, Info.Normal);
                    if (Dot > BestDot)
                    {
                        BestDot = Dot;
                        ComputedHitPlane = Info.Label;
                    }
                }
                SelectedHitPlane = ComputedHitPlane;
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
    
    if (bIsCubeDrag && SelectedCubeActor)
    {
        APlayerController* PC = Cast<APlayerController>(GetController());
        if (!PC) return;
        float MouseX, MouseY;
        if (!PC->GetMousePosition(MouseX, MouseY))
            return;
        FVector2D CurrentMousePos(MouseX, MouseY);
        FVector2D DragDelta = CurrentMousePos - CubeDragStartScreen;
        const float DragThresh = 10.f;
        if (DragDelta.Size() < DragThresh)
            return;
        
        // 拖拽初期确定目标面：剔除与 SelectedHitPlane 相同的候选
        if (CurrentTargetFace.IsEmpty())
        {
            TArray<FString> FilteredCandidates;
            for (const FString& Lab : SelectedCandidateFaces)
            {
                if (Lab != SelectedHitPlane)
                    FilteredCandidates.Add(Lab);
            }
            const TArray<FString>& UseCandidates = (FilteredCandidates.Num() > 0 ? FilteredCandidates : SelectedCandidateFaces);
            
            struct FCandidate { FString Label; FVector2D Ideal; };
            TArray<FCandidate> CandArr;
            for (const FString& Lab : UseCandidates)
            {
                FCandidate Candidate;
                Candidate.Label = Lab;
                if (Lab == TEXT("上面"))
                    Candidate.Ideal = FVector2D(-1, 0);
                else if (Lab == TEXT("下面"))
                    Candidate.Ideal = FVector2D(1, 0);
                else if (Lab == TEXT("左面"))
                    Candidate.Ideal = FVector2D(0, -1);
                else if (Lab == TEXT("右面"))
                    Candidate.Ideal = FVector2D(0, -1);
                else if (Lab == TEXT("前面"))
                    Candidate.Ideal = (SelectedHitPlane == TEXT("左面")) ? FVector2D(0, 1) : FVector2D(0, -1);
                else if (Lab == TEXT("后面"))
                    Candidate.Ideal = (SelectedHitPlane == TEXT("后面")) ? FVector2D(0, 1) : FVector2D(0, -1);
                else if (Lab == TEXT("非上非下面"))
                    Candidate.Ideal = (SelectedHitPlane == TEXT("左面")) ? FVector2D(0, -1) : FVector2D(1, 0);
                else if (Lab == TEXT("非左非右面"))
                    Candidate.Ideal = (SelectedHitPlane == TEXT("后面")) ? FVector2D(1, 0) : FVector2D(0, 1);
                else if (Lab == TEXT("非前非后面"))
                {
                    if (SelectedHitPlane == TEXT("左面") || SelectedHitPlane == TEXT("右面"))
                        Candidate.Ideal = FVector2D(0, 1);
                    else
                        Candidate.Ideal = FVector2D(1, 0);
                }
                else
                    Candidate.Ideal = FVector2D(0, 0);
                CandArr.Add(Candidate);
            }
            
            FVector2D DragDir = DragDelta.GetSafeNormal();
            float BestDot = -2.f;
            FString TargetFace;
            for (const FCandidate& Candidate : CandArr)
            {
                float Dot = FVector2D::DotProduct(DragDir, Candidate.Ideal);
                if (FMath::Abs(Dot) > BestDot)
                {
                    BestDot = FMath::Abs(Dot);
                    TargetFace = Candidate.Label;
                }
            }
            if (TargetFace.IsEmpty() && SelectedCandidateFaces.Num() > 0)
                TargetFace = SelectedCandidateFaces[0];
            CurrentTargetFace = TargetFace;
        }
        
        // 根据目标面确定旋转轴和层索引
        FString TargetFace = CurrentTargetFace;
        ECubeAxis RotateAxis = ECubeAxis::X;
        int32 LayerIndex = 0;
        if (TargetFace == TEXT("上面"))
        {
            RotateAxis = ECubeAxis::Z;
            LayerIndex = SelectedCubeActor->Dimensions[2] - 1;
        }
        else if (TargetFace == TEXT("下面"))
        {
            RotateAxis = ECubeAxis::Z;
            LayerIndex = 0;
        }
        else if (TargetFace == TEXT("左面"))
        {
            RotateAxis = ECubeAxis::Y;
            LayerIndex = 0;
        }
        else if (TargetFace == TEXT("右面"))
        {
            RotateAxis = ECubeAxis::Y;
            LayerIndex = SelectedCubeActor->Dimensions[1] - 1;
        }
        else if (TargetFace == TEXT("前面"))
        {
            RotateAxis = ECubeAxis::X;
            LayerIndex = 0;
        }
        else if (TargetFace == TEXT("后面"))
        {
            RotateAxis = ECubeAxis::X;
            LayerIndex = SelectedCubeActor->Dimensions[0] - 1;
        }
        else if (TargetFace == TEXT("非左非右面"))
        {
            RotateAxis = ECubeAxis::Y;
            LayerIndex = 1;
        }
        else if (TargetFace == TEXT("非上非下面"))
        {
            RotateAxis = ECubeAxis::Z;
            LayerIndex = 1;
        }
        else if (TargetFace == TEXT("非前非后面"))
        {
            RotateAxis = ECubeAxis::X;
            LayerIndex = 1;
        }
        
        // 在拖拽初期仅调用一次 BeginLayerRotation
        static bool bDragInitiated = false;
        if (!bDragInitiated)
        {
            SelectedCubeActor->BeginLayerRotation(RotateAxis, LayerIndex);
            bDragInitiated = true;
        }
        
        // 根据拖拽分量计算旋转角度
        float RawAngle = 0.f;
        float DragSensitivity = 150.f;
        if (TargetFace == TEXT("上面") || TargetFace == TEXT("下面") || TargetFace == TEXT("非上非下面"))
        {
            RawAngle = -(DragDelta.X / DragSensitivity) * 90.f;
        }
        else if (TargetFace == TEXT("左面") || TargetFace == TEXT("右面") ||
                 TargetFace == TEXT("前面") || TargetFace == TEXT("后面") ||
                 TargetFace == TEXT("非左非右面") || TargetFace == TEXT("非前非后面"))
        {
            if (TargetFace == TEXT("非前非后面") && SelectedHitPlane == TEXT("左面"))
            {
                RawAngle = (DragDelta.Y / DragSensitivity) * 90.f;
            }
            else if (SelectedHitPlane == TEXT("左面") ||
                     (SelectedHitPlane == TEXT("后面") && (TargetFace == TEXT("左面") || TargetFace == TEXT("右面"))))
            {
                RawAngle = (DragDelta.Y / DragSensitivity) * 90.f;
            }
            else
            {
                RawAngle = -(DragDelta.Y / DragSensitivity) * 90.f;
            }
        }
        float ContinuousAngle = FMath::Clamp(RawAngle, -90.f, 90.f);
        
        CurrentRotateAxis = RotateAxis;
        CurrentLayerIndex = LayerIndex;
        CurrentDragAngle = ContinuousAngle;
        
        SelectedCubeActor->SetLayerRotation(RotateAxis, LayerIndex, ContinuousAngle);
    }
}

//////////////////////////////////////////////////////////////////////////
// 鼠标左键释放时完成最终旋转吸附，并输出详细调试信息，然后结束拖拽
//////////////////////////////////////////////////////////////////////////
void ACustomPawn::OnLeftMouseReleasedCube()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;
    bIsCubeDrag = false;
    
    float MouseX, MouseY;
    if (!PC->GetMousePosition(MouseX, MouseY))
        return;
    FVector2D ReleasePos(MouseX, MouseY);
    FVector2D DragDelta = ReleasePos - CubeDragStartScreen;
    float DragDist = DragDelta.Size();
    const float DragThresh = 10.f;
    
    if (!SelectedCubeActor) return;
    
    // 如果拖拽距离太短，则恢复旋转为 0 度
    if (DragDist < DragThresh)
    {
        SelectedCubeActor->SetLayerRotation(CurrentRotateAxis, CurrentLayerIndex, 0.f);
        if (GEngine)
            GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("点击，但未产生有效拖拽。"));
        {
            static bool bDragInitiated = false;
            bDragInitiated = false;
        }
        SelectedCubeActor->EndLayerRotationDrag();
        return;
    }
    
    // 最终吸附：拖拽角度绝对值大于45，则吸附至 ±90°，否则归零
    float FinalAngle = (FMath::Abs(CurrentDragAngle) > 45.f) ? ((CurrentDragAngle > 0) ? 90.f : -90.f) : 0.f;
    
    float AngleDelta = FinalAngle - CurrentDragAngle;
    if (FMath::Abs(AngleDelta) > KINDA_SMALL_NUMBER)
    {
        SelectedCubeActor->RotateLayer(CurrentRotateAxis, CurrentLayerIndex, AngleDelta);
    }
    else
    {
        SelectedCubeActor->SetLayerRotation(CurrentRotateAxis, CurrentLayerIndex, FinalAngle);
    }
    
    FString CandidateStr;
    for (const FString& s : SelectedCandidateFaces)
    {
        CandidateStr += s + TEXT(" ");
    }
    
    if (GEngine)
    {
        FString DebugMsg = FString::Printf(
            TEXT("旋转结束:\n鼠标屏幕位移: (%.1f, %.1f)\n选中块 index: %d\n射线碰撞平面: %s\n拖拽位移: (%.1f, %.1f)\n候选面集合: %s\n目标面: %s\n最终角度: %.1f"),
            ReleasePos.X - CubeDragStartScreen.X, ReleasePos.Y - CubeDragStartScreen.Y,
            SelectedCubeInstanceIndex,
            *SelectedHitPlane,
            DragDelta.X, DragDelta.Y,
            *CandidateStr,
            *CurrentTargetFace,
            FinalAngle
        );
        GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, DebugMsg);
    }
    
    {
        static bool bDragInitiated = false;
        bDragInitiated = false;
    }
    SelectedCubeActor->EndLayerRotationDrag();
}