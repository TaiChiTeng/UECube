#include "MagicCubeActor.h"
#include "Kismet/KismetMathLibrary.h"

AMagicCubeActor::AMagicCubeActor()
{
    PrimaryActorTick.bCanEverTick = true;
    
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    
    InstancedMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("InstancedMesh"));
    InstancedMesh->SetupAttachment(RootComponent);
    InstancedMesh->SetCollisionProfileName(TEXT("BlockAll"));
    
    // 如果 Dimensions 未设置，则默认使用 1x1x1 魔方
    if (Dimensions.Num() != 3)
    {
        Dimensions = { 1, 1, 1 };
    }
}

void AMagicCubeActor::OnConstruction(const FTransform& Transform)
{
    int32 TotalCells = Dimensions[0] * Dimensions[1] * Dimensions[2];
    if (LayoutMask.Num() != TotalCells)
    {
        LayoutMask.Init(true, TotalCells);
    }
    
    if (CubeMesh)
    {
        InstancedMesh->SetStaticMesh(CubeMesh);
    }
    if (CubeMaterial)
    {
        InstancedMesh->SetMaterial(0, CubeMaterial);
    }
    
    InstancedMesh->ClearInstances();
    InitializeCube();
    
    if (Dimensions.Num() >= 3)
    {
        InitializeTopParts();
    }
}

void AMagicCubeActor::BeginPlay()
{
    Super::BeginPlay();
    InitialTransforms.Empty();
    int32 InstanceCount = InstancedMesh->GetInstanceCount();
    for (int32 i = 0; i < InstanceCount; i++)
    {
        FTransform Transform;
        InstancedMesh->GetInstanceTransform(i, Transform, true);
        InitialTransforms.Add(Transform);
    }
}

void AMagicCubeActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    if (FMath::Abs(CurrentRotation.RemainingDegrees) > KINDA_SMALL_NUMBER)
    {
        float DeltaRotation = FMath::Sign(CurrentRotation.RemainingDegrees) *
            FMath::Min(RotationSpeed * DeltaTime, FMath::Abs(CurrentRotation.RemainingDegrees));
        
        ApplyRotationToInstances(DeltaRotation);
        CurrentRotation.RemainingDegrees -= DeltaRotation;
        
        if (FMath::IsNearlyZero(CurrentRotation.RemainingDegrees))
        {
            // 更新全局初始变换：对于每个受影响的实例，保存最新状态
            for (int32 Index : CurrentRotation.AffectedInstances)
            {
                FTransform T;
                InstancedMesh->GetInstanceTransform(Index, T, true);
                if (InitialTransforms.IsValidIndex(Index))
                {
                    InitialTransforms[Index] = T;
                }
            }
            
            // 对顶面部件，确保 TopPartInitialTransforms 数组与 TopPartComponents 数量匹配
            if (CurrentRotation.Axis == ECubeAxis::Z && CurrentRotation.Layer == Dimensions[2]-1)
            {
                if (TopPartInitialTransforms.Num() != TopPartComponents.Num())
                {
                    TopPartInitialTransforms.Empty();
                    for (int32 i = 0; i < TopPartComponents.Num(); i++)
                    {
                        if (TopPartComponents[i])
                            TopPartInitialTransforms.Add(TopPartComponents[i]->GetRelativeTransform());
                    }
                }
                else
                {
                    for (int32 i = 0; i < TopPartComponents.Num(); i++)
                    {
                        if (TopPartComponents[i])
                            TopPartInitialTransforms[i] = TopPartComponents[i]->GetRelativeTransform();
                    }
                }
            }
            
            OnRotationComplete.Broadcast(CurrentRotation.Axis, CurrentRotation.Layer);
            EndLayerRotationDrag();
        }
    }
}

void AMagicCubeActor::InitializeCube()
{
    float ComputedScale = 1.0f;
    if (CubeMesh)
    {
        FBoxSphereBounds MeshBounds = CubeMesh->GetBounds();
        float MeshSize = MeshBounds.BoxExtent.GetMax() * 2.0f;
        if (MeshSize > KINDA_SMALL_NUMBER)
        {
            ComputedScale = BlockSize / MeshSize;
        }
    }
    else
    {
        ComputedScale = BlockSize / 100.0f;
    }
    
    for (int32 z = 0; z < Dimensions[2]; z++)
    {
        for (int32 y = 0; y < Dimensions[1]; y++)
        {
            for (int32 x = 0; x < Dimensions[0]; x++)
            {
                int32 LinearIndex = x + y * Dimensions[0] + z * Dimensions[0] * Dimensions[1];
                bool bPlaceBlock = LayoutMask.IsValidIndex(LinearIndex) ? LayoutMask[LinearIndex] : true;
                if (bPlaceBlock)
                {
                    FTransform Transform;
                    Transform.SetLocation(CalculatePosition(x, y, z));
                    Transform.SetScale3D(FVector(ComputedScale));
                    InstancedMesh->AddInstance(Transform);
                }
            }
        }
    }
}

FVector AMagicCubeActor::CalculatePosition(int32 x, int32 y, int32 z) const
{
    float OffsetX = (x - (Dimensions[0] - 1) / 2.0f) * BlockSize;
    float OffsetY = (y - (Dimensions[1] - 1) / 2.0f) * BlockSize;
    float OffsetZ = (z - (Dimensions[2] - 1) / 2.0f) * BlockSize;
    return FVector(OffsetX, OffsetY, OffsetZ);
}

int32 AMagicCubeActor::GetLinearIndex(int32 x, int32 y, int32 z) const
{
    return x + y * Dimensions[0] + z * Dimensions[0] * Dimensions[1];
}

void AMagicCubeActor::RotateLayer(ECubeAxis Axis, int32 LayerIndex, float Degrees)
{
    if (FMath::Abs(CurrentRotation.RemainingDegrees) > KINDA_SMALL_NUMBER)
    {
        return;
    }
    
    int32 DimIndex = GetDimensionIndex(Axis);
    int32 MaxLayer = Dimensions[DimIndex] - 1;
    if (LayerIndex < 0 || LayerIndex > MaxLayer)
    {
        return;
    }
    
    CurrentRotation.Axis = Axis;
    CurrentRotation.Layer = LayerIndex;
    CurrentRotation.RemainingDegrees = Degrees;
    CollectLayerInstances(Axis, LayerIndex, CurrentRotation.AffectedInstances);
}

void AMagicCubeActor::SetLayerRotation(ECubeAxis Axis, int32 Layer, float Angle)
{
    // 如果当前拖拽数据不匹配，则初始化一次拖拽基准
    if (!bIsDraggingRotation || !(CurrentDragAxis == Axis && CurrentDragLayer == Layer))
    {
        BeginLayerRotation(Axis, Layer);
    }

    // 1. 计算旋转轴（与RotateLayer一致）
    FVector RotationAxis;
    switch (Axis)
    {
        case ECubeAxis::X: RotationAxis = FVector::ForwardVector; break;
        case ECubeAxis::Y: RotationAxis = FVector::RightVector;   break;
        case ECubeAxis::Z: RotationAxis = FVector::UpVector;      break;
        default:           RotationAxis = FVector::ZeroVector;    break;
    }
    FQuat RotQuat(RotationAxis, FMath::DegreesToRadians(Angle));

    // 2. 计算世界空间中的枢轴点（与RotateLayer一致）
    FVector Pivot = FVector::ZeroVector;
    for (const FTransform& BaseTransform : CurrentDragBaseTransforms)
    {
        Pivot += BaseTransform.GetLocation();
    }
    Pivot /= CurrentDragBaseTransforms.Num();

    // 3. 应用旋转到实例（世界空间计算）
    for (int32 i = 0; i < CurrentDragAffectedInstances.Num(); i++)
    {
        int32 idx = CurrentDragAffectedInstances[i];
        if (CurrentDragBaseTransforms.IsValidIndex(i))
        {
            FTransform BaseTransform = CurrentDragBaseTransforms[i];
            FVector LocalOffset = BaseTransform.GetLocation() - Pivot;
            FVector NewWorldPos = Pivot + RotQuat.RotateVector(LocalOffset);
            FQuat NewWorldRot = RotQuat * BaseTransform.GetRotation();

            FTransform NewTransform;
            NewTransform.SetLocation(NewWorldPos);
            NewTransform.SetRotation(NewWorldRot);
            NewTransform.SetScale3D(BaseTransform.GetScale3D());

            InstancedMesh->UpdateInstanceTransform(idx, NewTransform, true);
        }
    }

    // 4. 同步更新顶面部件（关键修正部分）
    int32 TopLayerStartIndex = Dimensions[0] * Dimensions[1] * (Dimensions[2]-1);
    for (int32 i = 0; i < TopPartComponents.Num(); i++)
    {
        int32 BlockIndex = TopLayerStartIndex + i;
        if (CurrentDragAffectedInstances.Contains(BlockIndex) && 
            CurrentDragTopPartBaseTransforms.IsValidIndex(i))
        {
            UStaticMeshComponent* Comp = TopPartComponents[i];
            if (Comp)
            {
                // 获取顶面部件的世界空间初始位置（相对于RootComponent）
                FTransform InitialWorldTransform = CurrentDragTopPartBaseTransforms[i] * RootComponent->GetComponentTransform();
                
                // 计算相对于枢轴点的偏移
                FVector LocalOffset = InitialWorldTransform.GetLocation() - Pivot;
                
                // 应用旋转
                FVector NewWorldPos = Pivot + RotQuat.RotateVector(LocalOffset);
                FQuat NewWorldRot = RotQuat * InitialWorldTransform.GetRotation();
                
                // 转换回相对RootComponent的变换
                FTransform NewWorldTransform;
                NewWorldTransform.SetLocation(NewWorldPos);
                NewWorldTransform.SetRotation(NewWorldRot);
                NewWorldTransform.SetScale3D(InitialWorldTransform.GetScale3D());
                
                FTransform NewRelativeTransform = NewWorldTransform.GetRelativeTransform(RootComponent->GetComponentTransform());
                Comp->SetRelativeTransform(NewRelativeTransform);
            }
        }
    }

    InstancedMesh->MarkRenderStateDirty();
}

void AMagicCubeActor::CollectLayerInstances(ECubeAxis Axis, int32 Layer, TArray<int32>& OutInstances)
{
    OutInstances.Empty();
    int32 Total = InstancedMesh->GetInstanceCount();
    int32 DimIdx = GetDimensionIndex(Axis);

    // 在组件自身空间里，这一层对应的理想坐标
    float CenterOffset = (Dimensions[DimIdx] - 1) * 0.5f;
    float TargetLocal = (Layer - CenterOffset) * BlockSize;
    const float Tol = 0.01f;

    for (int32 i = 0; i < Total; ++i)
    {
        // 直接拿「组件局部」Transform
        FTransform LocalTr;
        InstancedMesh->GetInstanceTransform(i, LocalTr, /*bWorldSpace=*/false);

        float V = (Axis == ECubeAxis::X ? LocalTr.GetLocation().X :
                   Axis == ECubeAxis::Y ? LocalTr.GetLocation().Y :
                                          LocalTr.GetLocation().Z);

        if (FMath::Abs(V - TargetLocal) <= Tol)
        {
            OutInstances.Add(i);
        }
    }
}

void AMagicCubeActor::ApplyRotationToInstances(float DeltaDegrees)
{
    // 1. 计算旋转轴
    FVector RotationAxis;
    switch (CurrentRotation.Axis)
    {
        case ECubeAxis::X: RotationAxis = FVector::ForwardVector; break;
        case ECubeAxis::Y: RotationAxis = FVector::RightVector;   break;
        case ECubeAxis::Z: RotationAxis = FVector::UpVector;      break;
        default:           RotationAxis = FVector::ZeroVector;    break;
    }
    FQuat DeltaQuat(RotationAxis, FMath::DegreesToRadians(DeltaDegrees));

    // 2. 先算出这一轮所有实例的世界质心（Pivot）
    FVector Pivot = FVector::ZeroVector;
    for (int32 Index : CurrentRotation.AffectedInstances)
    {
        FTransform Tr;
        InstancedMesh->GetInstanceTransform(Index, Tr, /*bWorldSpace=*/ true);
        Pivot += Tr.GetLocation();
    }
    Pivot /= CurrentRotation.AffectedInstances.Num();

    // 3. 围绕 Pivot 做旋转
    for (int32 Index : CurrentRotation.AffectedInstances)
    {
        // 拿到当前 world-space Transform
        FTransform Tr;
        InstancedMesh->GetInstanceTransform(Index, Tr, /*bWorldSpace=*/ true);

        // 移到枢轴原点、旋转、再移回
        FVector LocalOffset = Tr.GetLocation() - Pivot;
        FVector NewWorldPos = Pivot + DeltaQuat.RotateVector(LocalOffset);
        FQuat   NewWorldRot = DeltaQuat * Tr.GetRotation();

        FTransform NewTr;
        NewTr.SetLocation(NewWorldPos);
        NewTr.SetRotation(NewWorldRot);
        NewTr.SetScale3D(Tr.GetScale3D());

        InstancedMesh->UpdateInstanceTransform(Index, NewTr, /*bWorldSpace=*/ true, /*bMarkRenderStateDirty=*/ false);
    }

    // 4. 同步顶面部件（若有），同样围绕相同的 Pivot 做旋转
    int32 TopLayerStartIndex = Dimensions[0] * Dimensions[1] * (Dimensions[2] - 1);
    for (int32 i = 0; i < TopPartComponents.Num(); i++)
    {
        int32 BlockIndex = TopLayerStartIndex + i;
        if (CurrentRotation.AffectedInstances.Contains(BlockIndex))
        {
            UStaticMeshComponent* Comp = TopPartComponents[i];
            if (Comp)
            {
                // 取相对 Root 的世界 Transform
                FTransform CompWorldTr = Comp->GetComponentTransform();
                FVector LocalOffset = CompWorldTr.GetLocation() - Pivot;
                FVector NewWorldPos = Pivot + DeltaQuat.RotateVector(LocalOffset);
                FQuat   NewWorldRot = DeltaQuat * CompWorldTr.GetRotation();

                // 转回相对 Root 的相对变换
                FTransform NewRel = CompWorldTr;
                NewRel.SetLocation(NewWorldPos);
                NewRel.SetRotation(NewWorldRot);
                Comp->SetWorldTransform(NewRel);
            }
        }
    }

    // 5. 最后一次性刷新渲染
    InstancedMesh->MarkRenderStateDirty();
}


int32 AMagicCubeActor::GetDimensionIndex(ECubeAxis Axis) const
{
    switch (Axis)
    {
        case ECubeAxis::X: return 0;
        case ECubeAxis::Y: return 1;
        case ECubeAxis::Z: return 2;
    }
    return 0;
}

void AMagicCubeActor::Scramble(int32 Moves)
{
    for (int32 i = 0; i < Moves; i++)
    {
        ECubeAxis RandomAxis = static_cast<ECubeAxis>(FMath::RandRange(0, 2));
        int32 RandomLayer = FMath::RandRange(0, Dimensions[GetDimensionIndex(RandomAxis)] - 1);
        float RandomAngle = (FMath::RandBool() ? 90.0f : -90.0f);
        RotateLayer(RandomAxis, RandomLayer, RandomAngle);
    }
}

void AMagicCubeActor::ResetCube()
{
    InstancedMesh->ClearInstances();
    for (const FTransform& Transform : InitialTransforms)
    {
        InstancedMesh->AddInstance(Transform);
    }
}

void AMagicCubeActor::InitializeTopParts()
{
    for (UStaticMeshComponent* Comp : TopPartComponents)
    {
        if (Comp)
        {
            Comp->DestroyComponent();
        }
    }
    TopPartComponents.Empty();
    TopPartInitialTransforms.Empty();
    
    int32 ExpectedCount = Dimensions[0] * Dimensions[1];
    
    int32 MeshCount = TopPartMeshes.Num();
    if (MeshCount < ExpectedCount)
    {
    }
    
    if (bAutoAdjustTopPart)
    {
        if (TopPartMeshes.Num() > 0 && TopPartMeshes[0])
        {
            FBoxSphereBounds Bounds = TopPartMeshes[0]->GetBounds();
            FVector Extent = Bounds.BoxExtent * 2.0f;
            float MaxDimension = FMath::Max3(Extent.X, Extent.Y, Extent.Z);
            float TargetMax = BlockSize * TopPartSize;
            float UniformScale = TargetMax / MaxDimension;
            TopPartScale = FVector(UniformScale);
            float ScaledHalfHeight = (Extent.Z * UniformScale) * 0.5f;
            TopPartVerticalOffset = BlockSize * 0.5f + ScaledHalfHeight;
        }
    }
    
    int32 TopZ = Dimensions[2] - 1;
    for (int32 y = 0; y < Dimensions[1]; y++)
    {
        for (int32 x = 0; x < Dimensions[0]; x++)
        {
            int32 Index = x + y * Dimensions[0];
            FVector BlockPos = CalculatePosition(x, y, TopZ);
            FVector PartPos = BlockPos + FVector(0, 0, TopPartVerticalOffset);
            if (Index < TopPartMeshes.Num() && TopPartMeshes[Index])
            {
                FString CompName = FString::Printf(TEXT("TopPart_%d"), Index);
                UStaticMeshComponent* PartComp = NewObject<UStaticMeshComponent>(this, FName(*CompName));
                if (PartComp)
                {
                    PartComp->RegisterComponent();
                    PartComp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
                    PartComp->SetRelativeLocation(PartPos);
                    PartComp->SetRelativeScale3D(TopPartScale);
                    PartComp->SetStaticMesh(TopPartMeshes[Index]);
                    TopPartComponents.Add(PartComp);
                    TopPartInitialTransforms.Add(PartComp->GetRelativeTransform());
                    PartComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                    // PartComp->SetCollisionResponseToAllChannels(ECR_Ignore);
                }
            }
        }
    }
}

void AMagicCubeActor::BeginLayerRotation(ECubeAxis Axis, int32 Layer)
{
    bIsDraggingRotation = true;
    CurrentDragAxis = Axis;
    CurrentDragLayer = Layer;
    
    CollectLayerInstances(Axis, Layer, CurrentDragAffectedInstances);
    
    CurrentDragBaseTransforms.Empty();
    for (int32 Index : CurrentDragAffectedInstances)
    {
        FTransform T;
        InstancedMesh->GetInstanceTransform(Index, T, true);
        CurrentDragBaseTransforms.Add(T);
    }
    
    // 对顶面部件：判断顶层对应的实例索引范围
    int32 TopLayerStartIndex = Dimensions[0] * Dimensions[1] * (Dimensions[2]-1);
    CurrentDragTopPartBaseTransforms.Empty();
    if (TopPartComponents.Num() > 0)
    {
        for (int32 i = 0; i < TopPartComponents.Num(); i++)
        {
            if (TopPartComponents[i])
            {
                // 保存相对变换
                CurrentDragTopPartBaseTransforms.Add(TopPartComponents[i]->GetRelativeTransform());
            }
            else
            {
                CurrentDragTopPartBaseTransforms.Add(FTransform::Identity);
            }
        }
    }
}

void AMagicCubeActor::EndLayerRotationDrag()
{
    bIsDraggingRotation = false;
    CurrentDragAffectedInstances.Empty();
    CurrentDragBaseTransforms.Empty();
    CurrentDragTopPartBaseTransforms.Empty();
}

// 根据魔方块坐标计算归属面集合
TArray<EMagicCubeFace> AMagicCubeActor::GetCubeFacesForBlock(int32 x, int32 y, int32 z) const
{
    TArray<EMagicCubeFace> Faces;
    // Teng：这里AI容易错，UE是左手坐标系，X是食指红色（对准屏幕），Y是中指绿色（对准屏幕右方），Z是大拇指蓝色（对准屏幕上方）
    if (x == 0) Faces.Add(EMagicCubeFace::Front); // 前面
    if (x == Dimensions[0] - 1) Faces.Add(EMagicCubeFace::Back); // 后面
    if (x != 0 && x != Dimensions[0] - 1) Faces.Add(EMagicCubeFace::Standing); // 非前非后面
    if (y == 0) Faces.Add(EMagicCubeFace::Left); // 左面
    if (y == Dimensions[1] - 1) Faces.Add(EMagicCubeFace::Right); // 右面
    if (y != 0 && y != Dimensions[1] - 1) Faces.Add(EMagicCubeFace::Middle); // 非左非右面
    if (z == 0) Faces.Add(EMagicCubeFace::Bottom); // 底部
    if (z == Dimensions[2] - 1) Faces.Add(EMagicCubeFace::Top); // 顶部
    if (z != 0 && z != Dimensions[2] - 1) Faces.Add(EMagicCubeFace::Equatorial); // 非顶非底面

    return Faces;
}

// 获取面的法向量
FVector AMagicCubeActor::GetFaceNormal(EMagicCubeFace Face) const
{
    // 调用示例：
    // 获取顶面的法向量
    // FVector TopNormal = GetFaceNormal(EMagicCubeFace::Top);
    // UE_LOG(LogTemp, Warning, TEXT("Top Face Normal: %s"), *TopNormal.ToString());
    // // 获取左面的法向量
    // FVector LeftNormal = GetFaceNormal(EMagicCubeFace::Left);
    // UE_LOG(LogTemp, Warning, TEXT("Left Face Normal: %s"), *LeftNormal.ToString());

    // 定义面与法向量的映射
    // Teng：这里AI容易错，UE是左手坐标系，X是食指红色（对准屏幕），Y是中指绿色（对准屏幕右方），Z是大拇指蓝色（对准屏幕上方）
    TMap<EMagicCubeFace, FVector> FaceNormals = {
        {EMagicCubeFace::Top, FVector(0, 0, 1)},
        {EMagicCubeFace::Bottom, FVector(0, 0, -1)},
        {EMagicCubeFace::Equatorial, FVector(0, 0, 1)},
        {EMagicCubeFace::Front, FVector(-1, 0, 0)},
        {EMagicCubeFace::Back, FVector(1, 0, 0)},
        {EMagicCubeFace::Standing, FVector(-1, 0, 0)},
        {EMagicCubeFace::Left, FVector(0, -1, 0)},
        {EMagicCubeFace::Right, FVector(0, 1, 0)},
        {EMagicCubeFace::Middle, FVector(0, -1, 0)}
    };

    // 查找对应面的法向量，如果找不到则返回零向量
    if (FaceNormals.Contains(Face))
    {
        return FaceNormals[Face];
    }
    else
    {
        return FVector::ZeroVector; // 返回零向量表示错误
    }
}

// 获取面"顺时针"旋转的向量
FVector AMagicCubeActor::GetFaceRotateDirection(EMagicCubeFace Face) const
{
    // 调用示例：
    // 获取顶面的顺时针旋转向量
    // FVector TopRotationAxis = GetFaceRotateDirection(EMagicCubeFace::Top);
    // UE_LOG(LogTemp, Warning, TEXT("Top Face Rotation Axis: %s"), *TopRotationAxis.ToString());
    // 获取左面的顺时针旋转向量
    // FVector LeftRotationAxis = GetFaceRotateDirection(EMagicCubeFace::Left);
    // UE_LOG(LogTemp, Warning, TEXT("Left Face Rotation Axis: %s"), *LeftRotationAxis.ToString());
    
    // 定义面与顺时针旋转向量的映射
    // Teng：这里AI容易错，UE是左手坐标系，X是食指红色（对准屏幕），Y是中指绿色（对准屏幕右方），Z是大拇指蓝色（对准屏幕上方）
    TMap<EMagicCubeFace, FVector> ClockRotateNormals = {
        {EMagicCubeFace::Top, FVector(0, 1, 0)},
        {EMagicCubeFace::Bottom, FVector(0, -1, 0)},
        {EMagicCubeFace::Equatorial, FVector(0, 1, 0)},
        {EMagicCubeFace::Front, FVector(0, 0, -1)},
        {EMagicCubeFace::Back, FVector(0, 0, 1)},
        {EMagicCubeFace::Standing, FVector(0, 0, -1)},
        {EMagicCubeFace::Left, FVector(1, 0, 0)},
        {EMagicCubeFace::Right, FVector(-1, 0, 0)},
        {EMagicCubeFace::Middle, FVector(1, 0, 0)}
    };

    // 查找对应面的顺时针旋转轴向量，如果找不到则返回零向量
    if (ClockRotateNormals.Contains(Face))
    {
        return ClockRotateNormals[Face];
    }
    else
    {
        return FVector::ZeroVector; // 返回零向量表示错误
    }
}
// 获取面的反面
EMagicCubeFace AMagicCubeActor::GetOppositeFace(EMagicCubeFace Face) const
{
    // 定义面与反面的映射
    // Teng：这里AI容易错，UE是左手坐标系，X是食指红色（对准屏幕），Y是中指绿色（对准屏幕右方），Z是大拇指蓝色（对准屏幕上方）
    TMap<EMagicCubeFace, EMagicCubeFace> FaceAnti = {
        {EMagicCubeFace::Top, EMagicCubeFace::Bottom},
        {EMagicCubeFace::Bottom, EMagicCubeFace::Top},
        {EMagicCubeFace::Front, EMagicCubeFace::Back},
        {EMagicCubeFace::Back, EMagicCubeFace::Front},
        {EMagicCubeFace::Left, EMagicCubeFace::Right},
        {EMagicCubeFace::Right, EMagicCubeFace::Left}
    };

    // 查找对应面的反面，如果找不到则返回原面
    if (FaceAnti.Contains(Face))
    {
        return FaceAnti[Face];
    }
    else
    {
        return Face; // 返回原面表示错误
    }
}

// 获取面的旋转轴
ECubeAxis AMagicCubeActor::GetRotateAxis(EMagicCubeFace Face) const
{
    // 定义面与旋转轴的映射
    TMap<EMagicCubeFace, ECubeAxis> FaceRotateAxis = {
        {EMagicCubeFace::Top, ECubeAxis::Z},
        {EMagicCubeFace::Bottom, ECubeAxis::Z},
        {EMagicCubeFace::Equatorial, ECubeAxis::Z},
        {EMagicCubeFace::Front, ECubeAxis::X},
        {EMagicCubeFace::Back, ECubeAxis::X},
        {EMagicCubeFace::Standing, ECubeAxis::X},
        {EMagicCubeFace::Left, ECubeAxis::Y},
        {EMagicCubeFace::Right, ECubeAxis::Y},
        {EMagicCubeFace::Middle, ECubeAxis::Y}
    };

    // 查找对应面的旋转轴，如果找不到则返回X轴
    if (FaceRotateAxis.Contains(Face))
    {
        return FaceRotateAxis[Face];
    }
    else
    {
        return ECubeAxis::X; // 返回X轴表示错误或未定义
    }
}

// 获取面的层索引
int32 AMagicCubeActor::GetLayerIndex(EMagicCubeFace Face) const
{
    // 定义面与层索引的映射
    TMap<EMagicCubeFace, int32> FaceLayerIndex = {
        {EMagicCubeFace::Top, Dimensions[2] - 1},
        {EMagicCubeFace::Bottom, 0},
        {EMagicCubeFace::Equatorial, Dimensions[2] - 2},
        {EMagicCubeFace::Front, 0},
        {EMagicCubeFace::Back, Dimensions[0] - 1},
        {EMagicCubeFace::Standing, Dimensions[0] - 2},
        {EMagicCubeFace::Left, 0},
        {EMagicCubeFace::Right, Dimensions[1] - 1},
        {EMagicCubeFace::Middle, Dimensions[1] - 2}
    };

    // 查找对应面的层索引，如果找不到则返回-1
    if (FaceLayerIndex.Contains(Face))
    {
        return FaceLayerIndex[Face];
    }
    else
    {
        return -1; // 返回-1表示错误或未定义
    }
}