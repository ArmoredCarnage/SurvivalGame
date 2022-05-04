// Fill out your copyright notice in the Description page of Project Settings.


#include "ItemSpawn.h"
#include "../World/PickUp.h"
#include "../Items/Item.h"

AItemSpawn::AItemSpawn()
{
	PrimaryActorTick.bCanEverTick = false;
	bNetLoadOnClient = false;

	RespawnRange = FIntPoint(10, 30);
}

void AItemSpawn::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		SpawnItem();
	}
}

void AItemSpawn::SpawnItem()
{
	if (HasAuthority() && LootTable)
	{
		TArray<FLootTableRow*> SpawnItems;
		LootTable->GetAllRows("", SpawnItems);
		
		const FLootTableRow* LootRow = SpawnItems[FMath::RandRange(0, SpawnItems.Num() - 1)];

		ensure(LootRow);

		float ProbabilityRoll = FMath::FRandRange(0.0f, 1.0f);

		while (ProbabilityRoll > LootRow->Probability)
		{
			LootRow = SpawnItems[FMath::RandRange(0, SpawnItems.Num() - 1)];
			ProbabilityRoll = FMath::FRandRange(0.0f, 1.0f);
		}

		if (LootRow && LootRow->Items.Num() && PickupClass)
		{
			float Angle = 0.0f;

			for (auto& ItemClass : LootRow->Items)
			{
				const FVector LocationOffset = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 50.0f;

				FActorSpawnParameters SpawnParams;
				SpawnParams.bNoFail = true;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

				const int32 ItemQuantity = ItemClass->GetDefaultObject<UItem>()->GetQuantity();

				FTransform SpawnTransform = GetActorTransform();
				SpawnTransform.AddToTranslation(LocationOffset);

				APickUp* Pickup = GetWorld()->SpawnActor<APickUp>(PickupClass, SpawnTransform, SpawnParams);
				Pickup->InitializePickup(ItemClass, ItemQuantity);
				Pickup->OnDestroyed.AddUniqueDynamic(this, &AItemSpawn::OnItemTaken);

				SpawnedPickups.Add(Pickup);

				Angle += (PI * 2.0f) / LootRow->Items.Num();
			}
		}
	}
}

void AItemSpawn::OnItemTaken(AActor* DestroyedActor)
{
	if (HasAuthority())
	{
		SpawnedPickups.Remove(DestroyedActor);

		//If all pickups were taken queue a respawn
		if (SpawnedPickups.Num() <= 0)
		{
			GetWorldTimerManager().SetTimer(TimerHandle_RespawnItem, this, &AItemSpawn::SpawnItem, FMath::RandRange(RespawnRange.GetMin(), RespawnRange.GetMax()), false);
		}
	}
}
