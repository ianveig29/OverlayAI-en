#include "ViewPunch.h"
#include "Types.h"
#include "Memory.h"
#include "Offsets.h"
#include "Stats.h"

#include <cstdint>

namespace {
    // Offset inside the ConVar object to the current-value float
    // (found with Cheat Engine on 12/09/2026: object + 0x58 = value).
    constexpr uintptr_t kConVarValueOffset = 0x58;

    // Internal module state. Anything captured once (the original value)
    // is stored here so we can restore it when the overlay shuts down.
    struct AntiViewPunchState {
        uintptr_t valueAddress = 0;  // address of the ConVar float
        float     originalValue = 0.f; // original value before writing
        bool      hasOriginal = false; // did we capture the original?
        bool      modified = false;    // did we write a different value?
        float     lastReadValue = -1.f; // last read (for the menu)
        int       lastFailReason = 0;   // last error (0 = none)
    };
    AntiViewPunchState g_state;

    // Failure codes shown in the menu:
    //  1 = waiting for the game to be ready (not attached yet)
    //  2 = broken chain: the dwViewPunchDecayConVar offset is stale
    //      (re-derive with Cheat Engine, see ViewPunch.h)
    //  3 = the memory write failed
    //  4 = the float read does not look like a valid decay (dirty read)
}

void UpdateAntiViewPunch() {
    // No game attached yet: not an error, just wait.
    if (!mem.hProcess || !mem.clientModule) {
        g_state.lastFailReason = 1;
        return;
    }

    // Step 1: follow the chain client.dll + dwViewPunchDecayConVar -> object.
    const uintptr_t conVarObject = mem.Read<uintptr_t>(
        mem.clientModule + Offsets::dwViewPunchDecayConVar);
    if (!IsValidPtr(conVarObject)) {
        g_state.lastFailReason = 2; // stale offset after an update
        return;
    }

    // Step 2: read the ConVar's current float (object + 0x58).
    const uintptr_t valueAddress = conVarObject + kConVarValueOffset;
    const float current = mem.Read<float>(valueAddress);
    // Validation: the default decay is 18 and nobody sets it to 0 or to
    // huge numbers. If we read anything weird, the address is not
    // trustworthy (better to not write at all than to corrupt the game).
    if (!(current > 0.f && current < 10000.f)) {
        g_state.lastFailReason = 4;
        return;
    }

    // Step 3: capture the ORIGINAL value once, so we can restore it later.
    if (!g_state.hasOriginal) {
        g_state.originalValue = current;
        g_state.hasOriginal = true;
    }
    g_state.valueAddress = valueAddress;
    g_state.lastReadValue = current;

    // Step 4: act based on the menu checkbox.
    if (!g_Aim.enableAntiViewPunch) {
        // Feature off: restore the original ONLY if WE were the ones who
        // changed it (if the player set it from the console on their own
        // sv_cheats server, we respect their decision).
        if (g_state.modified && current != g_state.originalValue) {
            if (mem.Write<float>(valueAddress, g_state.originalValue)) {
                Stats::rpmWriteCount.fetch_add(1);
                g_state.modified = false;
            } else {
                g_state.lastFailReason = 3;
                return;
            }
        }
        g_state.lastFailReason = 0;
        return;
    }

    // Feature on: write the target decay if it differs. Re-asserted every
    // frame, so if the game resets the ConVar (map change etc.) we put it
    // back without the player noticing anything.
    const float target = static_cast<float>(g_Aim.antiViewPunchDecay);
    if (current != target) {
        if (mem.Write<float>(valueAddress, target)) {
            Stats::rpmWriteCount.fetch_add(1);
            g_state.modified = true;
        } else {
            g_state.lastFailReason = 3;
            return;
        }
    }
    g_state.lastFailReason = 0;
}

void ShutdownAntiViewPunch() {
    // On overlay exit we leave the game as it was.
    if (g_state.modified && g_state.hasOriginal && g_state.valueAddress &&
        mem.hProcess) {
        (void)mem.Write<float>(g_state.valueAddress, g_state.originalValue);
        Stats::rpmWriteCount.fetch_add(1);
    }
    g_state = AntiViewPunchState{};
}

AntiViewPunchStatus GetAntiViewPunchStatus() {
    AntiViewPunchStatus s;
    s.failReason = g_state.lastFailReason;
    s.currentValue = g_state.lastReadValue;
    s.originalValue = g_state.originalValue;
    s.modified = g_state.modified;
    s.active = g_state.modified && g_state.lastFailReason == 0;
    return s;
}
