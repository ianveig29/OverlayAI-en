#include "RecoilControl.h"
#include "Types.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include "Config.h"

#include <cmath>
#include <chrono>

// ============================================================================
// RCS IMPLEMENTATION
// ----------------------------------------------------------------------------
// Per-frame formula (P being the current punch, P_prev the previous
// frame's punch, and vel the predictable punch velocity):
//
//   P*    = P + vel * dt        (feed-forward: projects punch 1 frame ahead)
//   delta = P* - P_prev         (how much recoil is ABOUT to grow)
//   view  = view - delta * strength (strength: vertical/horizontal split)
//   P_prev = P*                 (saved for the next frame)
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

    // Clock of the previous frame: its dt feeds the feed-forward
    // projection and scales the anti-glitch ceiling (at low fps legit
    // punch accumulates more degrees per frame, and the fixed ceiling
    // used to clip real compensation).
    std::chrono::steady_clock::time_point g_lastFrame =
        std::chrono::steady_clock::now();

    // Punch velocity smoothed (EMA) and feed-forward limits.
    constexpr float kFfAlpha = 0.25f;   // converges in ~4 frames
    // Projection cap per frame. Previously 0.35: at 64 ticks the real
    // impulse of a bullet can exceed that and the feed-forward was being
    // clipped right at the peak, where it matters most (Dust 2 spray
    // test: the FF "was not noticeable"). 1.0 degrees covers the
    // per-bullet impulse at low fps without letting a glitch jerk the aim.
    constexpr float kMaxFFDegrees = 1.0f;
    Vector3 g_ffVel{ 0.0f, 0.0f, 0.0f };
    // Cold start: with the EMA starting at 0 and alpha 0.25, the trend
    // took ~4 frames to "catch up" - exactly the first shots of the
    // spray, where recoil grows the fastest. With the seed, the FIRST
    // velocity reading goes in raw and the projection contributes from
    // the second bullet instead of ramping up.
    bool g_ffSeeded = false;

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
        g_ffVel = { 0.0f, 0.0f, 0.0f }; g_ffSeeded = false;
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
        g_ffVel = { 0.0f, 0.0f, 0.0f }; g_ffSeeded = false;
        return;
    }

    // Step 2: the pawn's aim punch services.
    const uintptr_t aimServices = mem.Read<uintptr_t>(pawn + Offsets::m_pAimPunchServices);
    if (!IsValidPtr(aimServices)) {
        g_prevPunch = { 0.0f, 0.0f, 0.0f };
        g_ffVel = { 0.0f, 0.0f, 0.0f }; g_ffSeeded = false;
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

    // Step 3b: frame dt and feed-forward. The engine stores in
    // m_predictableBaseAngleVel the velocity at which the predictable
    // punch grows (degrees per second). Projecting it one frame ahead
    // (target = punch + vel * dt) keeps the crosshair pre-positioned for
    // the punch that is COMING instead of always reacting one frame late:
    // the bullet leaves with the angle already compensated. With
    // feed-forward off (or if the vel read fails) target = punch and it
    // behaves exactly like before (purely reactive).
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_lastFrame).count();
    g_lastFrame = now;
    if (dt > 0.25f) dt = 0.25f;  // freeze/alt-tab: do not over-predict
    if (dt < 0.0f) dt = 0.0f;

    Vector3 target = punch;
    if (g_Aim.rcsFeedForward && Offsets::m_predictableBaseAngleVel != 0) {
        Vector3 vel{ 0.0f, 0.0f, 0.0f };
        if (ReadAngle(aimServices + Offsets::m_predictableBaseAngleVel, vel)) {
            // EMA over the velocity: the raw value oscillates frame to
            // frame (per-bullet impulse + decay between bullets) and
            // projecting the raw value made the crosshair VIBRATE (frame
            // to frame shaking plus constant writes fighting the engine =
            // the "performance problem"). The EMA tracks the punch TREND,
            // not the instantaneous noise.
            if (!g_ffSeeded) {
                g_ffVel = vel;   // first reading after a reset: raw
                g_ffSeeded = true;
            } else {
                g_ffVel.x += (vel.x - g_ffVel.x) * kFfAlpha;
                g_ffVel.y += (vel.y - g_ffVel.y) * kFfAlpha;
                g_ffVel.z += (vel.z - g_ffVel.z) * kFfAlpha;
            }

            // Only project GROWTH: if the trend says the punch is SHRINKING
            // (decay between bullets) there is nothing to lead with (the
            // negative delta gets compensated when it happens); leading it
            // caused overshoot. And the projection never exceeds the
            // per-frame cap (anti-jump if dt misbehaves).
            const float ffx = g_ffVel.x * dt;
            const float ffy = g_ffVel.y * dt;
            const float ffz = g_ffVel.z * dt;
            Vector3 projection{ 0.0f, 0.0f, 0.0f };
            if (ffx * punch.x > 0.0f) projection.x = ffx;  // same sign = growing
            if (ffy * punch.y > 0.0f) projection.y = ffy;
            if (ffz * punch.z > 0.0f) projection.z = ffz;
            if (projection.x >  kMaxFFDegrees) projection.x =  kMaxFFDegrees;
            if (projection.x < -kMaxFFDegrees) projection.x = -kMaxFFDegrees;
            if (projection.y >  kMaxFFDegrees) projection.y =  kMaxFFDegrees;
            if (projection.y < -kMaxFFDegrees) projection.y = -kMaxFFDegrees;
            if (projection.z >  kMaxFFDegrees) projection.z =  kMaxFFDegrees;
            if (projection.z < -kMaxFFDegrees) projection.z = -kMaxFFDegrees;

            target = Vector3{ punch.x + projection.x,
                              punch.y + projection.y,
                              punch.z + projection.z };
        }
    }

    // Step 3b: firing gate, adapted from the public recoilControl() in
    // tim_apple (github.com/kristofhracza/tim_apple, features/aim.cpp).
    // With m_iShotsFired loaded by the auto-updater, we compensate ONLY
    // while a spray is active (more than 1 bullet fired). When the trigger
    // is released the reference syncs silently and the RCS stops fighting
    // the natural punch recovery (before, the crosshair kept "correcting"
    // for a moment after the burst ended). If the offset never loaded (0),
    // the gate stays disabled and behavior is unchanged.
    if (Offsets::m_iShotsFired != 0) {
        const int shotsFired = mem.Read<int>(pawn + Offsets::m_iShotsFired);
        if (shotsFired <= 1) {
            g_prevPunch = target;
            g_ffVel = { 0.0f, 0.0f, 0.0f }; g_ffSeeded = false;  // the spray ended: drop the stale trend
            return;
        }
    }

    // Step 4: delta against the previous frame, scaled by strength.
    // Vertical strength (pitch, punch axis X): up to 200%. 100% leaves a
    // small residual because the client punch is an approximation of the
    // real recoil (server-side since the April 2026 update); the excess
    // eats that residual AND additionally pushes the aim opposite to the
    // recoil (aggressive overcompensation, Dust 2 spray test). Horizontal
    // strength is separate (less % keeps control during spray transfers
    // between players).
    const float strengthVert = (g_Aim.rcsStrengthPercent < 0) ? 0.0f :
        (g_Aim.rcsStrengthPercent > 200) ? 2.0f :
        static_cast<float>(g_Aim.rcsStrengthPercent) / 100.0f;
    const float strengthHoriz =
        (g_Aim.rcsStrengthHorizontalPercent < 0) ? 0.0f :
        (g_Aim.rcsStrengthHorizontalPercent > 200) ? 2.0f :
        static_cast<float>(g_Aim.rcsStrengthHorizontalPercent) / 100.0f;

    const Vector3 rawDelta{
        target.x - g_prevPunch.x,
        target.y - g_prevPunch.y,
        target.z - g_prevPunch.z
    };
    const Vector3 delta{
        rawDelta.x * strengthVert,
        rawDelta.y * strengthHoriz,
        rawDelta.z * strengthHoriz
    };

    // Nothing to compensate (the punch did not change): do not write.
    if (std::fabs(delta.x) < kMinDeltaDegrees &&
        std::fabs(delta.y) < kMinDeltaDegrees &&
        std::fabs(delta.z) < kMinDeltaDegrees) {
        g_prevPunch = target;
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
    // Ceiling scaled by frame time: the base ceiling (5 degrees) assumes
    // ~60 fps; at lower fps legit punch accumulates more per frame and the
    // fixed ceiling used to clip real compensation (under-compensating).
    // dt is already saturated above, so a freeze does not inflate it.
    const float maxDeltaThisFrame = kMaxDeltaPerFrameDegrees *
        (dt < 0.0167f ? 1.0f : dt / 0.0167f);

    if (std::fabs(rawDelta.x) > maxDeltaThisFrame ||
        std::fabs(rawDelta.y) > maxDeltaThisFrame ||
        std::fabs(rawDelta.z) > maxDeltaThisFrame) {
        // Punch read as exactly zero with a high reference: it is the
        // transient glitch. Discard the WHOLE frame (no write, no
        // reference update), same as deadlocked does: when the real
        // value comes back the delta is 0 and there is no jump.
        if (punch.x == 0.0f && punch.y == 0.0f && punch.z == 0.0f) return;
        // If instead it is a real jump (RCS enabled in the middle of an
        // already advanced spray), sync the reference without writing:
        // compensation starts from the current state, no yank.
        g_prevPunch = target;
        return;
    }

    g_prevPunch = target;

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
