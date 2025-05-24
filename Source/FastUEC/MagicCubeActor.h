#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "MagicCubeActor.generated.h" // 确保正确保留

// Teng：定义魔方的面类型，ECubeFace会编译失败，可能UE已经用了
UENUM(BlueprintType)
enum class EMagicCubeFace : uint8
{
    Top,    // 顶部
    Bottom, // 底部
    Front,  // 前面
    Back,   // 后面
    Left,   // 左面
    Right   // 右面
};

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

    // 根据魔方块坐标计算归属面集合
    TArray<EMagicCubeFace> GetCubeFacesForBlock(int32 x, int32 y, int32 z) const;

    void BeginLayerRotation(ECubeAxis Axis, int32 Layer);
    void EndLayerRotationDrag();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void OnConstruction(const FTransform& Transform) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    TArray<int32> Dimensions;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    TArray<bool> LayoutMask;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    float BlockSize = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube|TopParts")
    float TopPartVerticalOffset = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube|TopParts")
    float TopPartSize = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube|TopParts")
    FVector TopPartScale = FVector(1.0f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube|TopParts")
    bool bAutoAdjustTopPart = true;

    UFUNCTION(BlueprintCallable, Category = "MagicCube")
    void SetLayerRotation(ECubeAxis Axis, int32 Layer, float Angle);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    float RotationSpeed = 360.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    UStaticMesh* CubeMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube")
    UMaterialInterface* CubeMaterial;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube")
    UInstancedStaticMeshComponent* InstancedMesh;

    UPROPERTY(BlueprintAssignable, Category = "MagicCube")
    FOnRotationComplete OnRotationComplete;

    UFUNCTION(BlueprintCallable, Category = "MagicCube")
    void RotateLayer(ECubeAxis Axis, int32 LayerIndex, float Degrees);

    UFUNCTION(BlueprintCallable, Category = "MagicCube")
    void Scramble(int32 Moves = 20);

    UFUNCTION(BlueprintCallable, Category = "MagicCube")
    void ResetCube();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MagicCube|TopParts")
    TArray<UStaticMesh*> TopPartMeshes;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube|TopParts")
    TArray<UStaticMeshComponent*> TopPartComponents;

    int32 GetDimensionIndex(ECubeAxis Axis) const;
    int32 GetLinearIndex(int32 x, int32 y, int32 z) const;

private:
    struct FRotationData {
        ECubeAxis Axis;
        int32 Layer;
        float RemainingDegrees;
        TArray<int32> AffectedInstances;
    };

    FRotationData CurrentRotation;
    TArray<FTransform> InitialTransforms;
    TArray<FTransform> TopPartInitialTransforms;

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
};