// Fill out your copyright notice in the Description page of Project Settings.


#include "SurvivalVehicle.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "WheeledVehicleMovementComponent4W.h"
#include <Kismet/GameplayStatics.h>
#include "../Components/InteractionComponent.h"
#include "../Player/SurvivalCharacter.h"
#include "Net/UnrealNetwork.h"

static const FName NAME_SteerInput("Steer");
static const FName NAME_ThrottleInput("Throttle");

ASurvivalVehicle::ASurvivalVehicle()
{
	UWheeledVehicleMovementComponent4W* Vehicle4W = CastChecked<UWheeledVehicleMovementComponent4W>(GetVehicleMovement());

	// Adjust the tire loading
	Vehicle4W->MinNormalizedTireLoad = 0.0f;
	Vehicle4W->MinNormalizedTireLoadFiltered = 0.2f;
	Vehicle4W->MaxNormalizedTireLoad = 2.0f;
	Vehicle4W->MaxNormalizedTireLoadFiltered = 2.0f;

	// Torque setup
	Vehicle4W->MaxEngineRPM = 5700.0f;
	Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->Reset();
	Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(0.0f, 400.0f);
	Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(1890.0f, 500.0f);
	Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(5730.0f, 400.0f);

	// Adjust the steering
	Vehicle4W->SteeringCurve.GetRichCurve()->Reset();
	Vehicle4W->SteeringCurve.GetRichCurve()->AddKey(0.0f, 1.0f);
	Vehicle4W->SteeringCurve.GetRichCurve()->AddKey(40.0f, 0.7f);
	Vehicle4W->SteeringCurve.GetRichCurve()->AddKey(120.0f, 0.6f);

	Vehicle4W->DifferentialSetup.DifferentialType = EVehicleDifferential4W::LimitedSlip_4W;
	Vehicle4W->DifferentialSetup.FrontRearSplit = 0.65;

	// Automatic gearbox
	Vehicle4W->TransmissionSetup.bUseGearAutoBox = true;
	Vehicle4W->TransmissionSetup.GearSwitchTime = 0.15f;
	Vehicle4W->TransmissionSetup.GearAutoBoxLatency = 1.0f;

	// Create a spring arm component for our chase camera
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 250.0f;
	SpringArm->bUsePawnControlRotation = true;

	// Create the chase camera component
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->FieldOfView = 90.f;

	InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
	InteractionComponent->SetInteractableNameText(InteractionNameText);
	InteractionComponent->SetInteractableActionText(InteractionActionText);
	InteractionComponent->InteractionTime = InteractionTime;

}

void ASurvivalVehicle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateInAirControl(DeltaTime);
}

void ASurvivalVehicle::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis("Throttle", this, &ASurvivalVehicle::ApplyThrottle);
	PlayerInputComponent->BindAxis("Steer", this, &ASurvivalVehicle::ApplySteering);
	PlayerInputComponent->BindAxis("LookUpVehicle", this, &ASurvivalVehicle::LookUp);
	PlayerInputComponent->BindAxis("TurnVehicle", this, &ASurvivalVehicle::Turn);

	PlayerInputComponent->BindAction("Handbrake", IE_Pressed, this, &ASurvivalVehicle::OnHandbrakePressed);
	PlayerInputComponent->BindAction("Handbrake", IE_Released, this, &ASurvivalVehicle::OnHandbrakeReleased);
}

void ASurvivalVehicle::ApplyThrottle(float Val)
{
	GetVehicleMovementComponent()->SetThrottleInput(Val);
}

void ASurvivalVehicle::ApplySteering(float Val)
{
	GetVehicleMovementComponent()->SetSteeringInput(Val);
}

void ASurvivalVehicle::LookUp(float Val)
{
	if (Val != 0.f)
	{
		AddControllerPitchInput(Val);
	}
}

void ASurvivalVehicle::Turn(float Val)
{
	if (Val != 0.f)
	{
		AddControllerYawInput(Val);
	}
}

void ASurvivalVehicle::OnHandbrakePressed()
{
	GetVehicleMovementComponent()->SetHandbrakeInput(true);
}

void ASurvivalVehicle::OnHandbrakeReleased()
{
	GetVehicleMovementComponent()->SetHandbrakeInput(false);
}

void ASurvivalVehicle::UpdateInAirControl(float DeltaTime)
{
	if (UWheeledVehicleMovementComponent* Vehicle4W = CastChecked<UWheeledVehicleMovementComponent>(GetVehicleMovement()))
	{
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		const FVector TraceStart = GetActorLocation() + FVector(0.f, 0.f, 50.f);
		const FVector TraceEnd = GetActorLocation() + FVector(0.f, 0.f, 200.f);

		FHitResult Hit;

		//Check if car is flipped on its side, and check if the car is in the air
		const bool bInAir = !GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
		const bool bNotGrounded = FVector::DotProduct(GetActorUpVector(), FVector::UpVector) < 0.1f;

		//Only allow in air-movement if we are not on the ground, or are in the air
		if (bInAir || bNotGrounded)
		{
			if (InputComponent)
			{
				const float ForwardInput = InputComponent->GetAxisValue(TEXT("Steer"));
				const float RightInput = InputComponent->GetAxisValue(TEXT("Throttle"));

				//In car is grounded allow player to roll the car over
				const float AirMovementForcePitch = 3.f;
				const float AirMovementForceRoll = !bInAir && bNotGrounded ? 20.f : 3.f;

				if (UPrimitiveComponent* VehicleMesh = Vehicle4W->UpdatedPrimitive)
				{
					const FVector MovementVector = FVector(RightInput * -AirMovementForceRoll, ForwardInput * AirMovementForcePitch, 0.f) * DeltaTime * 200.f;
					const FVector NewAngularMovement = GetActorRotation().RotateVector(MovementVector);

					VehicleMesh->SetPhysicsAngularVelocityInDegrees(NewAngularMovement, true);
				}
			}	
		}
	}
}

void ASurvivalVehicle::OnRep_Driver()
{

}

void ASurvivalVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASurvivalVehicle, Driver);
}

