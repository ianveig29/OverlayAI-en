// ============================================================
// Glow.cpp
// Glow system. Applies a brightness effect to players so they are easily visible, even through walls.
// ============================================================

#include "Glow.h"

#include "Config.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace {
    struct GlowColorRGBA {
        uint8_t r, g, b, a;
    };

    std::unordered_set<uintptr_t> g_activeGlowPawns;
    struct ExpectedGlowState {
        int type = 3;
        GlowColorRGBA color{};
    };
    std::unordered_map<uintptr_t, ExpectedGlowState> g_expectedGlow;
    GlowDiagnostics g_diagnostics;
    ULONGLONG g_lastDiagnosticSampleMs = 0;
    size_t g_diagnosticCursor = 0;

    void GetGlowRgb(bool visible, int& r, int& g, int& b) {
        if (g_Esp.glowUseStaticColor) {
            r = g_Esp.glowStaticR;
            g = g_Esp.glowStaticG;
            b = g_Esp.glowStaticB;
        } else if (visible) {
            r = g_Esp.glowVisibleR;
            g = g_Esp.glowVisibleG;
            b = g_Esp.glowVisibleB;
        } else {
            r = g_Esp.glowInvisibleR;
            g = g_Esp.glowInvisibleG;
            b = g_Esp.glowInvisibleB;
        }
    }

    bool WriteFullGlowState(uintptr_t pawn, const GlowColorRGBA& color) {
        const uintptr_t glowBase = pawn + Offsets::m_Glow;
        bool success = true;
        success = mem.Write<int>(glowBase + Offsets::glow_m_iGlowType, 3) && success;
        success = mem.Write<int>(glowBase + Offsets::glow_m_iGlowTeam, 0) && success;
        success = mem.Write<int>(glowBase + Offsets::glow_m_nGlowRange, 3500) && success;
        success = mem.Write<int>(glowBase + Offsets::glow_m_nGlowRangeMin, 0) && success;
        success = mem.Write<GlowColorRGBA>(glowBase + Offsets::glow_m_glowColorOverride, color) && success;
        const Vector3 colorVector = {
            color.r / 255.0f, color.g / 255.0f, color.b / 255.0f
        };
        success = mem.Write<Vector3>(glowBase + Offsets::glow_m_fGlowColor, colorVector) && success;
        success = mem.Write<bool>(glowBase + Offsets::glow_m_bFlashing, false) && success;
        success = mem.Write<bool>(glowBase + Offsets::glow_m_bEligibleForScreenHighlight, false) && success;
        success = mem.Write<bool>(glowBase + Offsets::glow_m_bGlowing, true) && success;
        success = mem.Write<float>(pawn + Offsets::m_flGlowBackfaceMult, 1.0f) && success;
        return success;
    }

    bool SetPawnGlow(uintptr_t pawn, bool enable, int r, int g, int b, int a) {
        if (!IsValidPtr(pawn)) return false;

        const uintptr_t glowBase = pawn + Offsets::m_Glow;
        if (!enable) {
            return mem.Write<bool>(glowBase + Offsets::glow_m_bGlowing, false);
        }

        const GlowColorRGBA color = {
            static_cast<uint8_t>(r), static_cast<uint8_t>(g),
            static_cast<uint8_t>(b), static_cast<uint8_t>(a)
        };
        const Vector3 colorVector = { r / 255.0f, g / 255.0f, b / 255.0f };
        g_expectedGlow[pawn] = { 3, color };

        // Read one compact snapshot, but repair only documented fields. Writing the
        // whole block can race with client.dll and overwrite game-owned state.
        constexpr size_t blockSize = 0x5C;
        std::array<uint8_t, blockSize> block{};
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(glowBase),
            block.data(), block.size(), &bytesRead) || bytesRead != block.size()) {
            if (g_diagnostics.enabled) ++g_diagnostics.readFailures;
            return WriteFullGlowState(pawn, color);
        }

        bool layoutValid = true;
        bool success = true;
        auto repairValue = [&](uintptr_t offset, const auto& value) {
            using ValueType = std::decay_t<decltype(value)>;
            if (offset + sizeof(ValueType) > block.size()) {
                layoutValid = false;
                return;
            }
            if (std::memcmp(block.data() + offset, &value, sizeof(ValueType)) != 0) {
                success = mem.Write<ValueType>(glowBase + offset, value) && success;
            }
        };

        repairValue(Offsets::glow_m_iGlowType, 3);
        repairValue(Offsets::glow_m_iGlowTeam, 0);
        repairValue(Offsets::glow_m_nGlowRange, 3500);
        repairValue(Offsets::glow_m_nGlowRangeMin, 0);
        repairValue(Offsets::glow_m_glowColorOverride, color);
        repairValue(Offsets::glow_m_fGlowColor, colorVector);
        repairValue(Offsets::glow_m_bFlashing, false);
        repairValue(Offsets::glow_m_bEligibleForScreenHighlight, false);
        repairValue(Offsets::glow_m_bGlowing, true);
        if (Offsets::m_flGlowBackfaceMult >= Offsets::m_Glow)
            repairValue(Offsets::m_flGlowBackfaceMult - Offsets::m_Glow, 1.0f);
        else
            layoutValid = false;

        if (!layoutValid)
            return WriteFullGlowState(pawn, color);
        return success;
    }

    void SampleGlowState(uintptr_t pawn) {
        auto expectedIt = g_expectedGlow.find(pawn);
        if (expectedIt == g_expectedGlow.end()) return;

        const uintptr_t glowBase = pawn + Offsets::m_Glow;
        constexpr size_t propertySize = 0x58;
        std::array<uint8_t, propertySize> property{};
        SIZE_T bytesRead = 0;
        const bool success = ReadProcessMemory(mem.hProcess,
            reinterpret_cast<LPCVOID>(glowBase), property.data(), property.size(),
            &bytesRead) && bytesRead == property.size();

        ++g_diagnostics.samples;
        if (!success) {
            ++g_diagnostics.readFailures;
            g_diagnostics.lastAffectedPawn = pawn;
            return;
        }

        auto readValue = [&](uintptr_t offset, auto& value) -> bool {
            if (offset + sizeof(value) > property.size()) return false;
            std::memcpy(&value, property.data() + offset, sizeof(value));
            return true;
        };
        bool glowing = false;
        bool flashing = false;
        bool eligible = false;
        int type = 0;
        GlowColorRGBA color{};
        float glowTime = 0.0f;
        float glowStartTime = 0.0f;
        const bool parsed = readValue(Offsets::glow_m_bGlowing, glowing) &&
            readValue(Offsets::glow_m_bFlashing, flashing) &&
            readValue(Offsets::glow_m_bEligibleForScreenHighlight, eligible) &&
            readValue(Offsets::glow_m_iGlowType, type) &&
            readValue(Offsets::glow_m_glowColorOverride, color) &&
            readValue(Offsets::glow_m_flGlowTime, glowTime) &&
            readValue(Offsets::glow_m_flGlowStartTime, glowStartTime);
        if (!parsed) {
            ++g_diagnostics.readFailures;
            g_diagnostics.lastAffectedPawn = pawn;
            return;
        }
        if (!glowing) {
            ++g_diagnostics.gameDisabledGlow;
            g_diagnostics.lastAffectedPawn = pawn;
        }
        const GlowColorRGBA& expected = expectedIt->second.color;
        if (type != expectedIt->second.type || color.r != expected.r || color.g != expected.g ||
            color.b != expected.b || color.a != expected.a) {
            ++g_diagnostics.alteredProperties;
            g_diagnostics.lastAffectedPawn = pawn;
        }
        if (!glowing || type != expectedIt->second.type) {
            g_diagnostics.lastGlowTime = glowTime;
            g_diagnostics.lastGlowStartTime = glowStartTime;
            g_diagnostics.lastFlashing = flashing;
            g_diagnostics.lastEligible = eligible;
        }
    }
}

void RunGlow() {
    if (!mem.clientModule) return;

// Gets the most recent player snapshot
    const FrameSnapshot& frame = GetCurrentFrameSnapshot();
    std::unordered_set<uintptr_t> currentPawns;
    currentPawns.reserve(frame.entities.size());

    uintptr_t diagnosticPawn = 0;
    const ULONGLONG nowMs = GetTickCount64();
    if (g_diagnostics.enabled && !g_activeGlowPawns.empty() &&
        nowMs - g_lastDiagnosticSampleMs >= 100) {
        const size_t selected = g_diagnosticCursor++ % g_activeGlowPawns.size();
        auto selectedIt = g_activeGlowPawns.begin();
        std::advance(selectedIt, selected);
        diagnosticPawn = *selectedIt;
        g_lastDiagnosticSampleMs = nowMs;
    }

    for (const EntitySnapshot& snap : frame.entities) {
        if (!IsValidPtr(snap.pawn) || snap.pawn == frame.localPawn) continue;

        const bool alive = snap.lifeState == 0 && snap.health > 0 && snap.health <= 100;
        const bool teammate = frame.localTeam != 0 && snap.team == frame.localTeam;
        const bool shouldGlow = g_Esp.enableGlow && alive &&
            (!teammate || g_Esp.showTeammateGlow);

        if (!shouldGlow) {
            if (alive && g_activeGlowPawns.erase(snap.pawn) != 0)
                if (!SetPawnGlow(snap.pawn, false, 0, 0, 0, 0) && g_diagnostics.enabled)
                    ++g_diagnostics.writeFailures;
            g_expectedGlow.erase(snap.pawn);
            continue;
        }

        if (snap.pawn == diagnosticPawn)
            SampleGlowState(snap.pawn);

        int r = 0, g = 0, b = 0;
        GetGlowRgb(IsPawnVisibleInSnapshot(snap, frame.localPlayerIndex), r, g, b);
        if (!SetPawnGlow(snap.pawn, true, r, g, b, g_Esp.glowAlpha) && g_diagnostics.enabled)
            ++g_diagnostics.writeFailures;
        currentPawns.insert(snap.pawn);
    }

    // Missing pawns may already have been destroyed or recycled. Forget them
    // without writing to stale addresses.
    for (auto it = g_activeGlowPawns.begin(); it != g_activeGlowPawns.end();) {
        if (currentPawns.find(*it) == currentPawns.end()) {
            if (g_diagnostics.enabled) ++g_diagnostics.snapshotGaps;
            g_expectedGlow.erase(*it);
            it = g_activeGlowPawns.erase(it);
        } else
            ++it;
    }
    g_activeGlowPawns.insert(currentPawns.begin(), currentPawns.end());
    g_diagnostics.activePawns = static_cast<int>(g_activeGlowPawns.size());
}

void ShutdownGlow() {
    if (mem.clientModule) {
// Gets the most recent player snapshot
        const FrameSnapshot& frame = GetCurrentFrameSnapshot();
        for (const EntitySnapshot& snap : frame.entities) {
            if (g_activeGlowPawns.find(snap.pawn) != g_activeGlowPawns.end() &&
                snap.lifeState == 0 && snap.health > 0 && snap.health <= 100) {
                SetPawnGlow(snap.pawn, false, 0, 0, 0, 0);
            }
        }
    }
    g_activeGlowPawns.clear();
    g_expectedGlow.clear();
    g_diagnostics.activePawns = 0;
}

void SetGlowDiagnosticsEnabled(bool enabled) {
    g_diagnostics.enabled = enabled;
    g_lastDiagnosticSampleMs = 0;
}

void ResetGlowDiagnostics() {
    const bool enabled = g_diagnostics.enabled;
    g_diagnostics = {};
    g_diagnostics.enabled = enabled;
    g_lastDiagnosticSampleMs = 0;
    g_diagnosticCursor = 0;
}

const GlowDiagnostics& GetGlowDiagnostics() {
    return g_diagnostics;
}
