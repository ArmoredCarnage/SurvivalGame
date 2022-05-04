// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "WheeledVehicle.h"
#include "SurvivalVehicle.generated.h"

/**
 * 
 */
UCLASS()
class SURVIVALGAME_API ASurvivalVehicle : public AWheeledVehicle
{
	GENERATED_BODY()

public:
	ASurvivalVehicle();

	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/* Throttle / Steering */
	void ApplyThrottle(float Val);
	void ApplySteering(float Val);

	/* Look around */
	void LookUp(float Val);
	void Turn(float Val);

	/* Handbrake */
	void OnHandbrakePressed();
	void OnHandbrakeReleased();

	/* Update in air physics */
	void UpdateInAirControl(float DeltaTime);

	FText InteractionNameText = FText::FromString("Pickup Truck");
	FText InteractionActionText = FText::FromString("Drive the truck.");
	float InteractionTime = 0.5f;

protected:
	/* Spring arm that will offset the camera */
	UPROPERTY(Category = Camera, EditDefaultsOnly, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class USpringArmComponent* SpringArm;

	/* Camera component that will be our viewpoint */
	UPROPERTY(Category = Camera, EditDefaultsOnly, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UCameraComponent* Camera;

	UPROPERTY(EditDefaultsOnly)
	class UInteractionComponent* InteractionComponent;

	UPROPERTY(ReplicatedUsing = OnRep_Driver)
	class ASurvivalCharacter* Driver;

	UFUNCTION()
	void OnRep_Driver();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
