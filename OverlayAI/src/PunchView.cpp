#include "PunchView.h"
#include "Types.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"

#include <cmath>

// ============================================================================
// QUIT PUNCHVIEW IMPLEMENTATION
// ----------------------------------------------------------------------------
// Full flow, step by step:
//   1) Get the "pawn" (our player's body in memory).
//   2) Read the pointer to the camera services (m_pCameraServices).
//   3) Read the kick angle (m_vecCsViewPunchAngle, 3 floats: x, y, z).
//   4) If the angle is not (0,0,0), write (0,0,0) on top of it.
// Repeated every frame, the kick never becomes visible on screen.
// ============================================================================

namespace {

    // How many kicks we have neutralized since the feature was enabled.
    // Used to show visible proof in the menu that it is working.
    unsigned g_correctionCount = 0;

    // Returns the local player's pawn, with a fallback to the direct
    // offset if the frame snapshot is not ready yet (same criteria as
    // AntiFlash).
    uintptr_t GetLocalPawnFromSnapshot() {
        const uintptr_t pawn = GetCurrentFrameSnapshot().localPawn;
        if (IsValidPtr(pawn)) return pawn;
        const uintptr_t directPawn = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerPawn);
        return IsValidPtr(directPawn) ? directPawn : 0;
    }

    // An angle is considered "zero" if all 3 components are closer to
    // zero than this margin. Avoids writing again when there is nothing
    // left to neutralize (fewer writes = safer).
    constexpr float kZeroEpsilon = 0.001f;

    // Neutralizes an angle (3 consecutive floats) at the given address.
    // Reads first: if it is already (0,0,0), writes nothing.
    // Returns true only if a correction was needed.
    bool NeutralizeAngleAt(uintptr_t address) {
        const Vector3 angle = mem.Read<Vector3>(address);
        if (!std::isfinite(angle.x) || !std::isfinite(angle.y) || !std::isfinite(angle.z))
            return false;
        if (std::fabs(angle.x) <= kZeroEpsilon &&
            std::fabs(angle.y) <= kZeroEpsilon &&
            std::fabs(angle.z) <= kZeroEpsilon)
            return false;
        const Vector3 zero{ 0.0f, 0.0f, 0.0f };
        (void)mem.Write<Vector3>(address, zero);
        return true;
    }

} // namespace

void RunQuitPunchview() {
    // If the feature is turned off in the menu, do nothing.
    if (!g_Esp.quitPunchview) return;
    if (!mem.hProcess || !mem.clientModule) return;

    // If the offsets failed to load (auto-update failed and no cache),
    // do not try to write to bogus addresses.
    if (!Offsets::m_pCameraServices || !Offsets::m_vecCsViewPunchAngle) return;

    // Step 1: the local player's pawn.
    const uintptr_t pawn = GetLocalPawnFromSnapshot();
    if (!IsValidPtr(pawn)) return;

    // Step 2: the pawn's pointer to its camera services.
    const uintptr_t cameraServices = mem.Read<uintptr_t>(pawn + Offsets::m_pCameraServices);
    if (!IsValidPtr(cameraServices)) return;

    // Step 3: read the kick angle (3 consecutive floats: pitch, yaw, roll).
    const Vector3 punch = mem.Read<Vector3>(cameraServices + Offsets::m_vecCsViewPunchAngle);
    if (!std::isfinite(punch.x) || !std::isfinite(punch.y) || !std::isfinite(punch.z)) return;

    // Step 4: if there is a kick, neutralize it by writing zeros.
    if (std::fabs(punch.x) > kZeroEpsilon ||
        std::fabs(punch.y) > kZeroEpsilon ||
        std::fabs(punch.z) > kZeroEpsilon) {
        const Vector3 zero{ 0.0f, 0.0f, 0.0f };
        (void)mem.Write<Vector3>(cameraServices + Offsets::m_vecCsViewPunchAngle, zero);
        ++g_correctionCount;
    }
}

unsigned GetPunchViewCorrectionCount() {
    return g_correctionCount;
}

// ============================================================================
// QUIT AIM PUNCH (VISUAL RECOIL)
// ----------------------------------------------------------------------------
// Aim punch lives somewhere else than view punch: inside the pawn's
// "aim punch services". There are TWO angles that add up to the visible
// recoil:
//   - m_predictableBaseAngle: the predictable part (the weapon pattern),
//     accompanied by its velocity (m_predictableBaseAngleVel).
//   - m_unpredictableBaseAngle: the unpredictable part (random variation).
// We neutralize all three every frame: predictable angle, its velocity and
// the unpredictable one. Without velocity, the predictable angle does not
// grow between frames either, so we write less often.
//
// REMEMBER: bullets follow the server's REAL pattern. This is purely
// cosmetic for your camera (see the warning in PunchView.h).
// ============================================================================

namespace {
    // Counter of frames where aim punch had to be neutralized (for the menu).
    unsigned g_aimCorrectionCount = 0;
}

void RunQuitAimPunch() {
    // If the feature is turned off in the menu, do nothing.
    if (!g_Esp.quitAimPunch) return;
    if (!mem.hProcess || !mem.clientModule) return;

    // Without valid offsets (failed auto-update), do not write anything.
    if (!Offsets::m_pAimPunchServices ||
        !Offsets::m_predictableBaseAngle ||
        !Offsets::m_predictableBaseAngleVel ||
        !Offsets::m_unpredictableBaseAngle) return;

    // Step 1: the local player's pawn.
    const uintptr_t pawn = GetLocalPawnFromSnapshot();
    if (!IsValidPtr(pawn)) return;

    // Step 2: the pawn's pointer to its aim punch services.
    const uintptr_t aimServices = mem.Read<uintptr_t>(pawn + Offsets::m_pAimPunchServices);
    if (!IsValidPtr(aimServices)) return;

    // Step 3: neutralize the three values (predictable angle, its velocity
    // and the unpredictable angle). If any was corrected, count the frame.
    bool corrected = false;
    if (NeutralizeAngleAt(aimServices + Offsets::m_predictableBaseAngle)) corrected = true;
    if (NeutralizeAngleAt(aimServices + Offsets::m_predictableBaseAngleVel)) corrected = true;
    if (NeutralizeAngleAt(aimServices + Offsets::m_unpredictableBaseAngle)) corrected = true;
    if (corrected) ++g_aimCorrectionCount;
}

unsigned GetAimPunchCorrectionCount() {
    return g_aimCorrectionCount;
}
