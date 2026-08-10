// ============================================================
// AntiSmoke.cpp
// Anti-smoke: removes smoke from smoke grenades to see through them.
// ============================================================

#include "AntiSmoke.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include "Stats.h"

#include <array>
#include <cmath>
#include <vector>

namespace {
    struct SmokePatchState {
        uintptr_t address = 0;
        uint8_t originalByte = 0;
        bool scanAttempted = false;
        bool applied = false;
    };

    SmokePatchState g_smokePatch;

    uintptr_t FindSmokePatchImmediate() {
        if (!mem.clientModule || mem.clientModuleSize == 0) return 0;

        std::vector<uint8_t> module(mem.clientModuleSize);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(mem.clientModule),
            module.data(), module.size(), &bytesRead) || bytesRead < 16) return 0;
        Stats::rpmReadCount.fetch_add(1);

        constexpr std::array<uint8_t, 10> prefix = {
            0x8B, 0x51, 0x28, 0x48, 0x89, 0x5C, 0x24, 0x30, 0x83, 0xFA
        };
        constexpr std::array<uint8_t, 5> suffix = { 0x74, 0x21, 0x8B, 0x49, 0x2C };

        uintptr_t match = 0;
        size_t matchCount = 0;
        for (size_t i = 0; i + prefix.size() + 1 + suffix.size() <= bytesRead; ++i) {
            bool prefixMatches = true;
            for (size_t j = 0; j < prefix.size(); ++j) {
                if (module[i + j] != prefix[j]) {
                    prefixMatches = false;
                    break;
                }
            }
            if (!prefixMatches || module[i + prefix.size()] != 0xFF) continue;

            bool suffixMatches = true;
            for (size_t j = 0; j < suffix.size(); ++j) {
                if (module[i + prefix.size() + 1 + j] != suffix[j]) {
                    suffixMatches = false;
                    break;
                }
            }
            if (!suffixMatches) continue;

            match = mem.clientModule + i + prefix.size();
            ++matchCount;
            if (matchCount > 1) return 0;
        }
        return matchCount == 1 ? match : 0;
    }

    bool WriteExecutableByte(uintptr_t address, uint8_t expected, uint8_t replacement) {
        uint8_t current = 0;
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(address),
            &current, sizeof(current), &bytesRead) || bytesRead != sizeof(current) || current != expected)
            return false;

        DWORD oldProtection = 0;
        if (!VirtualProtectEx(mem.hProcess, reinterpret_cast<LPVOID>(address), 1,
            PAGE_EXECUTE_READWRITE, &oldProtection)) return false;

        SIZE_T bytesWritten = 0;
        const bool wrote = WriteProcessMemory(mem.hProcess, reinterpret_cast<LPVOID>(address),
            &replacement, sizeof(replacement), &bytesWritten) && bytesWritten == sizeof(replacement);
        if (wrote) {
            FlushInstructionCache(mem.hProcess, reinterpret_cast<LPCVOID>(address), 1);
            Stats::rpmWriteCount.fetch_add(1);
        }

        DWORD ignoredProtection = 0;
        VirtualProtectEx(mem.hProcess, reinterpret_cast<LPVOID>(address), 1,
            oldProtection, &ignoredProtection);
        return wrote;
    }
}

bool RunAntiSmoke() {
    if (!mem.hProcess || !mem.clientModule) return false;
    if (g_smokePatch.applied) return true;

    if (!g_smokePatch.address && !g_smokePatch.scanAttempted) {
        g_smokePatch.scanAttempted = true;
        g_smokePatch.address = FindSmokePatchImmediate();
    }
    if (!g_smokePatch.address) return false;

    constexpr uint8_t originalImmediate = 0xFF;
    constexpr uint8_t antiSmokeImmediate = 0x00;
    if (!WriteExecutableByte(g_smokePatch.address, originalImmediate, antiSmokeImmediate))
        return false;

    g_smokePatch.originalByte = originalImmediate;
    g_smokePatch.applied = true;
    return true;
}

void RestoreAntiSmoke() {
    if (!g_smokePatch.applied) {
        if (!g_smokePatch.address) g_smokePatch.scanAttempted = false;
        return;
    }
    if (!mem.hProcess || !g_smokePatch.address) return;

    constexpr uint8_t antiSmokeImmediate = 0x00;
    if (WriteExecutableByte(g_smokePatch.address, antiSmokeImmediate, g_smokePatch.originalByte))
        g_smokePatch.applied = false;
}

bool IsAntiSmokeActive() {
    return g_smokePatch.applied;
}

float GetLocalSmokeOverlayAlpha() {
    if (!mem.clientModule) return 0.0f;
// Gets the most recent player snapshot
    const uintptr_t localPawn = GetCurrentFrameSnapshot().localPawn;
    if (!IsValidPtr(localPawn)) return 0.0f;

    const float value = mem.Read<float>(localPawn + Offsets::m_flLastSmokeOverlayAlpha);
    return std::isfinite(value) && value >= 0.0f ? value : 0.0f;
}

bool IsLocalInSmoke(float threshold) {
    return GetLocalSmokeOverlayAlpha() > threshold;
}
