#include "RecoilControl.h"
#include "Types.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include "Config.h"

#include <cmath>

// ============================================================================
// RCS IMPLEMENTATION
// ----------------------------------------------------------------------------
// Per-frame formula (P being the current punch and P_prev the previous
// frame's punch):
//
//   delta = P - P_prev                    (how much the recoil grew)
//   view  = view - delta * strength       (strength = rcsStrengthPercent/100)
//   P_prev = P                            (saved for the next frame)
//
// With punch in degrees and view angles in degrees, the subtraction is
// direct. We only write when the delta exceeds a minimum (avoids writes
// caused by network noise or float rounding while punch is still).
// ============================================================================

namespace {

    // Previous frame's punch (x=pitch, y=yaw, z=roll). Starts at zero.
    Vector3 g_prevPunch{ 0.0f, 0.0f, 0.0f };

    // Counter of compensated frames (for the menu).
    unsigned g_compensationCount = 0;

    // Minimum delta in degrees worth writing for. Below this the movement
    // is imperceptible and the write is not worth it.
    constexpr float kMinDeltaDegrees = 0.01f;

    // Ceiling for a legitimate per-frame delta (degrees). A larger delta
    // is not real recoil but a bad read; explained in RunRCS.
    constexpr float kMaxDeltaPerFrameDegrees = 5.0f;

    // Returns the local player's pawn (same criteria as AntiFlash).
    uintptr_t GetLocalPawnFromSnapshot() {
        const uintptr_t pawn = GetCurrentFrameSnapshot().localPawn;
        if (IsValidPtr(pawn)) return pawn;
        const uintptr_t directPawn = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerPawn);
        return IsValidPtr(directPawn) ? directPawn : 0;
    }

    // Reads an angle (3 floats). Returns false if any value is not finite.
    bool ReadAngle(uintptr_t address, Vector3& out) {
        out = mem.Read<Vector3>(address);
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
    }

} // namespace

void RunRCS() {
    // Without the feature enabled we do nothing (and reset the reference
    // so the next activation starts clean).
    if (!g_Aim.recoilControlSystem) {
        g_prevPunch = { 0.0f, 0.0f, 0.0f };
        return;
    }
    if (!mem.hProcess || !mem.clientModule) return;

    // Without valid offsets (failed auto-update), do not touch anything.
    if (!Offsets::dwViewAngles ||
        !Offsets::m_pAimPunchServices ||
        !Offsets::m_predictableBaseAngle ||
        !Offsets::m_unpredictableBaseAngle) return;

    // Step 1: the local pawn. No pawn (dead, spectating) means reset.
    const uintptr_t pawn = GetLocalPawnFromSnapshot();
    if (!IsValidPtr(pawn)) {
        g_prevPunch = { 0.0f, 0.0f, 0.0f };
        return;
    }

    // Step 2: the pawn's aim punch services.
    const uintptr_t aimServices = mem.Read<uintptr_t>(pawn + Offsets::m_pAimPunchServices);
    if (!IsValidPtr(aimServices)) {
        g_prevPunch = { 0.0f, 0.0f, 0.0f };
        return;
    }

    // Step 3: TOTAL punch = predictable part + unpredictable part.
    // Bullets deflect by the sum of both; compensating only one would
    // leave the impact point shifted.
    Vector3 predictable{ 0.0f, 0.0f, 0.0f };
    Vector3 unpredictable{ 0.0f, 0.0f, 0.0f };
    if (!ReadAngle(aimServices + Offsets::m_predictableBaseAngle, predictable)) return;
    if (!ReadAngle(aimServices + Offsets::m_unpredictableBaseAngle, unpredictable)) return;

    const Vector3 punch{
        predictable.x + unpredictable.x,
        predictable.y + unpredictable.y,
        predictable.z + unpredictable.z
    };

    // Step 4: delta against the previous frame, scaled by strength.
    const float strength = (g_Aim.rcsStrengthPercent < 0) ? 0.0f :
        (g_Aim.rcsStrengthPercent > 100) ? 100.0f :
        static_cast<float>(g_Aim.rcsStrengthPercent) / 100.0f;

    const Vector3 rawDelta{
        punch.x - g_prevPunch.x,
        punch.y - g_prevPunch.y,
        punch.z - g_prevPunch.z
    };
    const Vector3 delta{
        rawDelta.x * strength,
        rawDelta.y * strength,
        rawDelta.z * strength
    };

    // Nothing to compensate (the punch did not change): do not write.
    if (std::fabs(delta.x) < kMinDeltaDegrees &&
        std::fabs(delta.y) < kMinDeltaDegrees &&
        std::fabs(delta.z) < kMinDeltaDegrees) {
        g_prevPunch = punch;
        return;
    }

    // Guard against glitch reads (real-world case documented in public
    // cheats like deadlocked): mid-spray the game can report a FAKE punch
    // for one frame, typically (0,0,0). Without this guard that
    // accumulated delta would be applied at once and the aim would yank
    // UP several degrees in a single frame (the "possessed camera"
    // effect). Legitimate ceiling: the AK fires 10 shots per second; even
    // at low FPS the real punch grows a fraction of a degree per frame. A
    // 5-degree delta in one frame is not recoil: it is a bad read.
    if (std::fabs(rawDelta.x) > kMaxDeltaPerFrameDegrees ||
        std::fabs(rawDelta.y) > kMaxDeltaPerFrameDegrees ||
        std::fabs(rawDelta.z) > kMaxDeltaPerFrameDegrees) {
        // Punch read as exactly zero with a high reference: it is the
        // transient glitch. Discard the WHOLE frame (no write, no
        // reference update), same as deadlocked does: when the real
        // value comes back the delta is 0 and there is no jump.
        if (punch.x == 0.0f && punch.y == 0.0f && punch.z == 0.0f) return;
        // If instead it is a real jump (RCS enabled in the middle of an
        // already advanced spray), sync the reference without writing:
        // compensation starts from the current state, no yank.
        g_prevPunch = punch;
        return;
    }

    g_prevPunch = punch;

    // Never move the aim while the cheat menu is open. The punch was
    // already updated above (g_prevPunch), so when the menu closes there
    // is NO accumulated jump: only the current frame's delta is applied,
    // as always.
    if (g_MenuOpen) return;

    // Step 5: read the current view angles and subtract the delta.
    // dwViewAngles is already the absolute address inside client.dll.
    Vector3 view{ 0.0f, 0.0f, 0.0f };
    if (!ReadAngle(mem.clientModule + Offsets::dwViewAngles, view)) return;

    const Vector3 compensated{
        view.x - delta.x,
        view.y - delta.y,
        view.z - delta.z
    };
    (void)mem.Write<Vector3>(mem.clientModule + Offsets::dwViewAngles, compensated);
    ++g_compensationCount;
}

unsigned GetRCSCompensationCount() {
    return g_compensationCount;
}
