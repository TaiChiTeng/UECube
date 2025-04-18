#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

// 前向声明枚举类和魔方Actor类，避免直接包含 MagicCubeActor.h 带来的耦合
enum class ECubeAxis : uint8;
class AMagicCubeActor;

#include "ACustomPawn.generated.h"

UCLASS()
class FASTUEC_API ACustomPawn : public APawn
{
	GENERATED_BODY()

public:
	ACustomPawn();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaTime) override;

	// 摄像机控制函数
	void OnRightMousePressed();
	void OnRightMouseReleased();
	void Turn(float AxisValue);
	void LookUp(float AxisValue);

	// 左键拖拽交互函数
	void OnLeftMousePressedCube();
	void OnLeftMouseReleasedCube();

	// 摄像机相关
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	bool bIsRotating;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float MinPitch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float MaxPitch;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	USpringArmComponent* SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	UCameraComponent* Camera;

	//////////////////////////////////////////////////////////////////////////
	// 魔方旋转交互所需变量
	//////////////////////////////////////////////////////////////////////////
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube|Interaction")
	bool bIsCubeDrag;

	// 鼠标按下时的屏幕坐标
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube|Interaction")
	FVector2D CubeDragStartScreen;

	// 记录选中块在 InstancedMesh 中的索引
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube|Interaction")
	int32 SelectedCubeInstanceIndex;

	// 记录选中块相对于魔方中心的局部位置
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube|Interaction")
	FVector SelectedBlockLocalPos;

	// 候选面集合（例如："上面", "下面", "左面", "右面", "前面", "后面"，以及扩展的"非上非下面"、"非左非右面"、"非前非后面"）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube|Interaction")
	TArray<FString> SelectedCandidateFaces;
	
	// 用于记录射线碰撞面（调试用）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube|Interaction")
	FString SelectedHitPlane;
	
	// 记录射线命中的法向量，用于判断碰撞面
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube|Interaction")
	FVector HitNormal;

	// 连续拖拽时保存当前状态
	FString CurrentTargetFace;        // 固定的目标面
	ECubeAxis CurrentRotateAxis;        // 当前旋转轴
	int32 CurrentLayerIndex;            // 当前旋转层索引
	float CurrentDragAngle;             // 当前拖拽计算出的旋转角度

	// 新增：记录当前被操作的魔方 Actor，支持场景中多个魔方 Actor 正常交互
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MagicCube|Interaction")
	AMagicCubeActor* SelectedCubeActor;
};