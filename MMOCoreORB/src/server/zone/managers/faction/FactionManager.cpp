/*
 * FactionManager.cpp
 *
 *  Created on: Mar 17, 2011
 *      Author: crush
 */

#include "FactionManager.h"
#include "FactionMap.h"
#include "server/zone/objects/player/PlayerObject.h"
#include "templates/manager/TemplateManager.h"
#include "server/zone/packets/player/PlayMusicMessage.h"
#include "server/chat/ChatManager.h"
#include "server/zone/managers/frs/FrsManager.h"
#include "server/zone/objects/group/GroupObject.h"
#include "server/zone/managers/player/PlayerManager.h"


FactionManager::FactionManager() {
	setLoggingName("FactionManager");
	setGlobalLogging(false);
	setLogging(false);
}

void FactionManager::loadData() {
	loadLuaConfig();
	loadFactionRanks();
}

void FactionManager::loadFactionRanks() {
	IffStream* iffStream = TemplateManager::instance()->openIffFile("datatables/faction/rank.iff");

	if (iffStream == NULL) {
		warning("Faction ranks could not be found.");
		return;
	}

	DataTableIff dtiff;
	dtiff.readObject(iffStream);

	factionRanks.readObject(&dtiff);

	delete iffStream;

	info("loaded " + String::valueOf(factionRanks.getCount()) + " ranks", true);
}

void FactionManager::loadLuaConfig() {
	info("Loading config file.", true);

	FactionMap* fMap = getFactionMap();

	Lua* lua = new Lua();
	lua->init();

	//Load the faction manager lua file.
	lua->runFile("scripts/managers/faction_manager.lua");

	LuaObject luaObject = lua->getGlobalObject("factionList");

	if (luaObject.isValidTable()) {
		for (int i = 1; i <= luaObject.getTableSize(); ++i) {
			LuaObject factionData = luaObject.getObjectAt(i);

			if (factionData.isValidTable()) {
				String factionName = factionData.getStringAt(1);
				bool playerAllowed = factionData.getBooleanAt(2);
				String enemies = factionData.getStringAt(3);
				String allies = factionData.getStringAt(4);
				float adjustFactor = factionData.getFloatAt(5);

				Faction faction(factionName);
				faction.setAdjustFactor(adjustFactor);
				faction.setPlayerAllowed(playerAllowed);
				faction.parseEnemiesFromList(enemies);
				faction.parseAlliesFromList(allies);

				fMap->addFaction(faction);
			}

			factionData.pop();
		}
	}

	luaObject.pop();

	delete lua;
	lua = NULL;
}

FactionMap* FactionManager::getFactionMap() {
	return &factionMap;
}

void FactionManager::awardFactionStanding(CreatureObject* player, const String& factionName, int level) {
	if (player == NULL)
		return;

	ManagedReference<PlayerObject*> ghost = player->getPlayerObject();

	if (!factionMap.contains(factionName))
		return;

	const Faction& faction = factionMap.get(factionName);
	const SortedVector<String>* enemies = faction.getEnemies();
	const SortedVector<String>* allies = faction.getAllies();

	if (!faction.isPlayerAllowed())
		return;

	float gain = level * faction.getAdjustFactor();
	float lose = gain * 2;

	ghost->decreaseFactionStanding(factionName, lose);

	//Lose faction standing to allies of the creature.
	for (int i = 0; i < allies->size(); ++i) {
		const String& ally = allies->get(i);

		if ((ally == "rebel" || ally == "imperial")) {
			continue;
		}

		if (!factionMap.contains(ally))
			continue;

		const Faction& allyFaction = factionMap.get(ally);

		if (!allyFaction.isPlayerAllowed())
			continue;

		ghost->decreaseFactionStanding(ally, lose);
	}

	bool gcw = false;
	if (factionName == "rebel" || factionName == "imperial") {
		gcw = true;
	}

	//Gain faction standing to enemies of the creature.
	for (int i = 0; i < enemies->size(); ++i) {
		const String& enemy = enemies->get(i);

		if ((enemy == "rebel" || enemy == "imperial") && !gcw) {
			continue;
		}

		if (!factionMap.contains(enemy))
			continue;

		const Faction& enemyFaction = factionMap.get(enemy);

		if (!enemyFaction.isPlayerAllowed())
			continue;

		ghost->increaseFactionStanding(enemy, gain);
	}
}


void FactionManager::awardPvpFactionPoints(TangibleObject* killer, CreatureObject* destructedObject, int numberCombatants = 1) {
	int killerFrsXp = 0;
	int victimFrsXp	= 0;
	int award = 0;
	ManagedReference<GroupObject*> group;
	ManagedReference<GroupObject*> vGroup;
	float contribution = 1;
	float vContribution = 1;

	if (killer->isPlayerCreature() && destructedObject->isPlayerCreature()) {
		CreatureObject* killerCreature = cast<CreatureObject*>(killer);
		ManagedReference<PlayerObject*> ghost = killerCreature->getPlayerObject();

		ManagedReference<PlayerObject*> killedGhost = destructedObject->getPlayerObject();
		ManagedReference<PlayerManager*> playerManager = killerCreature->getZoneServer()->getPlayerManager();

		//Broadcast to Server
		String playerName = destructedObject->getFirstName();
		String killerName = killerCreature->getFirstName();
		StringBuffer zBroadcast;

		FrsManager* frsManager = killerCreature->getZoneServer()->getFrsManager();

		if (killer->isRebel() && destructedObject->isImperial()) {
			ghost->increaseFactionStanding("rebel", 30);
			killer->playEffect("clienteffect/holoemote_rebel.cef", "head");
			PlayMusicMessage* pmm = new PlayMusicMessage("sound/music_themequest_victory_imperial.snd");
 			killer->sendMessage(pmm);
			ghost->decreaseFactionStanding("imperial", 45);
			killedGhost->decreaseFactionStanding("imperial", 45);

			if (killerCreature->hasSkill("force_rank_light_novice") && destructedObject->hasSkill("force_rank_dark_novice")) {
				zBroadcast << "\\#00e604" << "Jedi " << "\\#00bfff" << killerName << "\\#ffd700 has defeated" << "\\#e60000 Dark Jedi " << "\\#00bfff" << playerName << "\\#ffd700 in the FRS";
			} else if (killerCreature->hasSkill("force_rank_light_novice") && destructedObject->hasSkill("force_title_jedi_rank_02")) {
				zBroadcast << "\\#00e604" << "Jedi " << "\\#00bfff" << killerName << "\\#ffd700 has defeated" << "\\#e60000 Dark Jedi " << "\\#00bfff" << playerName << "\\#ffd700 in the GCW";
			}
			else {
				zBroadcast << "\\#00e604" << playerName << " \\#e60000 was slain in the GCW by " << "\\#00cc99" << killerName;
			}


			killer->getZoneServer()->getChatManager()->broadcastGalaxy(NULL, zBroadcast.toString());
		} else if (killer->isImperial() && destructedObject->isRebel()) {
			ghost->increaseFactionStanding("imperial", 30);
			killer->playEffect("clienteffect/holoemote_imperial.cef", "head");
			PlayMusicMessage* pmm = new PlayMusicMessage("sound/music_themequest_victory_imperial.snd");
 			killer->sendMessage(pmm);
			ghost->decreaseFactionStanding("rebel", 45);
			killedGhost->decreaseFactionStanding("rebel", 45);
			if (killerCreature->hasSkill("force_rank_dark_novice") && destructedObject->hasSkill("force_rank_light_novice")) {
				zBroadcast << "\\#e60000" << "Dark Jedi " << "\\#00bfff" << killerName << "\\#ffd700 has defeated" << "\\#00e604 Jedi " << "\\#00bfff" << playerName << "\\#ffd700 in the FRS";
			} else if (killerCreature->hasSkill("force_rank_dark_novice") && destructedObject->hasSkill("force_title_jedi_rank_02")){
				zBroadcast << "\\#e60000" << "Dark Jedi " << "\\#00bfff" << killerName << "\\#ffd700 has defeated" << "\\#00e604 Jedi " << "\\#00bfff" << playerName << "\\#ffd700 in the GCW";
			}
			else {
				zBroadcast << "\\#00e604" << playerName << " \\#e60000 was slain in the GCW by " << "\\#00cc99" << killerName;
			}

			ghost->getZoneServer()->getChatManager()->broadcastGalaxy(NULL, zBroadcast.toString());
		}
			if (killerCreature->hasSkill("force_rank_light_novice") || killerCreature->hasSkill("force_rank_dark_novice") || killerCreature->hasSkill("combat_bountyhunter_investigation_03")) {

				group = killerCreature->getGroup();
				if (group != NULL){
					for (int x=0; x< group->getGroupSize(); x++){
						ManagedReference<CreatureObject*> groupMember = group->getGroupMember(x);

						if (groupMember == killerCreature)
							continue;

						if ((groupMember->hasSkill("force_rank_light_novice") && (destructedObject->hasSkill("force_rank_dark_novice") || destructedObject->hasSkill("combat_bountyhunter_investigation_03")))
						|| (groupMember->hasSkill("force_rank_dark_novice") && (destructedObject->hasSkill("force_rank_light_novice") || destructedObject->hasSkill("combat_bountyhunter_investigation_03"))))
							if (groupMember->isInRange( killerCreature, 128.0f ))
								contribution++;
					}
				}

				vGroup = destructedObject->getGroup();
				if (vGroup != NULL){
					for (int x=0; x< vGroup->getGroupSize(); x++){
						ManagedReference<CreatureObject*> groupMember = vGroup->getGroupMember(x);

						if (groupMember == killerCreature)
							continue;

						if ((groupMember->hasSkill("force_rank_light_novice") && (destructedObject->hasSkill("force_rank_dark_novice") || destructedObject->hasSkill("combat_bountyhunter_investigation_03")))
						|| (groupMember->hasSkill("force_rank_dark_novice") && (destructedObject->hasSkill("force_rank_light_novice") || destructedObject->hasSkill("combat_bountyhunter_investigation_03"))))
							if (groupMember->isInRange( destructedObject, 128.0f ))
								vContribution++;
					}
				}

				int groupFrsXp = 0;
				int attackGroupContribution = contribution;

                                Locker threatLocker(killerCreature);
				ThreatMap* threatMapKiller = killerCreature->getThreatMap();		//calculate threatmap for all attackers as well as group members of attacking team for FRS
				for (int i = 0; i < threatMapKiller->size(); ++i) {
						ThreatMapEntry* entry = &threatMapKiller->elementAt(i).getValue();
						CreatureObject* attacker = threatMapKiller->elementAt(i).getKey();
								if (entry == NULL || attacker == NULL || attacker == killerCreature || !attacker->isPlayerCreature())
									continue;

								if (!killerCreature->isAttackableBy(destructedObject, true))
									continue;

								PlayerObject* attackerGhost = destructedObject->getPlayerObject();

								attackGroupContribution++;
				}
				threatLocker.release();

                                Locker threatLockerVictim(destructedObject);
				ThreatMap* threatMap = destructedObject->getThreatMap();		//calculate threatmap for all attackers as well as group members of attacking team for FRS
				for (int i = 0; i < threatMap->size(); ++i) {
						ThreatMapEntry* entry = &threatMap->elementAt(i).getValue();
						CreatureObject* attacker = threatMap->elementAt(i).getKey();
								if (entry == NULL || attacker == NULL || attacker == destructedObject || !attacker->isPlayerCreature())
									continue;

								if (!destructedObject->isAttackableBy(attacker, true))
									continue;

								PlayerObject* attackerGhost = attacker->getPlayerObject();

								if (attackerGhost == NULL)
									continue;

								contribution++;
				}
				threatLockerVictim.release();

				if (contribution > 1)
						error("New FRS Kill [" + String::valueOf(contribution) + "] Killers involved");

				if (contribution > 10)
						contribution = 10;


				if (vContribution  > 10)
						vContribution = 10;

				contribution *=2;
				vContribution *=2;

				float penalty = 1.f;
				//Modify FRS for group ratio difference
				if (attackGroupContribution > vContribution){
					penalty = attackGroupContribution/vContribution;
				}

				if (penalty < 4){ //up to 3v1 is not penalized
					penalty = 1;
				}
				else{
				error("FRS Group Size penalty KillerGroup Size [" + String::valueOf(attackGroupContribution) + "] VictimGroup Size [" + String::valueOf(vContribution) + "] Penalizing frs by [" + String::valueOf(penalty) + "00] Percent");
			  }

				if (group != NULL){
					for (int x=0; x< group->getGroupSize(); x++){
						ManagedReference<CreatureObject*> groupMember = group->getGroupMember(x);

						if ((groupMember->hasSkill("force_rank_light_novice") && (destructedObject->hasSkill("force_rank_dark_novice") || destructedObject->hasSkill("combat_bountyhunter_investigation_03")))
						|| (groupMember->hasSkill("force_rank_dark_novice") && (destructedObject->hasSkill("force_rank_light_novice") || destructedObject->hasSkill("combat_bountyhunter_investigation_03")))){

							if (groupMember == killerCreature)
								continue;

							if (!groupMember->isInRange( killerCreature, 128.0f )){
								error("group member: " + groupMember->getFirstName() + " Is not in range to recieve FRS XP");
								continue;
							}
							Locker glocker(groupMember);
							Locker frsLocker(frsManager);
							groupFrsXp = frsManager->calculatePvpExperienceChange(groupMember,destructedObject,1, false);
							award = (groupFrsXp / (1 + (contribution/20))) * (1 - (penalty/10)); //once penalty reaches 10 to 1 odds no frs is awareded.
							if (award < 0)
								award = 500; //Min 500 payout
							error("FRS Kill, Group member[" + String::valueOf(x) + "]: XP =" + String::valueOf(award));

 							frsManager->adjustFrsExperience(groupMember, award);
							glocker.release();
							frsLocker.release();
						}
					}
				}

				Locker locker(killerCreature);
				Locker vlocker(destructedObject);
				Locker frsLocker(frsManager);
				killerFrsXp = frsManager->calculatePvpExperienceChange(killerCreature,destructedObject,1, false);
				award = (killerFrsXp / (1 + (contribution/20))) * (1 - (penalty/10)); //once penalty reaches 10 to 1 odds no frs is awareded.
				if (award < 0)
					award = 500; //Min 500 payout
				error("FRS KILL, KILLER primary: " + killerCreature->getFirstName() + " Killer XP =" + String::valueOf(award));
				frsManager->adjustFrsExperience(killerCreature, award);

				if (destructedObject->hasSkill("force_rank_dark_novice") || destructedObject->hasSkill("force_rank_light_novice")){
					victimFrsXp = frsManager->calculatePvpExperienceChange(destructedObject,killerCreature,contribution, true);
					int deduct = victimFrsXp/(contribution/2);
					if (deduct > -2500)
						deduct = -2500; //minimum 2500 loss from anywhere.

					deduct /= penalty;
					if (penalty > 5) //more than 5v1 no FRS is lost.
						deduct = 0;

					frsManager->adjustFrsExperience(destructedObject, deduct);
				}
				error("FRS Death, Killer XP =" + String::valueOf(award));
				error("FRS Death, Victim XP =" + String::valueOf(victimFrsXp/(contribution/2)));

			}
	}
}

String FactionManager::getRankName(int idx) {
	if (idx >= factionRanks.getCount())
		return "";

	return factionRanks.getRank(idx).getName();
}

int FactionManager::getRankCost(int rank) {
	if (rank >= factionRanks.getCount())
		return -1;

	return factionRanks.getRank(rank).getCost();
}

int FactionManager::getRankDelegateRatioFrom(int rank) {
	if (rank >= factionRanks.getCount())
		return -1;

	return factionRanks.getRank(rank).getDelegateRatioFrom();
}

int FactionManager::getRankDelegateRatioTo(int rank) {
	if (rank >= factionRanks.getCount())
		return -1;

	return factionRanks.getRank(rank).getDelegateRatioTo();
}

int FactionManager::getFactionPointsCap(int rank) {
	if (rank >= factionRanks.getCount())
		return -1;

	return Math::max(1000, getRankCost(rank) * 20);
}

bool FactionManager::isFaction(const String& faction) {
	if (factionMap.contains(faction))
		return true;

	return false;
}

bool FactionManager::isEnemy(const String& faction1, const String& faction2) {

	if (!factionMap.contains(faction1) || !factionMap.contains(faction2))
		return false;

	Faction* faction = factionMap.getFaction(faction1);

	return faction->getEnemies()->contains(faction2);
}

bool FactionManager::isAlly(const String& faction1, const String& faction2) {

	if (!factionMap.contains(faction1) || !factionMap.contains(faction2))
		return false;

	Faction* faction = factionMap.getFaction(faction1);

	return faction->getAllies()->contains(faction2);
}
