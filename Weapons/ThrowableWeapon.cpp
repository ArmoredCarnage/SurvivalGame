// Fill out your copyright notice in the Description page of Project Settings.


#include "ThrowableWeapon.h"
#include <GameFramework/ProjectileMovementComponent.h>
#include <Components/StaticMeshComponent.h>

// Sets default values
AThrowableWeapon::AThrowableWeapon()
{
    ThrowableMesh = CreateDefaultSubobject<UStaticMeshComponent>("ThrowableMesh");
    SetRootComponent(ThrowableMesh);

    ThrowableMovement = CreateDefaultSubobject<UProjectileMovementComponent>("ThrowableMovement");
    ThrowableMovement->InitialSpeed = 1000.0f;

    SetReplicates(true);
    SetReplicatingMovement(true);
}

