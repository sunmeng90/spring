/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef SKIRMISH_AI_WRAPPER_H
#define SKIRMISH_AI_WRAPPER_H

#include "AICallback.h"
#include "AICheats.h"
#include "SkirmishAIKey.h"
#include "System/Object.h"

#include <string>
#include <memory>

class CAICallback;
class CAICheats;
class CSkirmishAILibrary;
struct SSkirmishAICallback;

struct Command;
class float3;


/**
 * Acts as an OO wrapper for a Skirmish AI instance.
 * Basically converts function calls to AIEvents,
 * which are then sent ot the AI.
 */
class CSkirmishAIWrapper {
private:
	CR_DECLARE_STRUCT(CSkirmishAIWrapper)

	void CreateCallback();

public:
	/// used only by creg
	CSkirmishAIWrapper() = default;
	CSkirmishAIWrapper(const int skirmishAIId);

	CSkirmishAIWrapper(const CSkirmishAIWrapper& w) = delete;
	CSkirmishAIWrapper(CSkirmishAIWrapper&& w) { *this = std::move(w); }

	CSkirmishAIWrapper& operator = (const CSkirmishAIWrapper& w) = delete;
	CSkirmishAIWrapper& operator = (CSkirmishAIWrapper&& w) {
		key = w.key;

		library = w.library; w.library = nullptr;
		sCallback = w.sCallback; w.sCallback = nullptr;

		callback = std::move(w.callback);
		cheats = std::move(w.cheats);

		timerName = std::move(w.timerName);


		skirmishAIId = w.skirmishAIId; w.skirmishAIId = -1;
		teamId = w.teamId; w.teamId = -1;

		initialized = w.initialized; w.initialized = false;
		released = w.released; w.released = false;
		cheatEvents = w.cheatEvents; w.cheatEvents = false;

		initOk = w.initOk; w.initOk = false;
		dieing = w.dieing; w.dieing = false;
		return *this;
	}

	void Serialize(creg::ISerializer* s) {}
	void PostLoad() {
		CreateCallback();
		LoadSkirmishAI(true);
	}


	/**
	 * Initialize the AI instance.
	 * This calls the native init() method, the InitAIEvent is sent afterwards.
	 */
	void Init();
	void Kill();

	/// @see SReleaseEvent in Interface/AISEvents.h
	void Release(int reason = 0 /* = unspecified */);

	/** Called just before all the units are destroyed. */
	void PreDestroy();

	/**
	 * No events are forwarded to the Skirmish AI plugin
	 * after this method has been called.
	 * Do not call this if you want to kill a local AI, but use
	 * the Skirmish AI Handler instead.
	 * @see CSkirmishAIHandler::SetLocalSkirmishAIDieing()
	 */
	void SetDieing() { dieing = true; }


	// AI Events
	void Load(std::istream *s);
	void Save(std::ostream *s);

	void UnitIdle(int unitId);
	void UnitCreated(int unitId, int builderId);
	void UnitFinished(int unitId);
	void UnitDestroyed(int unitId, int attackerUnitId);
	void UnitDamaged(int unitId, int attackerUnitId, float damage, const float3& dir, int weaponDefId, bool paralyzer);
	void UnitMoveFailed(int unitId);
	void UnitGiven(int unitId, int oldTeam, int newTeam);
	void UnitCaptured(int unitId, int oldTeam, int newTeam);
	void EnemyCreated(int unitId);
	void EnemyFinished(int unitId);
	void EnemyEnterLOS(int unitId);
	void EnemyLeaveLOS(int unitId);
	void EnemyEnterRadar(int unitId);
	void EnemyLeaveRadar(int unitId);
	void EnemyDestroyed(int enemyUnitId, int attackerUnitId);
	void EnemyDamaged(int enemyUnitId, int attackerUnitId, float damage, const float3& dir, int weaponDefId, bool paralyzer);
	void Update(int frame);
	void SendChatMessage(const char* msg, int fromPlayerId);
	void SendLuaMessage(const char* inData, const char** outData);
	void WeaponFired(int unitId, int weaponDefId);
	void PlayerCommandGiven(const std::vector<int>& selectedUnits, const Command& c, int playerId);
	void CommandFinished(int unitId, int commandId, int commandTopicId);
	void SeismicPing(int allyTeam, int unitId, const float3& pos, float strength);

	int GetSkirmishAIID() const { return skirmishAIId; }
	int GetTeamId() const { return teamId; }
	const SkirmishAIKey& GetKey() const { return key; }

	bool Active() const { return (skirmishAIId != -1); }

	void SetCheatEventsEnabled(bool enable) { cheatEvents = enable; }
	bool CheatEventsEnabled() const { return cheatEvents; }

private:
	bool LoadSkirmishAI(bool postLoad);

	/**
	 * CAUTION: takes C AI Interface events, not engine C++ ones!
	 */
	int HandleEvent(int topic, const void* data) const;

private:
	SkirmishAIKey key;

	const CSkirmishAILibrary* library = nullptr;
	const SSkirmishAICallback* sCallback = nullptr;

	std::unique_ptr<CAICallback> callback;
	std::unique_ptr<CAICheats> cheats;

	std::string timerName;


	int skirmishAIId = -1;
	int teamId = -1;

	bool initialized = false;
	bool released = false;
	bool cheatEvents = false;

	bool initOk = false;
	bool dieing = false;
};

#endif // SKIRMISH_AI_WRAPPER_H
