// ============================================================
// Aimlock.cpp
// Aimlock: helps aiming. Calculates which enemy is closest to the cursor and adjusts the aim towards them.
// ============================================================

#include "Aimlock.h"
#include "AntiFlash.h"
#include "AntiSmoke.h"
#include "Config.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include "Skeleton.h"
#include "WorldTransform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <windows.h>

namespace {
    uintptr_t g_lockedAimPawn = 0;

    struct AimCandidate {
        const EntitySnapshot* snapshot = nullptr;
        Vector3 origin{};
        float coarseDistance = 0.0f;
    };

    void ResetAimTarget() {
        g_lockedAimPawn = 0;
    }

    bool IsGameInputActive() {
        const HWND foregroundWindow = GetForegroundWindow();
        if (!foregroundWindow || IsIconic(foregroundWindow) || !IsWindowVisible(foregroundWindow))
            return false;

        DWORD foregroundPid = 0;
        GetWindowThreadProcessId(foregroundWindow, &foregroundPid);
        return foregroundPid == mem.pid;
    }

    void SendMouseMove(long dx, long dy) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        SendInput(1, &input, sizeof(INPUT));
    }

    float ScreenDistance(const Vector3& point, float centerX, float centerY) {
        const float dx = point.x - centerX;
        const float dy = point.y - centerY;
        return std::sqrt(dx * dx + dy * dy);
    }

    Vector3 GetCurrentOrigin(const EntitySnapshot& snapshot) {
        if (IsValidPtr(snapshot.sceneNode)) {
            const Vector3 origin = mem.Read<Vector3>(snapshot.sceneNode + Offsets::m_vecAbsOrigin);
            if (std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z) &&
                (std::fabs(origin.x) > 1.0f || std::fabs(origin.y) > 1.0f || std::fabs(origin.z) > 1.0f))
                return origin;
        }
        return snapshot.worldPos;
    }

    std::array<Vector3, 3> GetFallbackAimPoints(const Vector3& origin) {
        return { Vector3{ origin.x, origin.y, origin.z + g_Aim.headOffset },
            Vector3{ origin.x, origin.y, origin.z + g_Aim.torsoOffset },
            Vector3{ origin.x, origin.y, origin.z + g_Aim.legOffset } };
    }

    bool SelectProjectedPoint(const std::array<Vector3, 3>& points, const Matrix4x4& matrix,
        int screenWidth, int screenHeight, float centerX, float centerY,
        Vector3& selectedScreen, float& selectedDistance) {
        std::array<Vector3, 3> projected{};
        std::array<bool, 3> valid{};
        for (size_t i = 0; i < points.size(); ++i)
// Converts a 3D world position to 2D screen coordinates
            valid[i] = WorldToScreen(points[i], projected[i], matrix, screenWidth, screenHeight);

        if (g_Aim.targetPart >= 0 && g_Aim.targetPart <= 2) {
            const int index = g_Aim.targetPart;
            if (!valid[index]) return false;
            selectedScreen = projected[index];
            selectedDistance = ScreenDistance(selectedScreen, centerX, centerY);
            return true;
        }

        selectedDistance = 1e9f;
        bool found = false;
        for (size_t i = 0; i < projected.size(); ++i) {
            if (!valid[i]) continue;
            const float distance = ScreenDistance(projected[i], centerX, centerY);
            if (distance < selectedDistance) {
                selectedDistance = distance;
                selectedScreen = projected[i];
                found = true;
            }
        }
        return found;
    }

    std::array<Vector3, 3> GetBoneAimPoints(uintptr_t pawn, const Vector3& origin,
        Vector3& alternateLeg, bool& hasAlternateLeg) {
        std::array<Vector3, 3> points = GetFallbackAimPoints(origin);
        alternateLeg = points[2];
        hasAlternateLeg = false;
        SkeletonPose pose{};
        if (!ReadSkeletonPose(pawn, origin, pose)) return points;

        if (pose.valid[7]) points[0] = pose.bones[7];
        if (pose.valid[4]) points[1] = pose.bones[4];

        if (pose.valid[18] && pose.valid[21]) {
            points[2] = pose.bones[18];
            alternateLeg = pose.bones[21];
            hasAlternateLeg = true;
        } else if (pose.valid[18]) {
            points[2] = pose.bones[18];
        } else if (pose.valid[21]) {
            points[2] = pose.bones[21];
        }
        return points;
    }
}

float GetAimFovDegreesForScope(int scopeLevel) {
    if (!g_Aim.useScopedFov || scopeLevel <= 0) return g_Aim.fovDegrees;
    return scopeLevel >= 2 ? g_Aim.doubleScopeFovDegrees : g_Aim.singleScopeFovDegrees;
}

float CalculateAimFovRadiusPixels(const Matrix4x4& viewMatrix,
    int screenWidth, int screenHeight, float fovDegrees) {
    if (screenWidth <= 0 || screenHeight <= 0 || !std::isfinite(fovDegrees)) return 0.0f;

    // dwViewMatrix combines view and projection. Row magnitudes recover the
    // projection scale without making the radius depend on camera rotation.
    const float horizontalScale = std::sqrt(
        viewMatrix.m[0][0] * viewMatrix.m[0][0] +
        viewMatrix.m[0][1] * viewMatrix.m[0][1] +
        viewMatrix.m[0][2] * viewMatrix.m[0][2]);
    const float verticalScale = std::sqrt(
        viewMatrix.m[1][0] * viewMatrix.m[1][0] +
        viewMatrix.m[1][1] * viewMatrix.m[1][1] +
        viewMatrix.m[1][2] * viewMatrix.m[1][2]);
    const float focalX = horizontalScale * screenWidth * 0.5f;
    const float focalY = verticalScale * screenHeight * 0.5f;
    const float focalLength = (focalX > 1.0f && focalY > 1.0f)
        ? (focalX + focalY) * 0.5f
        : (std::max)(focalX, focalY);
    if (!std::isfinite(focalLength) || focalLength <= 1.0f) return 0.0f;

    constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
    const float halfAngle = (std::clamp)(fovDegrees, 0.1f, 89.0f) * kDegreesToRadians;
    const float radius = focalLength * std::tan(halfAngle);
    return std::isfinite(radius) ? radius : 0.0f;
}

void PollAimKeyBind() {
    if (!g_Aim.waitingForHoldKey) return;
    for (int vk = 1; vk < 256; ++vk) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
        if (GetAsyncKeyState(vk) & 0x8000) {
            g_Aim.holdKeyVk = vk;
            g_Aim.waitingForHoldKey = false;
            break;
        }
    }
}

void RunAimlock(int screenWidth, int screenHeight) {
    if (!g_Aim.enabled || !mem.clientModule || g_MenuOpen || screenWidth <= 0 || screenHeight <= 0) {
        ResetAimTarget();
        return;
    }
    // SendInput affects the system cursor. Never emit input while CS2 is
    // minimized or another application owns the foreground window.
    if (!IsGameInputActive()) {
        ResetAimTarget();
        return;
    }
    if (g_Aim.requireHoldKey && (GetAsyncKeyState(g_Aim.holdKeyVk) & 0x8000) == 0) {
        ResetAimTarget();
        return;
    }

// Gets the most recent player snapshot
    const FrameSnapshot& frame = GetCurrentFrameSnapshot();
    if (!IsValidPtr(frame.localPawn)) {
        ResetAimTarget();
        return;
    }

    Matrix4x4 coarseMatrix{};
    if (!ReadViewMatrix(coarseMatrix)) return;

    const float centerX = screenWidth * 0.5f;
    const float centerY = screenHeight * 0.5f;
    const float configuredFov = GetAimFovDegreesForScope(frame.localScopeLevel);
    const float coarseFovRadius = CalculateAimFovRadiusPixels(
        coarseMatrix, screenWidth, screenHeight, configuredFov);
    if (coarseFovRadius <= 0.0f) return;

    const bool localFlashed = IsLocalFlashed(g_Esp.flashThreshold);
    if (localFlashed && !g_Aim.allowWhenFlashed) return;
    const bool bypassVisibilityForFlash = localFlashed && g_Aim.allowWhenFlashed;
    // Smoke between the local player and the target does not raise the local
    // overlay alpha. The explicit option must therefore bypass spotted state
    // independently of the client-side render patch state.
    const bool bypassVisibilityForSmoke = g_Aim.allowWhenInSmoke;

    std::vector<AimCandidate> candidates;
    candidates.reserve(frame.entities.size());
    for (const EntitySnapshot& snapshot : frame.entities) {
        if (!IsValidPtr(snapshot.pawn) || snapshot.pawn == frame.localPawn) continue;
        if (snapshot.lifeState != 0 || snapshot.health <= 0 || snapshot.health > 100) continue;
        if (frame.localTeam != 0 && snapshot.team == frame.localTeam) continue;
        if (g_Aim.requireVisible && !bypassVisibilityForFlash && !bypassVisibilityForSmoke &&
            !IsPawnVisibleInSnapshot(snapshot, frame.localPlayerIndex)) continue;

        const Vector3 origin = GetCurrentOrigin(snapshot);
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) continue;

        Vector3 coarseScreen{};
        float coarseDistance = 0.0f;
        if (!SelectProjectedPoint(GetFallbackAimPoints(origin), coarseMatrix,
            screenWidth, screenHeight, centerX, centerY, coarseScreen, coarseDistance)) continue;
        if (coarseDistance > coarseFovRadius) continue;

        candidates.push_back({ &snapshot, origin, coarseDistance });
    }

    if (candidates.empty()) {
        ResetAimTarget();
        return;
    }

    const AimCandidate* selected = nullptr;
    for (const AimCandidate& candidate : candidates) {
        if (candidate.snapshot->pawn == g_lockedAimPawn) {
            selected = &candidate;
            break;
        }
    }
    if (!selected) {
        selected = &*std::min_element(candidates.begin(), candidates.end(),
            [](const AimCandidate& lhs, const AimCandidate& rhs) {
                return lhs.coarseDistance < rhs.coarseDistance;
            });
    }

    Vector3 alternateLeg{};
    bool hasAlternateLeg = false;
    const std::array<Vector3, 3> targetPoints = GetBoneAimPoints(
        selected->snapshot->pawn, selected->origin, alternateLeg, hasAlternateLeg);

    // Sample the camera after all entity and bone reads, immediately before
    // projection and input, matching the late-matrix ESP strategy.
    Matrix4x4 lateMatrix{};
    if (!ReadViewMatrix(lateMatrix)) return;
    const float lateFovRadius = CalculateAimFovRadiusPixels(
        lateMatrix, screenWidth, screenHeight, configuredFov);
    if (lateFovRadius <= 0.0f) return;

    Vector3 targetScreen{};
    float targetDistance = 0.0f;
    bool targetValid = SelectProjectedPoint(targetPoints, lateMatrix, screenWidth, screenHeight,
        centerX, centerY, targetScreen, targetDistance);

    // Legs are separate hitboxes. Choose the knee nearest to the crosshair
    // instead of aiming at the empty midpoint between them.
    if (hasAlternateLeg && (g_Aim.targetPart == 2 || g_Aim.targetPart > 2)) {
        Vector3 alternateScreen{};
// Converts a 3D world position to 2D screen coordinates
        if (WorldToScreen(alternateLeg, alternateScreen, lateMatrix, screenWidth, screenHeight)) {
            const float alternateDistance = ScreenDistance(alternateScreen, centerX, centerY);
            if (!targetValid || alternateDistance < targetDistance) {
                targetScreen = alternateScreen;
                targetDistance = alternateDistance;
                targetValid = true;
            }
        }
    }

    if (!targetValid || targetDistance > lateFovRadius) {
        ResetAimTarget();
        return;
    }

    g_lockedAimPawn = selected->snapshot->pawn;

    if (IsAntiSmokeActive() && !g_Aim.allowWhenInSmoke &&
        IsLocalInSmoke(g_Esp.smokeThreshold)) return;

    // Phase 1 preserves the existing aggressiveness calculation. It will be
    // evaluated independently during the Smoothing phase.
    const float moveX = (targetScreen.x - centerX) / g_Aim.smoothing;
    const float moveY = (targetScreen.y - centerY) / g_Aim.smoothing;
    SendMouseMove(static_cast<long>(moveX), static_cast<long>(moveY));
}
