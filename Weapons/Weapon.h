// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapon.generated.h"

class UAnimMontage;
class ASurvivalCharacter;
class UAudioComponent;
class UParticleSystem;
class UCameraShake;
class UForceFeedbackEffect;
class USoundCue;

UENUM(BlueprintType)
enum class EWeaponState : uint8
{
	Idle,
	Firing,
	Reloading,
	Equipping
};

USTRUCT(BlueprintType)
struct FWeaponData
{
	GENERATED_USTRUCT_BODY()

	//Magazine size
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Ammo)
	int32 AmmoPerClip;

	//The item that this weapon uses as ammo
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Ammo)
	TSubclassOf<class UAmmoItem> AmmoClass;

	//The time between two consecutive shots
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = WeaponStat)
	float TimeBetweenShots;

	/** defaults **/
	FWeaponData() 
	{
		AmmoPerClip = 20;
		TimeBetweenShots = 0.2f;
	}
};

USTRUCT()
struct FWeaponAnim
{
	GENERATED_USTRUCT_BODY()

	/* animation played on pawn (1st person view) */
	UPROPERTY(EditDefaultsOnly, Category = Animation)
	UAnimMontage* Pawn1P;

	/* animation played on pawn (3rd person view) */
	UPROPERTY(EditDefaultsOnly, Category = Animation)
	UAnimMontage* Pawn3P;
};

USTRUCT(BlueprintType)
struct FHitScanConfiguration
{
	GENERATED_BODY()

	FHitScanConfiguration()
	{
		Distance = 10000.f;
		Damage = 25.f;
		Radius = 0.f;
		DamageType = UDamageType::StaticClass();
	}

	/* A map of bone -> damage amount.  If the bone is a child of the given bond, it will use this damage amount.
	A value of 2 would mean double damage etc. */
	UPROPERTY(EditDefaultsOnly, Category = "Trace Info")
	TMap<FName, float> BoneDamageModifiers;

	/* How far the hitscan traces for a hit */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trace Info")
	float Distance;

	/* The amount of damage to deal when we hit a player with the hitscan */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trace Info")
	float Damage;

	/* Optional trace radius.  A value of zero is just a linetrace, anything higher is a spheretrace */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trace Info")
	float Radius;

	/* type of damage */
	UPROPERTY(EditDefaultsOnly, Category = WeaponStat)
	TSubclassOf<UDamageType> DamageType;

};

UCLASS()
class SURVIVALGAME_API AWeapon : public AActor
{
	GENERATED_BODY()

	friend class ASurvivalCharacter;
	
public:	
	// Sets default values for this actor's properties
	AWeapon();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Destroyed() override;

protected:
	/* consume a bullet */
	void UseClipAmmo();

	/* consume ammo from the inventory */
	void ConsumeAmmo(const int32 Amount);

	/* [server] return ammo to the inventory when the weapon is unequipped */
	void ReturnAmmoToInventory();

	/* weapon is being equipped by owner pawn */
	virtual void OnEquip();

	/* weapon is now equipped by owner pawn */
	virtual void OnEquipFinished();

	/* weapon is holstered by owner pawn */
	virtual void OnUnEquip();

	/* check if it's currently equipped */
	bool IsEquipped() const;

	/* check if mesh is already attached */
	bool IsAttachedToPawn() const;

	///////////////////////////////////////////////
	// Input

	/* [local + server] start weapon fire */
	virtual void StartFire();

	/* [local + server] stop weapon fire */
	virtual void StopFire();

	/* [all] start weapon reload */
	virtual void StartReload(bool bFromReplication = false);

	/* [local + server] interrupt weapon reload */
	virtual void StopReload();

	/* [server] performs actual reload */
	virtual void ReloadWeapon();

	/* trigger reload from server */
	UFUNCTION(Reliable, client)
	void ClientStartReload();

	bool CanFire() const;
	bool CanReload() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	EWeaponState GetCurrentState() const;

	/* get current ammo amount (total) */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetCurrentAmmo() const;

	/* get current ammo amount (clip) */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetCurrentAmmoInClip() const;

	/* get clip size */
	int32 GetAmmoPerClip() const;

	/* get weapon mesh (needs pawn owner to determine variant) */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	USkeletalMeshComponent* GetWeaponMesh() const;

	/* get pawn owner */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	class ASurvivalCharacter* GetPawnOwner() const;

	/* set the weapon's owning pawn */
	void SetPawnOwner(ASurvivalCharacter* SurvivalCharacter);

	/* gets last time when this weapon was switched to */
	float GetEquipStartedTime() const;

	/* gets the duration of equipping weapon */
	float GetEquipDuration() const;

protected:
	//The weapon item in the players inventory
	UPROPERTY(Replicated, BlueprintReadOnly, Transient)
	class UWeaponItem* Item;

	/* pawn owner */
	UPROPERTY(Transient, ReplicatedUsing = OnRep_PawnOwner)
	class ASurvivalCharacter* PawnOwner;

	/* weapon data */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Config)
	FWeaponData WeaponConfig;

	/* Line trace data.  Will be used if projectile class is null */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Config)
	FHitScanConfiguration HitScanConfig;

public:
	/* weapon mesh */
	UPROPERTY(EditAnywhere, Category = Components)
	USkeletalMeshComponent* WeaponMesh;

protected:
	/* Adjustment to handle frame rate affecting actual timer interval. */
	UPROPERTY(Transient)
	float TimerIntervalAdjustment;

	/* Whether to allow automatic weapons to catch up with shorter refire cycles */
	UPROPERTY(Config)
	bool bAllowAutomaticCatchup = true;
	
	/* firing audio (bLoopedFireSound set) */
	UPROPERTY(Transient)
	UAudioComponent* FireAC;

	/* name of bone/socket for muzzle in weapon mesh */
	UPROPERTY(EditDefaultsOnly, Category = Effects)
	FName MuzzleAttachPoint;

	/* Name of the socket to attach to the character on */
	UPROPERTY(EditDefaultsOnly, Category = Effects)
	FName AttachSocket1P;

	/* Name of the socket to attach to the character on */
	UPROPERTY(EditDefaultsOnly, Category = Effects)
	FName AttachSocket3P;
	
	/* FX for muzzle flash */
	UPROPERTY(EditDefaultsOnly, Category = Effects)
	UParticleSystem* MuzzleFX;

	/* Spawned component for muzzle FX */
	UPROPERTY(Transient)
	UParticleSystemComponent* MuzzlePSC;

	/* Spawned component for second muzzle FX (Needed for split screen) */
	UPROPERTY(Transient)
	UParticleSystemComponent* MuzzlePSCSecondary;

	/* Camera shake on firing */
	UPROPERTY(EditDefaultsOnly, Category = Effects)
	TSubclassOf<UCameraShake> FireCameraShake;

	//The time it takes to aim down the sights, in seconds
	UPROPERTY(EditDefaultsOnly, Category = Weapon)
	float ADSTime;

	/* The amount of recoil to apply.  We choose a random point from 0-1 on the curve and use it to drive recoil.
	This means designers get lots of control over the recoil pattern. */
	UPROPERTY(EditDefaultsOnly, Category = Recoil)
	class UCurveVector* RecoilCurve;

	//The speed at which the recoil bumps up per second
	UPROPERTY(EditDefaultsOnly, Category = Recoil)
	float RecoilSpeed;

	//The speed at which the recoil resets per second
	UPROPERTY(EditDefaultsOnly, Category = Recoil)
	float RecoilResetSpeed;

	/* force feedback effect to play when the weapon is fired */
	UPROPERTY(EditDefaultsOnly, Category = Effects)
	UForceFeedbackEffect* FireForceFeedback;

	/* single fire sound (bLoopedFireSound not set) */
	UPROPERTY(EditDefaultsOnly, Category = Sound)
	USoundCue* FireSound;

	/* looped fire sound (bLoopedFireSound set) */
	UPROPERTY(EditDefaultsOnly, Category = Sound)
	USoundCue* FireLoopSound;

	/* finished burst sound (bLoopedFiredSound set) */
	UPROPERTY(EditDefaultsOnly, Category = Sound)
	USoundCue* FireFinishedSound;

	/* out of ammo sound */
	UPROPERTY(EditDefaultsOnly, Category = Sound)
	USoundCue* OutOfAmmoSound;

	/* reload sound */
	UPROPERTY(EditDefaultsOnly, Category = Sound)
	USoundCue* ReloadSound;

	/* reload animations */
	UPROPERTY(EditDefaultsOnly, Category = Animation)
	FWeaponAnim ReloadAnim;

	/* equip sound */
	UPROPERTY(EditDefaultsOnly, Category = Sound)
	USoundCue* EquipSound;

	/* equip animations */
	UPROPERTY(EditDefaultsOnly, Category = Animation)
	FWeaponAnim EquipAnim;

	/* fire animations */
	UPROPERTY(EditDefaultsOnly, Category = Animation)
	FWeaponAnim FireAnim;

	/* fire animations */
	UPROPERTY(EditDefaultsOnly, Category = Animation)
	FWeaponAnim FireAimingAnim;

	/* is muzzle FX looped? */
	UPROPERTY(EditDefaultsOnly, Category = Effects)
	uint32 bLoopedMuzzleFX : 1;

	/* is fire sound looped? */
	UPROPERTY(EditDefaultsOnly, Category = Sound)
	uint32 bLoopedFireSound: 1;

	/* is fire animation looped */
	UPROPERTY(EditDefaultsOnly, Category = Animation)
	uint32 bLoopedFireAnim : 1;

	/* is fire animation playing? */
	uint32 bPlayingFireAnim : 1;

	/* is weapon currently equipped */
	uint32 bIsEquipped : 1;

	/* is weapon fire active */
	uint32 bWantsToFire : 1;

	/* is reload animation playing? */
	UPROPERTY(Transient, ReplicatedUsing = OnRep_Reload)
	uint32 bPendingReload : 1;

	/* is equip animation playing */
	uint32 bPendingEquip : 1;

	/* weapon refiring */
	uint32 bRefiring;

	/* current weapon state */
	EWeaponState CurrentState;

	/* time of last successful weapon fire */
	float LastFireTime;

	/* last time when this weapon was switched to */
	float EquipStartedTime;

	/* how much time weapon needs to be equipped */
	float EquipDuration;

	/* current ammo inside the magazine */
	UPROPERTY(Transient, Replicated)
	int32 CurrentAmmoInClip;

	/* bust counter, used for replicating fire events to remote clients */
	UPROPERTY(Transient, ReplicatedUsing = OnRep_BurstCounter)
	int32 BurstCounter;

	/* Handle for efficient manager of OnEquippedFinished timer */
	FTimerHandle TimerHandle_OnEquipFinished;

	/* Handle for efficient manager of StopReload timer */
	FTimerHandle TimerHandle_StopReload;

	/* Handle for efficient management of ReloadWeapon timer */
	FTimerHandle TimerHandle_ReloadWeapon;

	/* Handle for efficient management of HandleFiring timer */
	FTimerHandle TimerHandle_HandleFiring;

	////////////////////////////////////////////////////////////
	// Input - server side

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerStartFire();

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerStopFire();

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerStartReload();

	UFUNCTION(Reliable, Server, WithValidation)
	void ServerStopReload();

	////////////////////////////////////////////////////////////
	// Replication & effects

	UFUNCTION()
	void OnRep_PawnOwner();

	UFUNCTION()
	void OnRep_BurstCounter();

	UFUNCTION()
	void OnRep_Reload();

	/* Called in network play to do the cosmetic FX for firing */
	virtual void SimulateWeaponFire();

	/* Called in network play to stop cosmetic FS (e.g. for a looping shot). */
	virtual void StopSimulatingWeaponFire();

	///////////////////////////////////////////////////////
	//// Weapon usage

	/* Hand hit locally before asking server to process hit */
	void HandleHit(const FHitResult& Hit, class ASurvivalCharacter* HitPlayer = nullptr);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerHandleHit(const FHitResult& Hit, class ASurvivalCharacter* HitPlayer = nullptr);

	/* [local] weapon specific fire implementation */
	virtual void FireShot();

	/* [server] fire & update ammo */
	UFUNCTION(Reliable, Server, WithValidation)
	void ServerHandleFiring();

	/* [local + server] handle weapon refire, compensating for slack time if the timer can't sample fast enough  */
	void HandleReFiring();

	/* [local + server] handle weapon fire */
	void HandleFiring();

	/* [local + server] firing started */
	virtual void OnBurstStarted();

	/* [local + server] firing finished */
	virtual void OnBurstFinished();

	/* updated weapon state */
	void SetWeaponState(EWeaponState NewState);

	/* determine current weapon state */
	void DetermineWeaponState();

	/* attaches weapon mesh to pawn's mesh */
	void AttachMeshToPawn();

	///////////////////////////////////////////////////////
	//// Weapon usage helpers

	/* play weapon sounds */
	UAudioComponent* PlayWeaponSound(USoundCue* Sound);

	/* play weapon animations */
	float PlayWeaponAnimation(const FWeaponAnim& Animation);

	/* stop playing weapon animations */
	void StopWeaponAnimation(const FWeaponAnim& Animation);

	/* Get the aim of the camera */
	FVector GetCameraAim() const;

	/* find hit */
	FHitResult WeaponTrace(const FVector& StartTrace, const FVector& EndTrace) const;
};
