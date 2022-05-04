// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon.h"
#include "../SurvivalGame.h"
#include "../Player/SurvivalPlayerController.h"
#include "../Player/SurvivalCharacter.h"
#include <Components/SkeletalMeshComponent.h>
#include "Components/AudioComponent.h"
#include "../Components/InventoryComponent.h"
#include "Curves/CurveVector.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundCue.h"
#include "Net/UnrealNetwork.h"
#include "../Items/EquippableItem.h"
#include "../Items/AmmoItem.h"
#include "DrawDebugHelpers.h"
#include "Camera/CameraShake.h"

// Sets default values
AWeapon::AWeapon()
{
	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->bReceivesDecals = false;
	WeaponMesh->SetCollisionObjectType(ECC_WorldDynamic);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	RootComponent = WeaponMesh;

	bLoopedMuzzleFX = false;
	bLoopedFireAnim = false;
	bPlayingFireAnim = false;
	bIsEquipped = false;
	bWantsToFire = false;
	bPendingReload = false;
	bPendingEquip = false;
	CurrentState = EWeaponState::Idle;
	AttachSocket1P = FName("GripPoint");
	AttachSocket3P = FName("GripPoint");

	CurrentAmmoInClip = 0;
	BurstCounter = 0;
	LastFireTime = 0.0f;

	ADSTime = 0.5f;
	RecoilResetSpeed = 5.0f;
	RecoilSpeed = 10.0f;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	bNetUseOwnerRelevancy = true;
}

void AWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWeapon, PawnOwner);
	DOREPLIFETIME_CONDITION(AWeapon, CurrentAmmoInClip, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AWeapon, BurstCounter, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AWeapon, bPendingReload, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AWeapon, Item, COND_InitialOnly);
}

void AWeapon::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}

// Called when the game starts or when spawned
void AWeapon::BeginPlay()
{
	Super::BeginPlay();
	
	if (HasAuthority())
	{
		PawnOwner = Cast<ASurvivalCharacter>(GetOwner());
	}
}

void AWeapon::Destroyed()
{
	Super::Destroyed();

	StopSimulatingWeaponFire();
}

void AWeapon::UseClipAmmo()
{
	if (HasAuthority())
	{
		--CurrentAmmoInClip;
	}
}

void AWeapon::ConsumeAmmo(const int32 Amount)
{
	if (HasAuthority() && PawnOwner)
	{
		if (UInventoryComponent* Inventory = PawnOwner->PlayerInventory)
		{
			if (UItem* AmmoItem = Inventory->FindItemByClass(WeaponConfig.AmmoClass))
			{
				Inventory->TryAddItemFromClass(WeaponConfig.AmmoClass, CurrentAmmoInClip);
			}
		}
	}
}

void AWeapon::ReturnAmmoToInventory()
{
	//When the weapon is unequipped, try to return the players ammo to their inventory
	if (HasAuthority())
	{
		if (PawnOwner && CurrentAmmoInClip > 0)
		{
			if (UInventoryComponent* Inventory = PawnOwner->PlayerInventory)
			{
				Inventory->TryAddItemFromClass(WeaponConfig.AmmoClass, CurrentAmmoInClip);
			}
		}
	}
}

void AWeapon::OnEquip()
{
	AttachMeshToPawn();

	bPendingEquip = true;
	DetermineWeaponState();

	OnEquipFinished();

	if (PawnOwner && PawnOwner->IsLocallyControlled())
	{
		PlayWeaponSound(EquipSound);
	}
}

void AWeapon::OnEquipFinished()
{
	AttachMeshToPawn();

	bIsEquipped = true;
	bPendingEquip = false;

	//Determine the state so that the can reload checks will work
	DetermineWeaponState();

	if (PawnOwner)
	{
		//Try to reload empty clip
		if (PawnOwner->IsLocallyControlled() && CurrentAmmoInClip <= 0 && CanReload())
		{
			StartReload();
		}
	}
}

void AWeapon::OnUnEquip()
{
	bIsEquipped = false;
	StopFire();

	if (bPendingReload)
	{
		StopWeaponAnimation(ReloadAnim);
		bPendingReload = false;

		GetWorldTimerManager().ClearTimer(TimerHandle_StopReload);
		GetWorldTimerManager().ClearTimer(TimerHandle_ReloadWeapon);
	}

	if (bPendingEquip)
	{
		StopWeaponAnimation(EquipAnim);
		bPendingEquip = false;

		GetWorldTimerManager().ClearTimer(TimerHandle_OnEquipFinished);
	}

	ReturnAmmoToInventory();
	DetermineWeaponState();
}

bool AWeapon::IsEquipped() const
{
	return bIsEquipped;
}

bool AWeapon::IsAttachedToPawn() const
{
	return bIsEquipped || bPendingEquip;
}

void AWeapon::StartFire()
{
	if (GetLocalRole() < ROLE_Authority)
	{
		ServerStartFire();
	}

	if (!bWantsToFire)
	{
		bWantsToFire = true;
		DetermineWeaponState();
	}
}

void AWeapon::StopFire()
{
	if ((GetLocalRole() < ROLE_Authority) && PawnOwner && PawnOwner->IsLocallyControlled())
	{
		ServerStopFire();
	}

	if (bWantsToFire)
	{
		bWantsToFire = false;
		DetermineWeaponState();
	}
}

void AWeapon::StartReload(bool bFromReplication /*= false*/)
{
	if (!bFromReplication && GetLocalRole() < ROLE_Authority)
	{
		ServerStartReload();
	}

	if (bFromReplication || CanReload())
	{
		bPendingReload = true;
		DetermineWeaponState();

		float AnimDuration = PlayWeaponAnimation(ReloadAnim);
		if (AnimDuration <= 0.0f)
		{
			AnimDuration = 0.5f;
		}

		GetWorldTimerManager().SetTimer(TimerHandle_StopReload, this, &AWeapon::StopReload, AnimDuration, false);
		if (HasAuthority())
		{
			GetWorldTimerManager().SetTimer(TimerHandle_ReloadWeapon, this, &AWeapon::ReloadWeapon, FMath::Max(0.1f, AnimDuration - 0.1f), false);
		}

		if (PawnOwner && PawnOwner->IsLocallyControlled())
		{
			PlayWeaponSound(ReloadSound);
		}
	}
}

void AWeapon::StopReload()
{
	if (CurrentState == EWeaponState::Reloading)
	{
		bPendingReload = false;
		DetermineWeaponState();
		StopWeaponAnimation(ReloadAnim);
	}
}

void AWeapon::ReloadWeapon()
{
	const int32 ClipDelta = FMath::Min(WeaponConfig.AmmoPerClip - CurrentAmmoInClip, GetCurrentAmmo());

	if (ClipDelta > 0)
	{
		CurrentAmmoInClip += ClipDelta;
		ConsumeAmmo(ClipDelta);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Didn't have enough ammo for a reload."));
	}
}

void AWeapon::ClientStartReload_Implementation()
{
	StartReload();
}

bool AWeapon::CanFire() const
{
	bool bCanFire = PawnOwner != nullptr;
	bool bStateOKToFire = ((CurrentState == EWeaponState::Idle) || (CurrentState == EWeaponState::Firing));
	return ((bCanFire == true) && (bStateOKToFire == true) && (bPendingReload == false));
}

bool AWeapon::CanReload() const
{
	bool bCanReload = PawnOwner != nullptr;
	bool bGotAmmo = (CurrentAmmoInClip < WeaponConfig.AmmoPerClip) && (GetCurrentAmmo() > 0);
	bool bStateOKToReload = ((CurrentState == EWeaponState::Idle) || (CurrentState == EWeaponState::Firing));
	return ((bCanReload == true) && (bGotAmmo == true) && (bStateOKToReload == true));
}

EWeaponState AWeapon::GetCurrentState() const
{
	return CurrentState;
}

int32 AWeapon::GetCurrentAmmo() const
{
	if (PawnOwner)
	{
		if (UInventoryComponent* Inventory = PawnOwner->PlayerInventory)
		{
			if (UItem* Ammo = Inventory->FindItemByClass(WeaponConfig.AmmoClass))
			{
				return Ammo->GetQuantity();
			}
		}
	}

	return 0;
}

int32 AWeapon::GetCurrentAmmoInClip() const
{
	return CurrentAmmoInClip;
}

int32 AWeapon::GetAmmoPerClip() const
{
	return WeaponConfig.AmmoPerClip;
}

USkeletalMeshComponent* AWeapon::GetWeaponMesh() const
{
	return WeaponMesh;
}

class ASurvivalCharacter* AWeapon::GetPawnOwner() const
{
	return PawnOwner;
}

void AWeapon::SetPawnOwner(ASurvivalCharacter* SurvivalCharacter)
{
	if (PawnOwner != SurvivalCharacter)
	{
		SetInstigator(SurvivalCharacter);
		PawnOwner = SurvivalCharacter;
		// net owner for RPC calls
		SetOwner(SurvivalCharacter);
	}
}

float AWeapon::GetEquipStartedTime() const
{
	return EquipStartedTime;
}

float AWeapon::GetEquipDuration() const
{
	return EquipDuration;
}

void AWeapon::ServerStartFire_Implementation()
{
	StartFire();
}

bool AWeapon::ServerStartFire_Validate()
{
	return true;
}

void AWeapon::ServerStopFire_Implementation()
{
	StopFire();
}

bool AWeapon::ServerStopFire_Validate()
{
	return true;
}

void AWeapon::ServerStartReload_Implementation()
{
	StartReload();
}

bool AWeapon::ServerStartReload_Validate()
{
	return true;
}

void AWeapon::ServerStopReload_Implementation()
{
	StopReload();
}

bool AWeapon::ServerStopReload_Validate()
{
	return true;
}

void AWeapon::OnRep_PawnOwner()
{

}

void AWeapon::OnRep_BurstCounter()
{
	if (BurstCounter > 0)
	{
		SimulateWeaponFire();
	}
	else
	{
		StopSimulatingWeaponFire();
	}
}

void AWeapon::OnRep_Reload()
{
	if (bPendingReload)
	{
		StartReload();
	}
	else
	{
		StopReload();
	}
}

void AWeapon::SimulateWeaponFire()
{
	if (HasAuthority() && CurrentState != EWeaponState::Firing)
	{
		return;
	}

	if (MuzzleFX)
	{
		if (!bLoopedMuzzleFX || MuzzlePSC == nullptr)
		{
			//Split screen requires we create 2 effects.  One that we see and one that the other player sees
			if ((PawnOwner != nullptr) &&(PawnOwner->IsLocallyControlled() == true))
			{
				AController* PlayerCon = PawnOwner->GetController();
				if (PlayerCon != nullptr)
				{
					WeaponMesh->GetSocketLocation(MuzzleAttachPoint);
					MuzzlePSC = UGameplayStatics::SpawnEmitterAttached(MuzzleFX, WeaponMesh, MuzzleAttachPoint);
					MuzzlePSC->bOwnerNoSee = false;
					MuzzlePSC->bOnlyOwnerSee = true;
				}
			}
			else
			{
				MuzzlePSC = UGameplayStatics::SpawnEmitterAttached(MuzzleFX, WeaponMesh, MuzzleAttachPoint);
			}
		}
	}

	if (!bLoopedFireAnim || bPlayingFireAnim)
	{
		FWeaponAnim AnimToPlay = FireAnim; //PawnOwner->IsAiming() || PawnOwner->IsLocallyControlled() ? FireAimingAnim : FireAnim;
		PlayWeaponAnimation(FireAnim);
		bPlayingFireAnim = true;
	}

	if (bLoopedFireSound)
	{
		if (FireAC == nullptr)
		{
			FireAC = PlayWeaponSound(FireLoopSound);
		}
	}
	else
	{
		PlayWeaponSound(FireSound);
	}

	ASurvivalPlayerController* PC = (PawnOwner != nullptr) ? Cast<ASurvivalPlayerController>(PawnOwner->Controller) : nullptr;
	if (PC != nullptr && PC->IsLocalController())
	{
		if (FireCameraShake != nullptr)
		{
			PC->ClientPlayCameraShake(FireCameraShake, 1);
		}
		if (FireForceFeedback != nullptr)
		{
			FForceFeedbackParameters FFParams;
			FFParams.Tag = "Weapon";
			PC->ClientPlayForceFeedback(FireForceFeedback, FFParams);
		}
	}
}

void AWeapon::StopSimulatingWeaponFire()
{
	if (bLoopedMuzzleFX)
	{
		if (MuzzlePSC != nullptr)
		{
			MuzzlePSC->DeactivateSystem();
			MuzzlePSC = nullptr;
		}
		if (MuzzlePSCSecondary != nullptr)
		{
			MuzzlePSCSecondary->DeactivateSystem();
			MuzzlePSCSecondary = nullptr;
		}
	}

	if (bLoopedFireAnim && bPlayingFireAnim)
	{
		StopWeaponAnimation(FireAimingAnim);
		StopWeaponAnimation(FireAnim);
		bPlayingFireAnim = false;
	}

	if (FireAC)
	{
		FireAC->FadeOut(0.1f, 0.0f);
		FireAC = nullptr;

		PlayWeaponSound(FireFinishedSound);
	}
}

void AWeapon::HandleHit(const FHitResult& Hit, class ASurvivalCharacter* HitPlayer /*= nullptr*/)
{
	if (Hit.GetActor())
	{
		UE_LOG(LogTemp, Warning, TEXT("Hit actor %s"), *Hit.GetActor()->GetName());
	}

	ServerHandleHit(Hit, HitPlayer);

	if (HitPlayer && PawnOwner)
	{
		if (ASurvivalPlayerController* PC = Cast<ASurvivalPlayerController>(PawnOwner->GetController()))
		{
			PC->OnHitPlayer();
		}
	}
}

void AWeapon::ServerHandleHit_Implementation(const FHitResult& Hit, class ASurvivalCharacter* HitPlayer /*= nullptr*/)
{
	if (PawnOwner)
	{
		float DamageMultiplier = 1.0f;

		/* Certain bones like head might give extra damage if hit.  Apply those. */
		for (auto& BoneDamageModifier : HitScanConfig.BoneDamageModifiers)
		{
			DamageMultiplier = BoneDamageModifier.Value;
			break;
		}

		if (HitPlayer)
		{
			UGameplayStatics::ApplyPointDamage(HitPlayer, HitScanConfig.Damage * DamageMultiplier, (Hit.TraceStart - Hit.TraceEnd).GetSafeNormal(), Hit, PawnOwner->GetController(), this, HitScanConfig.DamageType);
		}
	}

	
}

bool AWeapon::ServerHandleHit_Validate(const FHitResult& Hit, class ASurvivalCharacter* HitPlayer /*= nullptr*/)
{
	return true;
}

void AWeapon::FireShot()
{
	if (PawnOwner)
	{
		if (ASurvivalPlayerController* PC = Cast<ASurvivalPlayerController>(PawnOwner->GetController()))
		{
			if (RecoilCurve)
			{
				const FVector2D RecoilAmount(RecoilCurve->GetVectorValue(FMath::RandRange(0.f, 1.f)).X, RecoilCurve->GetVectorValue(FMath::RandRange(0.f, 1.f)).Y);
				PC->ApplyRecoil(RecoilAmount, RecoilSpeed, RecoilResetSpeed, FireCameraShake);
			}

			FVector CamLoc;
			FRotator CamRot;
			PC->GetPlayerViewPoint(CamLoc, CamRot);

			FHitResult Hit;
			FCollisionQueryParams QueryParams;
			QueryParams.AddIgnoredActor(this);
			QueryParams.AddIgnoredActor(PawnOwner);

			FVector FireDir = CamRot.Vector();//PawnOwner->IsAiming() ? CamRot.Vector() : FMath::VRandCone(CamRot.Vector(), FMath::DegreesToRadians(PawnOwner->IsAiming() ? 0.f : 5.f));
			FVector TraceStart = CamLoc;
			FVector TraceEnd = (FireDir * HitScanConfig.Distance) + CamLoc;

			if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, COLLISION_WEAPON, QueryParams))
			{
				ASurvivalCharacter* HitChar = Cast<ASurvivalCharacter>(Hit.GetActor());

				HandleHit(Hit, HitChar);

				FColor PointColor = FColor::Red;
				DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 5.f, PointColor, false, 30.f);
			}
		}
	}
}

void AWeapon::HandleReFiring()
{
	UWorld* MyWorld = GetWorld();

	float SlackTimeThisFrame = FMath::Max(0.0f, (MyWorld->TimeSeconds - LastFireTime) - WeaponConfig.TimeBetweenShots);

	if (bAllowAutomaticCatchup)
	{
		TimerIntervalAdjustment -= SlackTimeThisFrame;
	}

	HandleFiring();
}

void AWeapon::HandleFiring()
{
	if ((CurrentAmmoInClip>0) && CanFire())
	{
		if (GetNetMode() != NM_DedicatedServer)
		{
			SimulateWeaponFire();
		}

		if (PawnOwner && PawnOwner->IsLocallyControlled())
		{
			FireShot();
			UseClipAmmo();

			// update firing FX on remote clients if function was called on server
			BurstCounter++;
		}
	}
	else if (CanReload())
	{
		StartReload();
	}
	else if (PawnOwner && PawnOwner->IsLocallyControlled())
	{
		if (GetCurrentAmmo() == 0 && !bRefiring)
		{
			PlayWeaponSound(OutOfAmmoSound);
			ASurvivalPlayerController* MyPC = Cast<ASurvivalPlayerController>(PawnOwner->Controller);
		}
	}

	// stop weapon fire FX, but stay in firing state
	if (BurstCounter > 0)
	{
		OnBurstFinished();
	}

	if (PawnOwner && PawnOwner->IsLocallyControlled())
	{
		// local client will notify server
		if (GetLocalRole() < ROLE_Authority)
		{
			ServerHandleFiring();
		}

		// reload after firing last round
		if (CurrentAmmoInClip <= 0 && CanReload())
		{
			StartReload();
		}

		// setup refire timer
		bRefiring = (CurrentState == EWeaponState::Firing && WeaponConfig.TimeBetweenShots > 0.0f);
		if (bRefiring)
		{
			GetWorldTimerManager().SetTimer(TimerHandle_HandleFiring, this, &AWeapon::HandleReFiring, FMath::Max<float>(WeaponConfig.TimeBetweenShots + TimerIntervalAdjustment, SMALL_NUMBER), false);
			TimerIntervalAdjustment = 0.f;
		}
	}

	LastFireTime = GetWorld()->GetTimeSeconds();
}

void AWeapon::OnBurstStarted()
{
	// start firing, can be delayed to satisfy TimeBetweenShots
	const float GameTime = GetWorld()->GetTimeSeconds();
	if (LastFireTime > 0 && WeaponConfig.TimeBetweenShots > 0.0f && LastFireTime + WeaponConfig.TimeBetweenShots > GameTime)
	{
		GetWorldTimerManager().SetTimer(TimerHandle_HandleFiring, this, &AWeapon::HandleFiring, LastFireTime + WeaponConfig.TimeBetweenShots - GameTime, false);
	}
	else
	{
		HandleFiring();
	}
}

void AWeapon::OnBurstFinished()
{
	// stop firing FX on remote clients
	BurstCounter = 0;

	// stop firing FX locally, unless it's a dedicated server
	if (GetNetMode() != NM_DedicatedServer)
	{
		StopSimulatingWeaponFire();
	}

	GetWorldTimerManager().ClearTimer(TimerHandle_HandleFiring);
	bRefiring = false;

	// reset firing interval adjustment
	TimerIntervalAdjustment = 0.0f;
}

void AWeapon::SetWeaponState(EWeaponState NewState)
{
	const EWeaponState PrevState = CurrentState;

	if (PrevState == EWeaponState::Firing && NewState != EWeaponState::Firing)
	{
		OnBurstFinished();
	}

	CurrentState = NewState;

	if (PrevState != EWeaponState::Firing && NewState == EWeaponState::Firing)
	{
		OnBurstStarted();
	}
}

void AWeapon::DetermineWeaponState()
{
	EWeaponState NewState = EWeaponState::Idle;

	if (bIsEquipped)
	{
		if (bPendingReload)
		{
			if (CanReload() == false)
			{
				NewState = CurrentState;
			}
			else
			{
				NewState = EWeaponState::Reloading;
			}
		}
		else if ((bPendingReload == false) && (bWantsToFire == true) && (CanFire() == true))
		{
			NewState = EWeaponState::Firing;
		}
	}
	else if (bPendingEquip)
	{
		NewState = EWeaponState::Equipping;
	}

	SetWeaponState(NewState);
}

void AWeapon::AttachMeshToPawn()
{
	if (PawnOwner)
	{
		if (const USkeletalMeshComponent* PawnMesh = PawnOwner->GetMesh())
		{
			const FName AttachSocket = PawnOwner->IsLocallyControlled() ? AttachSocket1P : AttachSocket3P;
			AttachToComponent(PawnOwner->GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, AttachSocket);
		}
	}
}

UAudioComponent* AWeapon::PlayWeaponSound(USoundCue* Sound)
{
	UAudioComponent* AC = nullptr;
	if (Sound && PawnOwner)
	{
		AC = UGameplayStatics::SpawnSoundAttached(Sound, PawnOwner->GetRootComponent());
	}

	return AC;
}

float AWeapon::PlayWeaponAnimation(const FWeaponAnim& Animation)
{
	float Duration = 0.0f;
	if (PawnOwner)
	{
		UAnimMontage* UseAnim = PawnOwner->IsLocallyControlled() ? Animation.Pawn1P : Animation.Pawn3P;
		if (UseAnim)
		{
			Duration = PawnOwner->PlayAnimMontage(UseAnim);
		}
	}

	return Duration;
}

void AWeapon::StopWeaponAnimation(const FWeaponAnim& Animation)
{
	if (PawnOwner)
	{
		UAnimMontage* UseAnim = PawnOwner->IsLocallyControlled() ? Animation.Pawn1P : Animation.Pawn3P;
		if (UseAnim)
		{
			PawnOwner->StopAnimMontage(UseAnim);
		}
	}
}

FVector AWeapon::GetCameraAim() const
{
	ASurvivalPlayerController* const PlayerController = GetInstigator() ? Cast<ASurvivalPlayerController>(GetInstigator()->Controller) : nullptr;
	FVector FinalAim = FVector::ZeroVector;

	if (PlayerController)
	{
		FVector CamLoc;
		FRotator CamRot;
		PlayerController->GetPlayerViewPoint(CamLoc, CamRot);
		FinalAim = CamRot.Vector();
	}
	else if (GetInstigator())
	{
		FinalAim = GetInstigator()->GetBaseAimRotation().Vector();
	}

	return FinalAim;
}

FHitResult AWeapon::WeaponTrace(const FVector& StartTrace, const FVector& EndTrace) const
{
	// Performs trade to retrieve hit info
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(WeaponTrace), true, GetInstigator());
	TraceParams.bReturnPhysicalMaterial = true;

	FHitResult Hit(ForceInit);
	GetWorld()->LineTraceSingleByChannel(Hit, StartTrace, EndTrace, COLLISION_WEAPON, TraceParams);

	return Hit;
}

void AWeapon::ServerHandleFiring_Implementation()
{
	const bool bShouldUpdateAmmo = (CurrentAmmoInClip > 0 && CanFire());

	HandleFiring();

	if (bShouldUpdateAmmo)
	{
		// update ammo
		UseClipAmmo();

		// update firing FX on remote clients
		BurstCounter++;
	}
}

bool AWeapon::ServerHandleFiring_Validate()
{
	return true;
}