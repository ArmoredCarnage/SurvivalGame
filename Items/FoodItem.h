// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "SurvivalGame\Items\Item.h"
#include "FoodItem.generated.h"

/**
 * 
 */
UCLASS()
class SURVIVALGAME_API UFoodItem : public UItem
{
	GENERATED_BODY()

public:

	UFoodItem();

	//The amount for the food to heal
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Healing")
	float HealAmount;

	virtual void Use(class ASurvivalCharacter* Character) override;
	
};
