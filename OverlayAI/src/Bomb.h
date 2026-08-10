#pragma once

// ============================================================
// Bomb.h
// Bomb structures and function declarations.
// ============================================================

#include <vector>
#include <string>
#include <cstdio>
#include <cmath>
#include "imgui.h"
#include "Memory.h"
#include "Offsets.h"
#include "../offsets.hpp"

// --- BombInfo (was Bombatimer.h) ---
enum class BombState {
	Idle,
	Planted,
	Defusing,
	Exploded,
	Defused
};

enum class BombSite {
	None,
	A,
	B
};

class BombInfo {
public:
	static constexpr double kBombTime = 40.000;
	static constexpr double kDefuseTimeKit = 5.000;
	static constexpr double kDefuseTimeNoKit = 10.000;

	void Plant(BombSite site) {
		m_site = site;
		m_state = BombState::Planted;
		m_plantStart = ImGui::GetTime();
		m_defuseStart = 0.0;
		m_defuseDuration = 0.0;
	}

	void SyncPlant(double timeToExplode, BombSite site) {
		m_site = site;
		m_state = BombState::Planted;
		double now = ImGui::GetTime();
		double elapsed = kBombTime - timeToExplode;
		if (elapsed < 0.0) elapsed = 0.0;
		m_plantStart = now - elapsed;
		m_defuseStart = 0.0;
		m_defuseDuration = 0.0;
	}

	void StartDefuse(bool hasKit) {
		if (m_state != BombState::Planted) return;
		m_defuseDuration = hasKit ? kDefuseTimeKit : kDefuseTimeNoKit;
		m_defuseStart = ImGui::GetTime();
		m_state = BombState::Defusing;
	}

	void CancelDefuse() {
		if (m_state != BombState::Defusing) return;
		m_state = BombState::Planted;
		m_defuseStart = 0.0;
		m_defuseDuration = 0.0;
	}

	void Reset() {
		m_state = BombState::Idle;
		m_site = BombSite::None;
		m_plantStart = 0.0;
		m_defuseStart = 0.0;
		m_defuseDuration = 0.0;
	}

	bool Update() {
		double now = ImGui::GetTime();
		if (m_state == BombState::Planted || m_state == BombState::Defusing) {
			double bombRemaining = kBombTime - (now - m_plantStart);
			if (bombRemaining <= 0.0) {
				m_state = BombState::Exploded;
				return true;
			}
			if (m_state == BombState::Defusing) {
				double defuseRemaining = m_defuseDuration - (now - m_defuseStart);
				if (defuseRemaining <= 0.0) {
					m_state = BombState::Defused;
					return true;
				}
			}
		}
		return false;
	}

	BombState State() const { return m_state; }
	BombSite Site() const { return m_site; }

	const char* SiteLabel() const {
		switch (m_site) {
		case BombSite::A: return "A";
		case BombSite::B: return "B";
		default: return "-";
		}
	}

	double BombTimeRemaining() const {
		if (m_state != BombState::Planted && m_state != BombState::Defusing) return 0.0;
		double remaining = kBombTime - (ImGui::GetTime() - m_plantStart);
		return remaining > 0.0 ? remaining : 0.0;
	}

	double DefuseTimeRemaining() const {
		if (m_state != BombState::Defusing) return 0.0;
		double remaining = m_defuseDuration - (ImGui::GetTime() - m_defuseStart);
		return remaining > 0.0 ? remaining : 0.0;
	}

	float DefuseProgress() const {
		if (m_state != BombState::Defusing || m_defuseDuration <= 0.0) return 0.0f;
		float p = (float)((ImGui::GetTime() - m_defuseStart) / m_defuseDuration);
		if (p < 0.0f) p = 0.0f;
		if (p > 1.0f) p = 1.0f;
		return p;
	}

	bool DefuseHasKit() const { return m_defuseDuration == kDefuseTimeKit; }

	static std::string FormatTime(double seconds) {
		if (seconds < 0.0) seconds = 0.0;
		int totalMs = (int)(seconds * 1000.0 + 0.5);
		int s = totalMs / 1000;
		int ms = totalMs % 1000;
		char buf[16];
		snprintf(buf, sizeof(buf), "%02d.%03d", s, ms);
		return std::string(buf);
	}

	static std::string FormatTimeMinSec(double seconds) {
		if (seconds < 0.0) seconds = 0.0;
		int totalMs = (int)(seconds * 1000.0 + 0.5);
		int m = totalMs / 60000;
		int s = (totalMs / 1000) % 60;
		int ms = totalMs % 1000;
		char buf[16];
		snprintf(buf, sizeof(buf), "%02d:%02d.%03d", m, s, ms);
		return std::string(buf);
	}

private:
	BombState m_state = BombState::Idle;
	BombSite  m_site  = BombSite::None;

	double m_plantStart = 0.0;
	double m_defuseStart = 0.0;
	double m_defuseDuration = 0.0;
};

// --- Planted info / helpers (merged) ---
struct PlantedC4Info {
	int site = -1;
	float timeToExplode = 0.0f;
	bool beingDefused = false;
	std::string defuserName;
	bool defuserHasKit = false;
	int defuserKitRaw = -1;
	int defuserKitOffset = -1;
};

// BombInfo instance (inline to allow header-only)
inline BombInfo g_BombInfo;

// Forward externs (implemented elsewhere)
extern void ReadPlayerName(uintptr_t controller, char* out, size_t outSize);
extern uintptr_t GetEntityListBase();
extern uintptr_t GetEntityByIndexAuto(uintptr_t entityList, int index);

// Hardcoded planted-C4 offsets used locally
static constexpr std::ptrdiff_t OFF_CPlanted_m_flC4Blow = 0x11D0;
static constexpr std::ptrdiff_t OFF_CPlanted_m_bBeingDefused = 0x11DC;
static constexpr std::ptrdiff_t OFF_CPlanted_m_flTimerLength = 0x11D8;
static constexpr std::ptrdiff_t OFF_CPlanted_m_flDefuseCountDown = 0x11F0;

inline static float GetCurrentGameTimeSeconds() {
	if (!IsValidPtr(mem.clientModule)) return 0.0f;
	uintptr_t gvDirect = mem.clientModule + Offsets::dwGlobalVars;
	uintptr_t gvPtr = mem.Read<uintptr_t>(gvDirect);
	const uintptr_t kCurTimeOffset = 0xC;

	for (uintptr_t base : { gvDirect, gvPtr }) {
		if (!IsValidPtr(base)) continue;
		float cur = mem.Read<float>(base + kCurTimeOffset);
		if (isfinite(cur) && cur > 0.0001f && cur < 100000.0f) return cur;
	}

	return 0.0f;
}

inline std::vector<PlantedC4Info> GatherPlantedC4Infos() {
	std::vector<PlantedC4Info> out;

	auto validateAndFill = [&](uintptr_t ent) -> bool {
		if (!IsValidPtr(ent)) return false;
		bool ticking = mem.Read<bool>(ent + Offsets::m_bBombTicking);
		bool activated = ticking;
		bool hasExploded = mem.Read<bool>(ent + Offsets::m_bBombDefused);
		if (!ticking && !activated) return false;
		if (hasExploded) return false;

		float timerLen = mem.Read<float>(ent + OFF_CPlanted_m_flTimerLength);
		if (!isfinite(timerLen) || (timerLen != 0.0f && (timerLen < 0.1f || timerLen > 600.0f))) {
			return false;
		}

		int siteCheck = mem.Read<int>(ent + Offsets::m_nBombSite);
		if (siteCheck != 0 && siteCheck != 1) return false;

		uintptr_t usedFlBlowOff = OFF_CPlanted_m_flC4Blow;
		float flBlow = mem.Read<float>(ent + usedFlBlowOff);
		float now = GetCurrentGameTimeSeconds();

		auto toRemainingTime = [&](float gameTimeValue) -> float {
			if (!isfinite(gameTimeValue) || gameTimeValue <= 0.0f) return 0.0f;
			const float maxExpected = (timerLen > 0.0f ? timerLen : 40.0f) + 1.0f;
			if (now > 0.01f && gameTimeValue > now) {
				float remaining = gameTimeValue - now;
				if (remaining > 0.0f && remaining <= maxExpected) return remaining;
			}
			if (gameTimeValue > 0.0f && gameTimeValue <= maxExpected) return gameTimeValue;
			return 0.0f;
		};

		float timeToExplode = toRemainingTime(flBlow);
		if (timeToExplode <= 0.0f) {
			bool found = false;
			for (int delta = -0x20; delta <= 0x20; delta += 4) {
				uintptr_t candOff = OFF_CPlanted_m_flC4Blow + delta;
				if (candOff == usedFlBlowOff) continue;
				float cand = mem.Read<float>(ent + candOff);
				if (!isfinite(cand)) continue;
				float candRemaining = toRemainingTime(cand);
				if (candRemaining > 0.0f) {
					flBlow = cand;
					timeToExplode = candRemaining;
					usedFlBlowOff = candOff;
					found = true;
					break;
				}
			}
			if (!found) {
				// Keep the planted bomb visible even when flC4Blow cannot be resolved cleanly.
				timeToExplode = (isfinite(timerLen) && timerLen > 0.0f && timerLen <= 60.0f) ? timerLen : 0.0f;
			}
		}

		const float maxExpectedTime = (timerLen > 0.0f ? timerLen : 40.0f) + 1.0f;
		if (timeToExplode < 0.0f || timeToExplode > maxExpectedTime) {
			timeToExplode = 0.0f;
		}

		int site = mem.Read<int>(ent + Offsets::m_nBombSite);
		if (site != 0 && site != 1) return false;

		uint32_t hDef = mem.Read<uint32_t>(ent + Offsets::m_hBombDefuser);
		float defuseCountDown = mem.Read<float>(ent + OFF_CPlanted_m_flDefuseCountDown);
		bool beingDefused = mem.Read<bool>(ent + OFF_CPlanted_m_bBeingDefused);
		if (!beingDefused && hDef && hDef != 0xFFFFFFFF) {
			if ((now > 0.01f && defuseCountDown > now) || (defuseCountDown > 0.01f && defuseCountDown < 10000.0f)) {
				beingDefused = true;
			}
		}

		PlantedC4Info info;
		info.site = site;
		info.timeToExplode = timeToExplode;
		info.beingDefused = beingDefused;
		info.defuserName = "";
		if (hDef && hDef != 0xFFFFFFFF) {
			int defIdx = hDef & 0x7FFF;
			uintptr_t entityList = GetEntityListBase();
			if (IsValidPtr(entityList)) {
				uintptr_t defPawn = GetEntityByIndexAuto(entityList, defIdx);
				if (IsValidPtr(defPawn)) {
					uintptr_t defController = 0;
					for (int j = 1; j <= 64; ++j) {
						uintptr_t c = GetEntityByIndexAuto(entityList, j);
						if (!IsValidPtr(c)) continue;
						uint32_t ph = mem.Read<uint32_t>(c + Offsets::m_hPlayerPawn);
						if (!ph || ph == 0xFFFFFFFF) continue;
						if ((ph & 0x7FFF) == defIdx) { defController = c; break; }
					}
					char dn[128] = {};
// Reads a player name from memory
					if (IsValidPtr(defController)) ReadPlayerName(defController, dn, sizeof(dn));
					if (dn[0] != '\0') info.defuserName = dn;
					int raw = -1;
					int foundOffset = -1;
					bool hasKit = false;
					// Try reading as a byte at the pawn entity
					if (IsValidPtr(defPawn)) {
						uint8_t v = mem.Read<uint8_t>(defPawn + Offsets::m_bPawnHasDefuser);
						raw = (int)v;
						if (v != 0) { hasKit = true; foundOffset = (int)Offsets::m_bPawnHasDefuser; }
					}
					// If not found, try the controller (sometimes handles/pointers differ)
					if (foundOffset == -1 && IsValidPtr(defController)) {
						uint8_t v2 = mem.Read<uint8_t>(defController + Offsets::m_bPawnHasDefuser);
						if ((int)v2 != raw) raw = (int)v2; // capture last read
						if (v2 != 0) { hasKit = true; foundOffset = (int)Offsets::m_bPawnHasDefuser; }
					}
					// If still not found, scan nearby bytes around the expected offset for a likely flag
					if (foundOffset == -1 && IsValidPtr(defPawn)) {
						for (int delta = -0x20; delta <= 0x20; ++delta) {
							uintptr_t off = Offsets::m_bPawnHasDefuser + delta;
							uint8_t v = mem.Read<uint8_t>(defPawn + off);
							if ((int)v != 0) {
								raw = (int)v;
								hasKit = true;
								foundOffset = (int)off;
								break;
							}
						}
					}
					info.defuserHasKit = hasKit;
					info.defuserKitRaw = raw;
					info.defuserKitOffset = foundOffset;
				}
			}
		}

		out.push_back(info);
		return true;
	};

	if (IsValidPtr(mem.clientModule)) {
		uintptr_t plantedPtr = 0;
		if (Offsets::dwPlantedC4 != 0) {
			plantedPtr = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwPlantedC4);
		}
		if (IsValidPtr(plantedPtr)) {
			uintptr_t cand1 = plantedPtr;
			uintptr_t cand2 = mem.Read<uintptr_t>(plantedPtr);
			if (validateAndFill(cand1)) return out;
			if (validateAndFill(cand2)) return out;
		}
	}

	return out;
}

inline void UpdateBombFromMemory() {
	auto infos = GatherPlantedC4Infos();
	if (infos.empty()) {
		// No planted C4 detected: immediately clear any previous bomb state and debug
		g_BombInfo.Reset();
		return;
	}
	const PlantedC4Info& p = infos[0];
	BombSite site = (p.site == 0) ? BombSite::A : BombSite::B;
	BombState state = g_BombInfo.State();
	if (state == BombState::Idle || state == BombState::Exploded || state == BombState::Defused || g_BombInfo.Site() != site) {
		g_BombInfo.Plant(site);
	}
	if (p.beingDefused) {
		if (g_BombInfo.State() == BombState::Planted) g_BombInfo.StartDefuse(p.defuserHasKit);
	} else {
		if (g_BombInfo.State() == BombState::Defusing) g_BombInfo.CancelDefuse();
	}
	g_BombInfo.Update();
}

inline void DrawBombImGui() {
	ImGui::Begin("Bomb Timer");
	BombState st = g_BombInfo.State();
	if (st == BombState::Idle) {
		ImGui::TextDisabled("No planted C4 detected");
	} else {
		ImGui::Text("Site: %s", g_BombInfo.SiteLabel());
		ImGui::Text("Bomb: %s", BombInfo::FormatTimeMinSec(g_BombInfo.BombTimeRemaining()).c_str());
		if (st == BombState::Defusing) {
			ImGui::Text("Defuse: %s (%.0f%%)", g_BombInfo.DefuseHasKit() ? "Kit" : "No kit", g_BombInfo.DefuseProgress() * 100.0f);
			ImGui::Text("Defuse time: %s", BombInfo::FormatTime(g_BombInfo.DefuseTimeRemaining()).c_str());
		}
	}
	ImGui::End();
}
