#include "Bomb.h"
#include "Memory.h"
#include "Offsets.h"


// Hardcoded offsets (from client_dll.hpp) used locally to avoid depending on that header in build
static constexpr std::ptrdiff_t OFF_pObserverServices = 0x11F8; // C_BasePlayerPawn::m_pObserverServices
static constexpr std::ptrdiff_t OFF_obs_m_iObserverMode = 0x48; // CPlayer_ObserverServices::m_iObserverMode
static constexpr std::ptrdiff_t OFF_obs_m_hObserverTarget = 0x4C; // CPlayer_ObserverServices::m_hObserverTarget

static constexpr std::ptrdiff_t OFF_CPlanted_m_bBombTicking = 0x1160; // C_PlantedC4::m_bBombTicking
static constexpr std::ptrdiff_t OFF_CPlanted_m_flC4Blow = 0x1190; // C_PlantedC4::m_flC4Blow
static constexpr std::ptrdiff_t OFF_CPlanted_m_nBombSite = 0x1164; // C_PlantedC4::m_nBombSite
static constexpr std::ptrdiff_t OFF_CPlanted_m_flTimerLength = 0x1198; // C_PlantedC4::m_flTimerLength
static constexpr std::ptrdiff_t OFF_CPlanted_m_flDefuseCountDown = 0x11B0; // C_PlantedC4::m_flDefuseCountDown
static constexpr std::ptrdiff_t OFF_CPlanted_m_bBeingDefused = 0x119C; // C_PlantedC4::m_bBeingDefused
static constexpr std::ptrdiff_t OFF_CPlanted_m_hBombDefuser = 0x11B8; // C_PlantedC4::m_hBombDefuser
static constexpr std::ptrdiff_t OFF_CPlanted_m_bC4Activated = 0x11A8; // C_PlantedC4::m_bC4Activated

#include <vector>
#include <string>
#include <cstdio>

// Use existing ReadPlayerName to get names
extern void ReadPlayerName(uintptr_t controller, char* out, size_t outSize);
extern uintptr_t GetEntityListBase();
extern uintptr_t GetEntityByIndexAuto(uintptr_t entityList, int index);

using namespace std;

static float GetCurrentGameTimeSeconds() {
	// Read engine global vars pointer then m_flCurTime at +0xC (common layout)
	if (!IsValidPtr(mem.clientModule)) return 0.0f;
	uintptr_t gv = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwGlobalVars);
	if (!IsValidPtr(gv)) return 0.0f;
	// m_flCurTime commonly at +0xC
	float cur = mem.Read<float>(gv + 0xC);
	if (!isfinite(cur)) return 0.0f;
	return cur;
}

vector<pair<string,string>> GatherSpectators() {
	vector<pair<string,string>> ret;
	uintptr_t entityList = GetEntityListBase();
	if (!IsValidPtr(entityList)) return ret;

	// iterate controllers (1..64) and resolve observer target via pawn's observer services
	for (int i = 1; i <= 64; ++i) {
		uintptr_t controller = GetEntityByIndexAuto(entityList, i);
		if (!IsValidPtr(controller)) continue;

		uint32_t pawnHandle = mem.Read<uint32_t>(controller + Offsets::m_hPlayerPawn);
		if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue; // controller has no pawn

		uintptr_t pawn = GetEntityByIndexAuto(entityList, pawnHandle & 0x7FFF);
		if (!IsValidPtr(pawn)) continue;

		uintptr_t obsSrv = mem.Read<uintptr_t>(pawn + OFF_pObserverServices);
		if (!IsValidPtr(obsSrv)) continue;

		uint8_t mode = mem.Read<uint8_t>(obsSrv + OFF_obs_m_iObserverMode);
		uint32_t hTarget = mem.Read<uint32_t>(obsSrv + OFF_obs_m_hObserverTarget);
		if (!hTarget || hTarget == 0xFFFFFFFF) continue;

		int targetIndex = hTarget & 0x7FFF;
		uintptr_t targetEnt = GetEntityByIndexAuto(entityList, targetIndex);
		if (!IsValidPtr(targetEnt)) continue;

		char obsName[128] = {};
		ReadPlayerName(controller, obsName, sizeof(obsName));
		if (obsName[0] == '\0') snprintf(obsName, sizeof(obsName), "Player %d", i);

		// attempt to find target controller to read its name
		uintptr_t targetController = 0;
		for (int j = 1; j <= 64; ++j) {
			uintptr_t c = GetEntityByIndexAuto(entityList, j);
			if (!IsValidPtr(c)) continue;
			uint32_t ph = mem.Read<uint32_t>(c + Offsets::m_hPlayerPawn);
			if (!ph || ph == 0xFFFFFFFF) continue;
			if ((ph & 0x7FFF) == targetIndex) { targetController = c; break; }
		}

		char tgtName[128] = {};
		if (IsValidPtr(targetController)) ReadPlayerName(targetController, tgtName, sizeof(tgtName));
		if (tgtName[0] == '\0') snprintf(tgtName, sizeof(tgtName), "Pawn %d", targetIndex);

		ret.emplace_back(string(obsName), string(tgtName));
	}

	return ret;
}

struct RawPlanted {
	uintptr_t addr;
};

vector<PlantedC4Info> GatherPlantedC4Infos() {
	vector<PlantedC4Info> out;
	uintptr_t entityList = GetEntityListBase();
	if (!IsValidPtr(entityList)) return out;

	// Scan entity list for planted C4 entities by reading potential m_bBombTicking/m_flC4Blow presence.
	for (int i = 1; i <= 8192; ++i) {
		uintptr_t ent = GetEntityByIndexAuto(entityList, i);
		if (!IsValidPtr(ent)) continue;

		// Heuristics: check if this entity is a planted C4 by reading m_bBombTicking or m_bC4Activated
		bool ticking = mem.Read<bool>(ent + OFF_CPlanted_m_bBombTicking);
		bool activated = mem.Read<bool>(ent + OFF_CPlanted_m_bC4Activated);
		float flBlow = mem.Read<float>(ent + OFF_CPlanted_m_flC4Blow);
		if (!ticking && !activated) continue; // likely not a planted C4
		if (flBlow <= 0.0f || flBlow > 1e9f) continue;

		PlantedC4Info info;
		info.site = mem.Read<int>(ent + OFF_CPlanted_m_nBombSite);

		// read current system time: best-effort use flBlow as absolute timestamp and attempt to subtract current time
		// as a fallback show flBlow raw as seconds remaining if we cannot read global time
		float defCount = mem.Read<float>(ent + OFF_CPlanted_m_flDefuseCountDown);
		bool being = mem.Read<bool>(ent + OFF_CPlanted_m_bBeingDefused);
		uint32_t hDef = mem.Read<uint32_t>(ent + OFF_CPlanted_m_hBombDefuser);

		info.beingDefused = being;
		info.defuserName = "";

		// try to resolve defuser name
		if (hDef && hDef != 0xFFFFFFFF) {
			int defIdx = hDef & 0x7FFF;
			uintptr_t defPawn = GetEntityByIndexAuto(entityList, defIdx);
			if (IsValidPtr(defPawn)) {
				// find controller for pawn
				uintptr_t defController = 0;
				for (int j = 1; j <= 64; ++j) {
					uintptr_t c = GetEntityByIndexAuto(entityList, j);
					if (!IsValidPtr(c)) continue;
					uint32_t ph = mem.Read<uint32_t>(c + Offsets::m_hPlayerPawn);
					if (!ph || ph == 0xFFFFFFFF) continue;
					if ((ph & 0x7FFF) == defIdx) { defController = c; break; }
				}
				char dn[128] = {};
				if (IsValidPtr(defController)) ReadPlayerName(defController, dn, sizeof(dn));
				if (dn[0] != '\0') info.defuserName = dn;
			}
		}

		// timeToExplode: compute delta between flBlow and current game time if possible
		float now = GetCurrentGameTimeSeconds();
		if (now > 0.01f && flBlow > 0.01f && flBlow > now) {
			info.timeToExplode = flBlow - now;
		} else if (flBlow > 0.0f && flBlow < 10000.0f) {
			// sometimes flBlow is already a seconds-remaining value
			info.timeToExplode = flBlow;
		} else {
			info.timeToExplode = 0.0f;
		}

		out.push_back(info);
	}

	return out;
}
