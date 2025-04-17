#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "MagicCubeActor.generated.h"

UENUM(BlueprintType)
enum class ECubeAxis : uint8
{
    X,
    Y,
    Z
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRotationComplete, ECubeAxis, Axis, int32, LayerIndex);

UCLASS()
class FASTUEC_API AMagicCubeActor : public AActor
{
    GENERATED_BODY()

public:
    AMagicCubeActor();

    // **** 将连续拖拽接口设为 public ****
    void BeginLayerRotation(ECubeAxis Axis, int32 Layer);
    void EndLayerRotationDrag();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void OnConstruction(const FTransform& Transform) override;

public:
    // 魔方尺寸：Dimensions[0]=X方向块数，Dimensions[1]=Y方向块数，Dimensions[2]=Z方向块数
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    TArray<int32> Dimensions;

    // 布局掩码
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    TArray<bool> LayoutMask;

    // 每个块的尺寸（厘米）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    float BlockSize = 100.0f;

    // 顶面部件垂直偏移（厘米）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube|TopParts")
    float TopPartVerticalOffset = 10.0f;

    // 顶面部件相对比例
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube|TopParts")
    float TopPartSize = 1.0f;

    // 顶面部件缩放比例
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube|TopParts")
    FVector TopPartScale = FVector(1.0f, 1.0f, 1.0f);

    // 是否自动调整顶面部件
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube|TopParts")
    bool bAutoAdjustTopPart = true;
    
    UFUNCTION(BlueprintCallable, Category = "MagicCube")
    void SetLayerRotation(ECubeAxis Axis, int32 Layer, float Angle);
    
    // 旋转速度（度/秒）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    float RotationSpeed = 360.0f;

    // 静态网格与材质
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    UStaticMesh* CubeMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    UMaterialInterface* CubeMaterial;

    // InstancedStaticMesh 组件
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube")
    UInstancedStaticMeshComponent* InstancedMesh;

    // 旋转完成事件
    UPROPERTY(BlueprintAssignable, Category = "MagicCube")
    FOnRotationComplete OnRotationComplete;

    // 旋转指定轴上、指定层的所有块，Degrees 可正可负
    UFUNCTION(BlueprintCallable, Category = "MagicCube")
    void RotateLayer(ECubeAxis Axis, int32 LayerIndex, float Degrees);

    // 打乱魔方
    UFUNCTION(BlueprintCallable, Category = "MagicCube")
    void Scramble(int32 Moves = 20);

    // 重置魔方
    UFUNCTION(BlueprintCallable, Category = "MagicCube")
    void ResetCube();

    // 顶面部件相关
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube|TopParts")
    TArray<UStaticMesh*> TopPartMeshes;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube|TopParts")
    TArray<UStaticMeshComponent*> TopPartComponents;

private:
    // 内部旋转状态结构
    struct FRotationData {
        ECubeAxis Axis;
        int32 Layer;
        float RemainingDegrees;
        TArray<int32> AffectedInstances;
    };

    FRotationData CurrentRotation;
    // 保存全局初始状态
    TArray<FTransform> InitialTransforms;
    // 保存顶面部件初始相对变换
    TArray<FTransform> TopPartInitialTransforms;

    // 以下为连续拖拽旋转时的临时数据
    TArray<int32> CurrentDragAffectedInstances;
    TArray<FTransform> CurrentDragBaseTransforms;
    TArray<FTransform> CurrentDragTopPartBaseTransforms;
    ECubeAxis CurrentDragAxis;
    int32 CurrentDragLayer;
    bool bIsDraggingRotation = false;

    void InitializeCube();
    void InitializeTopParts();
    FVector CalculatePosition(int32 x, int32 y, int32 z) const;
    void CollectLayerInstances(ECubeAxis Axis, int32 Layer, TArray<int32>& OutInstances);
    void ApplyRotationToInstances(float DeltaDegrees);
    int32 GetDimensionIndex(ECubeAxis Axis) const;
    int32 GetLinearIndex(int32 x, int32 y, int32 z) const;
};
