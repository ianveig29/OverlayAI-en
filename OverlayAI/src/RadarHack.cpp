// ============================================================
// RadarHack.cpp
// Shows a radar with all player positions, even those behind walls.
// ============================================================

#include "RadarHack.h"

#include "Config.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include "Stats.h"

#include <unordered_map>
#include <unordered_set>

namespace {
    struct RadarOverride {
        uintptr_t identity = 0;
        bool originalSpotted = false;
    };

    std::unordered_map<uintptr_t, RadarOverride> g_overrides;
    ULONGLONG g_lastUpdateMs = 0;

    void WriteSpotted(uintptr_t pawn, bool value) {
        if (mem.Write<bool>(pawn + Offsets::m_entitySpottedState + Offsets::m_bSpotted, value))
            Stats::rpmWriteCount.fetch_add(1);
    }

    void RestoreOne(uintptr_t pawn, const RadarOverride& state) {
        if (!IsValidPtr(pawn)) return;
        const uintptr_t identity = mem.Read<uintptr_t>(pawn + Offsets::m_pEntityIdentity);
        if (identity != state.identity) return;
        const uintptr_t spottedAddress = pawn + Offsets::m_entitySpottedState + Offsets::m_bSpotted;
        if (mem.Read<bool>(spottedAddress) != state.originalSpotted)
            WriteSpotted(pawn, state.originalSpotted);
    }

    void RestoreAll() {
        for (const auto& [pawn, state] : g_overrides)
            RestoreOne(pawn, state);
        g_overrides.clear();
        g_lastUpdateMs = 0;
    }
}

void UpdateRadarHack() {
    if (!g_Esp.enableRadarHack || !mem.hProcess) {
        if (!g_overrides.empty()) RestoreAll();
        return;
    }

    const ULONGLONG nowMs = GetTickCount64();
    if (g_lastUpdateMs != 0 && nowMs - g_lastUpdateMs < 100) return;
    g_lastUpdateMs = nowMs;

// Gets the most recent player snapshot
    const FrameSnapshot& frame = GetCurrentFrameSnapshot();
    std::unordered_set<uintptr_t> currentPawns;
    currentPawns.reserve(frame.entities.size());
    for (const EntitySnapshot& entity : frame.entities) {
        if (!IsValidPtr(entity.pawn) || entity.lifeState != 0 || entity.health <= 0 ||
            entity.health > 100 || (frame.localTeam != 0 && entity.team == frame.localTeam))
            continue;

        const uintptr_t identity = mem.Read<uintptr_t>(entity.pawn + Offsets::m_pEntityIdentity);
        if (!IsValidPtr(identity)) continue;
        const uintptr_t spottedAddress = entity.pawn + Offsets::m_entitySpottedState + Offsets::m_bSpotted;
        const bool currentSpotted = mem.Read<bool>(spottedAddress);
        auto state = g_overrides.find(entity.pawn);
        if (state == g_overrides.end() || state->second.identity != identity) {
            state = g_overrides.insert_or_assign(entity.pawn,
                RadarOverride{ identity, currentSpotted }).first;
        }
        if (!currentSpotted) WriteSpotted(entity.pawn, true);
        currentPawns.insert(entity.pawn);
    }

    for (auto it = g_overrides.begin(); it != g_overrides.end();) {
        if (currentPawns.find(it->first) == currentPawns.end()) {
            RestoreOne(it->first, it->second);
            it = g_overrides.erase(it);
        } else {
            ++it;
        }
    }
}

void ShutdownRadarHack() {
    RestoreAll();
}
