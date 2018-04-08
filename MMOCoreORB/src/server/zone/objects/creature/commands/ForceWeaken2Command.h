/*
				Copyright <SWGEmu>
		See file COPYING for copying conditions.*/

#ifndef FORCEWEAKEN2COMMAND_H_
#define FORCEWEAKEN2COMMAND_H_

#include "server/zone/objects/scene/SceneObject.h"
#include "ForcePowersQueueCommand.h"

class ForceWeaken2Command : public ForcePowersQueueCommand {
public:

	ForceWeaken2Command(const String& name, ZoneProcessServer* server)
		: ForcePowersQueueCommand(name, server) {

	}

	int doQueueCommand(CreatureObject* creature, const uint64& target, const UnicodeString& arguments) const {

		if (!checkStateMask(creature))
			return INVALIDSTATE;

		if (!checkInvalidLocomotions(creature))
			return INVALIDLOCOMOTION;

		if (isWearingArmor(creature)) {
			return NOJEDIARMOR;
		}

		ManagedReference<SceneObject*> targetObject = server->getZoneServer()->getObject(target);

		if (targetObject == NULL || !targetObject->isCreatureObject()) {
			return INVALIDTARGET;
		}

		ManagedReference<CreatureObject*> creatureTarget = targetObject.castTo<CreatureObject*>();
		if (creatureTarget == NULL){
			return INVALIDTARGET;
		}

		if (creatureTarget->hasBuff(getNameCRC())){
			creature->sendSystemMessage("They have already been weakened");
			return INVALIDTARGET;
		}

		int res = doCombatAction(creature, target);

		if (res == SUCCESS) {

			// Setup debuff.

			if (creatureTarget != NULL) {
				Locker clocker(creatureTarget, creature);
				int speedMods = 0;
				int speedPenalty = 25;
				float frsBonus = creature->getFrsMod("power");
				ManagedReference<Buff*> buff = NULL;
				
				if (creature->isPlayerCreature())
					buff = new Buff(creatureTarget, getNameCRC(), 60, BuffType::JEDI);
				else
					buff = new Buff(creatureTarget, getNameCRC(), 20, BuffType::JEDI);
					
					
				if (buff == NULL)
					return GENERALERROR;	
					
				Locker locker(buff);
				int hamStrength =  -1 * creatureTarget->getMaxHAM(CreatureAttribute::HEALTH) * .15 * frsBonus;				
				buff->setAttributeModifier(CreatureAttribute::HEALTH, hamStrength);
				hamStrength =  -1 * creatureTarget->getMaxHAM(CreatureAttribute::ACTION) * .15 * frsBonus;
				buff->setAttributeModifier(CreatureAttribute::ACTION, hamStrength);
				hamStrength =  -1 * creatureTarget->getMaxHAM(CreatureAttribute::MIND) * .15 * frsBonus;
				buff->setAttributeModifier(CreatureAttribute::MIND, hamStrength);

				ManagedReference<WeaponObject*> weapon = NULL;;
				weapon = creatureTarget->getWeapon();

				if (weapon != NULL){
				Vector<String>* weaponSpeedMods = weapon->getSpeedModifiers();
					for (int i = 0; i < weaponSpeedMods->size(); ++i) {
						speedMods += creatureTarget->getSkillMod(weaponSpeedMods->get(i));
					}
				}
				
				speedMods += creatureTarget->getSkillMod("private_speed_bonus");
				if (speedMods * .2 > speedPenalty) //20% attack speed debuff, or 20, whichever is higher
						speedPenalty = speedMods;
				if (speedPenalty > 25)
						speedPenalty = 25;
						
				speedPenalty *= -1;
				speedPenalty *= frsBonus;

				buff->setSkillModifier("melee_speed", speedPenalty);
				buff->setSkillModifier("ranged_speed", speedPenalty);

				creatureTarget->addBuff(buff);

				CombatManager::instance()->broadcastCombatSpam(creature, creatureTarget, NULL, 0, "cbt_spam", combatSpam + "_hit", 1);
			}

		}

		return res;
	}

};

#endif //FORCEWEAKEN2COMMAND_H_
