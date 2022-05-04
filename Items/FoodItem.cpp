// Fill out your copyright notice in the Description page of Project Settings.


#include "FoodItem.h"
#include "../Player/SurvivalCharacter.h"
#include "../Player/SurvivalPlayerController.h"
#include "../Components/InventoryComponent.h"

#define LOCTEXT_NAMESPACE "FoodItem"

UFoodItem::UFoodItem()
{
	HealAmount = 20.0f;
	UseActionText = LOCTEXT("ItemUseActionText", "Consume");
}

void UFoodItem::Use(class ASurvivalCharacter* Character)
{
	if (Character)
	{
		const float ActualHealedAmount = Character->ModifyHealth(HealAmount);
		const bool bUsedFood = !FMath::IsNearlyZero(ActualHealedAmount);

		if (!Character->HasAuthority())
		{
			if (ASurvivalPlayerController* PC = Cast<ASurvivalPlayerController>(Character->GetController()))
			{
				if (bUsedFood)
				{
					PC->ClientShowNotification(FText::Format(LOCTEXT("AteFoodText", "Ate {FoodName}, healed {HealAmount}, health."), ItemDisplayName, ActualHealedAmount));
				}
				else
				{
					PC->ClientShowNotification(FText::Format(LOCTEXT("FullHealthText", "No need to eat {FoodName}, health is already full."), ItemDisplayName, HealAmount));
				}
			}
		}

		if (bUsedFood)
		{
			if (UInventoryComponent* Inventory = Character->PlayerInventory)
			{
				Inventory->ConsumeItem(this, 1);
			}
		}
	}

	
	
}

#undef LOCTEXT_NAMESPACE