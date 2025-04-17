#include "ACustomPawn.h"
#include "MagicCubeActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
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
	bCameraMoved = false;
	DragSensitivity = 150.f; // 默认灵敏度
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
void ACustomPawn::OnRightMousePressed()
{
	bIsRotating = true;
	bCameraMoved = true; // 标记镜头发生变化
}
void ACustomPawn::OnRightMouseReleased()
{
	bIsRotating = false;
	bCameraMoved = true; // 标记镜头发生变化
}

void ACustomPawn::Turn(float AxisValue)
{
	if (bIsRotating && FMath::Abs(AxisValue) > KINDA_SMALL_NUMBER)
	{
		FRotator CurrRot = SpringArm->GetComponentRotation();
		CurrRot.Yaw += AxisValue;
		SpringArm->SetWorldRotation(CurrRot);
		bCameraMoved = true; // 标记镜头发生变化
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
		bCameraMoved = true; // 标记镜头发生变化
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
	
	// 执行射线检测
	FVector WorldOrigin, WorldDirection;
	if (!PC->DeprojectScreenPositionToWorld(MouseX, MouseY, WorldOrigin, WorldDirection)) return;
	FVector TraceEnd = WorldOrigin + WorldDirection * 10000.f;
	FHitResult HitResult;
	if (PerformLineTrace(CubeDragStartScreen, HitResult))
    {
        // 获取选中块的索引
        if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(HitResult.GetComponent()))
        {
            SelectedCubeInstanceIndex = HitResult.Item;

            // 获取魔方维度并计算候选面
            if (AMagicCubeActor* CubeActor = GetMagicCubeActor())
            {
                SelectedCandidateFaces = CalculateCandidateFaces(SelectedCubeInstanceIndex, CubeActor->Dimensions);
            }
        }
    }
	/*
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	
	if (GetWorld()->LineTraceSingleByChannel(HitResult, WorldOrigin, TraceEnd, ECC_Visibility, QueryParams))
	{
		// 记录命中法向量
		HitNormal = HitResult.Normal;
		
		if (HitResult.GetActor() && HitResult.GetActor()->ActorHasTag(FName("Cube")))
		{
			UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(HitResult.GetComponent());
			if (ISMC && HitResult.Item != INDEX_NONE)
			{
				SelectedCubeInstanceIndex = HitResult.Item;
				FTransform InstTrans;
				ISMC->GetInstanceTransform(SelectedCubeInstanceIndex, InstTrans, true);
				
				// 这里不再依赖块位置的绝对值，而是依据选中块的 index 生成候选面
				AMagicCubeActor* CubeActor = nullptr;
				for (TActorIterator<AMagicCubeActor> It(GetWorld()); It; ++It)
				{
					CubeActor = *It;
					break;
				}
				if (CubeActor)
				{
					const TArray<int32>& CubeDims = CubeActor->Dimensions;
					if (CubeDims.Num() >= 3)
					{
						int32 dimX = CubeDims[0];
						int32 dimY = CubeDims[1];
						int32 dimZ = CubeDims[2];
						int32 index = SelectedCubeInstanceIndex;
						// 计算三维坐标
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
	*/
}

void ACustomPawn::UpdateCandidateIdealDirections()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;

    FVector CubeCenter = GetActorLocation();

    TMap<ECubeFace, FVector> FaceNormals = {
        {ECubeFace::Front, FVector(-1, 0, 0)},
        {ECubeFace::Back, FVector(1, 0, 0)},
        {ECubeFace::Left, FVector(0, -1, 0)},
        {ECubeFace::Right, FVector(0, 1, 0)},
        {ECubeFace::Top, FVector(0, 0, 1)},
        {ECubeFace::Bottom, FVector(0, 0, -1)}
    };

    CandidateIdealDirections.Empty();
    for (const auto& Face : FaceNormals)
    {
        FVector WorldDirection = CubeCenter + Face.Value * 100.f;
        FVector2D ScreenCenter, ScreenDir;
        if (PC->ProjectWorldLocationToScreen(CubeCenter, ScreenCenter) &&
            PC->ProjectWorldLocationToScreen(WorldDirection, ScreenDir))
        {
            CandidateIdealDirections.Add(Face.Key, (ScreenDir - ScreenCenter).GetSafeNormal());
        }
    }
}

bool ACustomPawn::PerformLineTrace(FVector2D ScreenPosition, FHitResult& OutHitResult)
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return false;

    FVector WorldOrigin, WorldDirection;
    if (PC->DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, WorldOrigin, WorldDirection))
    {
        FVector TraceEnd = WorldOrigin + WorldDirection * 10000.f;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(this);

        return GetWorld()->LineTraceSingleByChannel(OutHitResult, WorldOrigin, TraceEnd, ECC_Visibility, Params);
    }
    return false;
}

TArray<ECubeFace> ACustomPawn::CalculateCandidateFaces(int32 SelectedIndex, const TArray<int32>& CubeDims)
{
	TArray<ECubeFace> CandidateFaces;

    if (CubeDims.Num() < 3) return CandidateFaces; // 防护：魔方维度不足三维

    int32 DimX = CubeDims[0];
    int32 DimY = CubeDims[1];
    int32 DimZ = CubeDims[2];

    // 根据选中块的索引计算三维坐标
    int32 X = SelectedIndex % DimX;
    int32 Y = (SelectedIndex / DimX) % DimY;
    int32 Z = SelectedIndex / (DimX * DimY);

    // 添加候选面
    if (X == 0) CandidateFaces.Add(ECubeFace::Front);
    if (X == DimX - 1) CandidateFaces.Add(ECubeFace::Back);
    if (Y == 0) CandidateFaces.Add(ECubeFace::Left);
    if (Y == DimY - 1) CandidateFaces.Add(ECubeFace::Right);
    if (Z == 0) CandidateFaces.Add(ECubeFace::Bottom);
    if (Z == DimZ - 1) CandidateFaces.Add(ECubeFace::Top);

    // 添加中间层的候选面
    if (X > 0 && X < DimX - 1) CandidateFaces.Add(ECubeFace::NonFrontNonBack);
    if (Y > 0 && Y < DimY - 1) CandidateFaces.Add(ECubeFace::NonLeftNonRight);
    if (Z > 0 && Z < DimZ - 1) CandidateFaces.Add(ECubeFace::NonTopNonBottom);

    return CandidateFaces;
}

FString ACustomPawn::CubeFaceToString(ECubeFace Face)
{
    switch (Face)
    {
    case ECubeFace::Front: return TEXT("前面");
    case ECubeFace::Back: return TEXT("后面");
    case ECubeFace::Left: return TEXT("左面");
    case ECubeFace::Right: return TEXT("右面");
    case ECubeFace::Top: return TEXT("上面");
    case ECubeFace::Bottom: return TEXT("下面");
	case ECubeFace::NonLeftNonRight: return TEXT("非左非右面");
    case ECubeFace::NonTopNonBottom: return TEXT("非上非下面");
    case ECubeFace::NonFrontNonBack: return TEXT("非前非后面");
    default: return TEXT("未知");
    }
}

void ACustomPawn::ShowDebugInfo(FVector2D DragDelta, ECubeFace TargetFace, float FinalAngle)
{
    if (GEngine)
    {
        FString DebugMsg = FString::Printf(TEXT("拖拽位移: (%.1f, %.1f)\n目标面: %s\n最终角度: %.1f"),
                                           DragDelta.X, DragDelta.Y, *CubeFaceToString(TargetFace), FinalAngle);
        GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, DebugMsg);
    }
}

//
// 在 Tick() 中实时更新拖拽旋转
//
void ACustomPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

    // 更新候选面的理想方向（仅在镜头发生变化时）
    if (bCameraMoved)
    {
        UpdateCandidateIdealDirections();
        bCameraMoved = false;
    }

	if (bIsCubeDrag)
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
		
		// 拖拽初期计算目标面：从候选集合中剔除与 SelectedHitPlane 相同的候选
		if (CurrentTargetFace.IsEmpty())
		{
			TArray<FString> FilteredCandidates;
			for (const FString& Lab : SelectedCandidateFaces)
			{
				if (Lab != SelectedHitPlane)
					FilteredCandidates.Add(Lab);
			}
			const TArray<FString>& UseCandidates = (FilteredCandidates.Num() > 0 ? FilteredCandidates : SelectedCandidateFaces);

			// 构造候选数据，设置理想向量
			// 这里根据 SelectedHitPlane 分情况调整：
			// 例如，当 SelectedHitPlane=="左面"时：
			// - "前面" 设为 (0, -1)（反转默认），
			// - "后面" 设为 (0, -1)（保持默认），
			// - "非上非下面" 设为 (1, 0) 或根据需要调整；
			// 若 SelectedHitPlane=="后面"，则根据需求调整相应候选的 Ideal。
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
				{
					// 根据射线碰撞平面调整：
					// 如果正对左面，则希望前面候选取 (0, 1)（反转默认），否则用 (0,-1)
					Candidate.Ideal = (SelectedHitPlane == TEXT("左面")) ? FVector2D(0, 1) : FVector2D(0, -1);
				}
				else if (Lab == TEXT("后面"))
				{
					// 如果正对后面，则希望后面候选取 (0, 1)，否则 (0,-1)
					Candidate.Ideal = (SelectedHitPlane == TEXT("后面")) ? FVector2D(0, 1) : FVector2D(0, -1);
				}
				else if (Lab == TEXT("非上非下面"))
				{
					// 默认使用水平拖拽 (1,0)，若视角为左面，则改为 (0, -1)
					Candidate.Ideal = (SelectedHitPlane == TEXT("左面")) ? FVector2D(0, -1) : FVector2D(1, 0);
				}
				else if (Lab == TEXT("非左非右面"))
				{
					// 默认使用竖直拖拽 (0,1)，若视角为后面，则改为 (1,0)
					Candidate.Ideal = (SelectedHitPlane == TEXT("后面")) ? FVector2D(1, 0) : FVector2D(0, 1);
				}
				else if (Lab == TEXT("非前非后面"))
				{
					// 当命中平面为左/右面时，拖拽方向设为竖直（0,1），否则设为水平（1,0）
					if (SelectedHitPlane == TEXT("左面") || SelectedHitPlane == TEXT("右面")) 
					{
						Candidate.Ideal = FVector2D(0, 1);  // 上下拖拽对应 Y 轴
					}
					else 
					{
						Candidate.Ideal = FVector2D(1, 0);  // 左右拖拽对应 X 轴
					}
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
		
		// 根据固定目标面确定旋转轴和层索引
		FString TargetFace = CurrentTargetFace;
		ECubeAxis RotateAxis = ECubeAxis::X;
		int32 LayerIndex = 0;
		AMagicCubeActor* CubeActor = nullptr;
		for (TActorIterator<AMagicCubeActor> It(GetWorld()); It; ++It)
		{
			CubeActor = *It;
			break;
		}
		if (!CubeActor) return;
		const TArray<int32>& CubeDims = CubeActor->Dimensions;
		if (TargetFace == TEXT("上面"))
		{
			RotateAxis = ECubeAxis::Z;
			LayerIndex = CubeDims[2] - 1;
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
			LayerIndex = CubeDims[1] - 1;
		}
		else if (TargetFace == TEXT("前面"))
		{
			RotateAxis = ECubeAxis::X;
			LayerIndex = 0;
		}
		else if (TargetFace == TEXT("后面"))
		{
			RotateAxis = ECubeAxis::X;
			LayerIndex = CubeDims[0] - 1;
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
		
		// 在拖拽初期调用 BeginLayerRotation，仅执行一次
		static bool bDragInitiated = false;
		if (!bDragInitiated)
		{
			CubeActor->BeginLayerRotation(RotateAxis, LayerIndex);
			bDragInitiated = true;
		}
		
		// 根据目标面及拖拽分量计算旋转角度
		float RawAngle = 0.f;
		float DragSensitivity = 150.f;
		if (TargetFace == TEXT("上面") || TargetFace == TEXT("下面") || TargetFace == TEXT("非上非下面"))
		{
			// 对于上/下面和"非上非下面"，使用拖拽 X 分量
			RawAngle = -(DragDelta.X / DragSensitivity) * 90.f;
		}
		else if (TargetFace == TEXT("左面") || TargetFace == TEXT("右面") ||
				 TargetFace == TEXT("前面") || TargetFace == TEXT("后面") ||
				 TargetFace == TEXT("非左非右面") || TargetFace == TEXT("非前非后面"))
		{
			// 处理非前非后面 + 正对左面的特殊情况
			if (TargetFace == TEXT("非前非后面") && SelectedHitPlane == TEXT("左面")) 
			{
				// 上下拖拽对应 Y 轴分量，符号需反转
				RawAngle = (DragDelta.Y / DragSensitivity) * 90.f;
			}
			// 当命中平面为左面或后面且目标面为左/右面时，反转符号
			else if (SelectedHitPlane == TEXT("左面") || 
				(SelectedHitPlane == TEXT("后面") && (TargetFace == TEXT("左面") || TargetFace == TEXT("右面"))))
			{
				RawAngle = (DragDelta.Y / DragSensitivity) * 90.f; // 反转符号
			}
			else
			{
				RawAngle = -(DragDelta.Y / DragSensitivity) * 90.f; // 默认逻辑
			}
		}
		float ContinuousAngle = FMath::Clamp(RawAngle, -90.f, 90.f);
		
		CurrentRotateAxis = RotateAxis;
		CurrentLayerIndex = LayerIndex;
		CurrentDragAngle = ContinuousAngle;
		
		// 实时更新旋转
		CubeActor->SetLayerRotation(RotateAxis, LayerIndex, ContinuousAngle);
	}
	else
	{
		static bool bReset = true;
		if (!bReset) { bReset = true; }
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
	
	AMagicCubeActor* CubeActor = nullptr;
	for (TActorIterator<AMagicCubeActor> It(GetWorld()); It; ++It)
	{
		CubeActor = *It;
		break;
	}
	if (!CubeActor) return;
	
	// 如果拖拽距离太短，则视为无效拖拽，恢复旋转为 0 度
	if (DragDist < DragThresh)
	{
		CubeActor->SetLayerRotation(CurrentRotateAxis, CurrentLayerIndex, 0.f);
		if (GEngine)
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, TEXT("点击，但未产生有效拖拽。"));
		{
			static bool bDragInitiated = false;
			bDragInitiated = false;
		}
		CubeActor->EndLayerRotationDrag();
		return;
	}
	
	// 最终吸附：若当前拖拽角度绝对值大于45，则吸附至 ±90°，否则归零
	float FinalAngle = (FMath::Abs(CurrentDragAngle) > 45.f) ? ((CurrentDragAngle > 0) ? 90.f : -90.f) : 0.f;

	float AngleDelta = FinalAngle - CurrentDragAngle;
	if (FMath::Abs(AngleDelta) > KINDA_SMALL_NUMBER)
	{
		CubeActor->RotateLayer(CurrentRotateAxis, CurrentLayerIndex, AngleDelta);
	}
	else
	{
		CubeActor->SetLayerRotation(CurrentRotateAxis, CurrentLayerIndex, FinalAngle);
	}
	
	// 构造候选面字符串
	FString CandidateStr;
	for (const FString& s : SelectedCandidateFaces)
	{
		CandidateStr += s + TEXT(" ");
	}
	
	// 输出详细调试信息
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
	CubeActor->EndLayerRotationDrag();
}
