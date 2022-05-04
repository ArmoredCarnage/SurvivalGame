// Fill out your copyright notice in the Description page of Project Settings.


#include "WeaponItem.h"
#include "../Player/SurvivalPlayerController.h"
#include "../Player/SurvivalCharacter.h"

UWeaponItem::UWeaponItem()
{

}

bool UWeaponItem::Equip(class ASurvivalCharacter* Character)
{
	bool bEquipSuccessful = Super::Equip(Character);

	if (bEquipSuccessful && Character)
	{
		Character->EquipWeapon(this);
	}

	return bEquipSuccessful;
}

bool UWeaponItem::UnEquip(class ASurvivalCharacter* Character)
{
	bool bUnEquipSuccessful = Super::UnEquip(Character);

	if (bUnEquipSuccessful && Character)
	{
		Character->UnEquipWeapon();
	}

	return bUnEquipSuccessful;
}
