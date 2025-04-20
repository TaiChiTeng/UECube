#include "MagicCubeActor.h"
#include "Kismet/KismetMathLibrary.h"

AMagicCubeActor::AMagicCubeActor()
{
    PrimaryActorTick.bCanEverTick = true;
    
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    
    InstancedMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("InstancedMesh"));
    InstancedMesh->SetupAttachment(RootComponent);
    InstancedMesh->SetCollisionProfileName(TEXT("BlockAll"));
    
    // 如果 Dimensions 未设置，则默认使用 2x2x2 魔方
    if (Dimensions.Num() != 3)
    {
        Dimensions = { 2, 2, 2 };
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
    UE_LOG(LogTemp, Warning, TEXT("RotateLayer called: Axis=%d, LayerIndex=%d, Degrees=%f"), static_cast<int32>(Axis), LayerIndex, Degrees);
    
    if (FMath::Abs(CurrentRotation.RemainingDegrees) > KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogTemp, Warning, TEXT("Rotation in progress. Ignoring new command."));
        return;
    }
    
    int32 DimIndex = GetDimensionIndex(Axis);
    int32 MaxLayer = Dimensions[DimIndex] - 1;
    if (LayerIndex < 0 || LayerIndex > MaxLayer)
    {
        UE_LOG(LogTemp, Warning, TEXT("LayerIndex %d out of bounds (max %d)"), LayerIndex, MaxLayer);
        return;
    }
    
    CurrentRotation.Axis = Axis;
    CurrentRotation.Layer = LayerIndex;
    CurrentRotation.RemainingDegrees = Degrees;
    CollectLayerInstances(Axis, LayerIndex, CurrentRotation.AffectedInstances);
    UE_LOG(LogTemp, Warning, TEXT("Collected %d instances for rotation."), CurrentRotation.AffectedInstances.Num());
}

void AMagicCubeActor::SetLayerRotation(ECubeAxis Axis, int32 Layer, float Angle)
{
    // 如果当前拖拽数据不匹配，则初始化一次拖拽基准
    if (!bIsDraggingRotation || !(CurrentDragAxis == Axis && CurrentDragLayer == Layer))
    {
        BeginLayerRotation(Axis, Layer);
    }
    
    FVector RotationAxis;
    switch (Axis)
    {
        case ECubeAxis::X: RotationAxis = FVector::ForwardVector; break;
        case ECubeAxis::Y: RotationAxis = FVector::RightVector; break;
        case ECubeAxis::Z: RotationAxis = FVector::UpVector; break;
        default: RotationAxis = FVector::ZeroVector; break;
    }
    
    FQuat DeltaQuat(RotationAxis, FMath::DegreesToRadians(Angle));
    
    // 2. 遍历受影响实例，累加它们的世界位置计算质心 Pivot
    FVector Pivot = FVector::ZeroVector;
    for (int32 Idx : CurrentRotation.AffectedInstances)
    {
        FTransform Tr;
        InstancedMesh->GetInstanceTransform(Idx, Tr, /*bWorldSpace=*/true);
        Pivot += Tr.GetLocation();
    }
    Pivot /= CurrentRotation.AffectedInstances.Num();

    // 3. 再次遍历，用质心为中心进行四元数旋转
    for (int32 Idx : CurrentRotation.AffectedInstances)
    {
        FTransform Tr;
        InstancedMesh->GetInstanceTransform(Idx, Tr, /*bWorldSpace=*/true);

        FVector LocalPos = Tr.GetLocation() - Pivot;
        FVector NewPos   = DeltaQuat.RotateVector(LocalPos) + Pivot;
        FQuat   NewRot   = DeltaQuat * Tr.GetRotation();

        FTransform NewTr(NewRot, NewPos, Tr.GetScale3D());
        InstancedMesh->UpdateInstanceTransform(Idx, NewTr,
                                               /*bWorldSpace=*/true,
                                               /*bMarkRenderStateDirty=*/false);
    }
    
    // 同步更新顶面部件：不再仅限于 Z 轴旋转，
    // 对于每个顶面部件，其对应的实例索引为：TopLayerStartIndex + i
    int32 TopLayerStartIndex = Dimensions[0] * Dimensions[1] * (Dimensions[2]-1);
    if (TopPartComponents.Num() > 0 && CurrentDragTopPartBaseTransforms.Num() == TopPartComponents.Num())
    {
        for (int32 i = 0; i < TopPartComponents.Num(); i++)
        {
            int32 BlockIndex = TopLayerStartIndex + i;
            if (CurrentDragAffectedInstances.Contains(BlockIndex))
            {
                FTransform NewTransform = CurrentDragTopPartBaseTransforms[i];
                NewTransform.SetLocation(DeltaQuat.RotateVector(NewTransform.GetLocation()));
                NewTransform.SetRotation(DeltaQuat * NewTransform.GetRotation());
                TopPartComponents[i]->SetRelativeTransform(NewTransform);
            }
        }
    }
    
    InstancedMesh->MarkRenderStateDirty();
}

void AMagicCubeActor::CollectLayerInstances(ECubeAxis Axis, int32 Layer, TArray<int32>& OutInstances)
{
    OutInstances.Empty();
    int32 TotalInstances = InstancedMesh->GetInstanceCount();
    int32 DimIndex = GetDimensionIndex(Axis);
    
    for (int32 i = 0; i < TotalInstances; i++)
    {
        FTransform Transform;
        InstancedMesh->GetInstanceTransform(i, Transform, true);
        FVector LocalPos = Transform.GetLocation();
        int32 Coord[3];
        Coord[0] = FMath::RoundToInt(LocalPos.X / BlockSize + (Dimensions[0] - 1) / 2.0f);
        Coord[1] = FMath::RoundToInt(LocalPos.Y / BlockSize + (Dimensions[1] - 1) / 2.0f);
        Coord[2] = FMath::RoundToInt(LocalPos.Z / BlockSize + (Dimensions[2] - 1) / 2.0f);
        
        if (Coord[DimIndex] == Layer)
        {
            OutInstances.Add(i);
        }
    }
}

void AMagicCubeActor::ApplyRotationToInstances(float DeltaDegrees)
{
    // 1. 计算当前轴的旋转四元数
    FVector RotAxis = (CurrentRotation.Axis == ECubeAxis::X) ? FVector::ForwardVector :
                      (CurrentRotation.Axis == ECubeAxis::Y) ? FVector::RightVector :
                                                             FVector::UpVector;
    FQuat DeltaQuat(RotAxis, FMath::DegreesToRadians(DeltaDegrees));

    // 2. 遍历受影响实例，累加它们的世界位置计算质心 Pivot
    FVector Pivot = FVector::ZeroVector;
    for (int32 Idx : CurrentRotation.AffectedInstances)
    {
        FTransform Tr;
        InstancedMesh->GetInstanceTransform(Idx, Tr, /*bWorldSpace=*/true);
        Pivot += Tr.GetLocation();
    }
    Pivot /= CurrentRotation.AffectedInstances.Num();

    // 3. 再次遍历，用质心为中心进行四元数旋转
    for (int32 Idx : CurrentRotation.AffectedInstances)
    {
        FTransform Tr;
        InstancedMesh->GetInstanceTransform(Idx, Tr, /*bWorldSpace=*/true);

        FVector LocalPos = Tr.GetLocation() - Pivot;
        FVector NewPos   = DeltaQuat.RotateVector(LocalPos) + Pivot;
        FQuat   NewRot   = DeltaQuat * Tr.GetRotation();

        FTransform NewTr(NewRot, NewPos, Tr.GetScale3D());
        InstancedMesh->UpdateInstanceTransform(Idx, NewTr,
                                               /*bWorldSpace=*/true,
                                               /*bMarkRenderStateDirty=*/false);
    }
    // 同步更新顶面部件：与 RotateLayer 中的处理相似
    int32 TopLayerStartIndex = Dimensions[0] * Dimensions[1] * (Dimensions[2]-1);
    for (int32 i = 0; i < TopPartComponents.Num(); i++)
    {
        int32 BlockIndex = TopLayerStartIndex + i;
        if (CurrentRotation.AffectedInstances.Contains(BlockIndex))
        {
            UStaticMeshComponent* TopPartComp = TopPartComponents[i];
            if (TopPartComp)
            {
                FVector CurrPos = TopPartComp->GetRelativeLocation();
                FVector NewPos = DeltaQuat.RotateVector(CurrPos);
                TopPartComp->SetRelativeLocation(NewPos);
                
                FQuat CurrRot = TopPartComp->GetRelativeRotation().Quaternion();
                FQuat NewRot = DeltaQuat * CurrRot;
                TopPartComp->SetRelativeRotation(NewRot);
                
                UE_LOG(LogTemp, Warning, TEXT("同步旋转顶面部件 %d，对应 BlockIndex=%d"), i, BlockIndex);
            }
        }
    }
    // 4. 一次性刷新渲染
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
    UE_LOG(LogTemp, Warning, TEXT("顶面格子数：%d"), ExpectedCount);
    
    int32 MeshCount = TopPartMeshes.Num();
    if (MeshCount < ExpectedCount)
    {
        UE_LOG(LogTemp, Warning, TEXT("TopPartMeshes 配置不足，期望 %d 个，但只有 %d 个将被创建。"), ExpectedCount, MeshCount);
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
            UE_LOG(LogTemp, Warning, TEXT("自动调整顶面部件：Scale=%f, VerticalOffset=%f"), UniformScale, TopPartVerticalOffset);
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
                    UE_LOG(LogTemp, Warning, TEXT("生成顶面部件 %d 在位置: %s"), Index, *PartPos.ToString());
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
        // 对于每个 top part，判断其对应的实例索引是否在受影响集合中
        for (int32 i = 0; i < TopPartComponents.Num(); i++)
        {
            int32 BlockIndex = TopLayerStartIndex + i;
            if (CurrentDragAffectedInstances.Contains(BlockIndex))
            {
                UStaticMeshComponent* Comp = TopPartComponents[i];
                if (Comp)
                    CurrentDragTopPartBaseTransforms.Add(Comp->GetRelativeTransform());
                else
                    CurrentDragTopPartBaseTransforms.Add(FTransform::Identity);
            }
            else
            {
                // 如果该 top part不在当前受影响范围中，依然填入一个占位值
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