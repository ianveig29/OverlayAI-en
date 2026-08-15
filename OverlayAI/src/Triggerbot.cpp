#include "Triggerbot.h"
#include "Config.h"
#include "Entity.h"
#include "AntiFlash.h"
#include "AntiSmoke.h"
#include "Memory.h"
#include "Offsets.h"
#include "Skeleton.h"
#include "WorldTransform.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>


static constexpr uint16_t WEAPON_R8_REVOLVER = 64;
static constexpr ULONGLONG REV_COCK_MS = 220;
static constexpr ULONGLONG REV_CYCLE_MS = 850;

enum class RevolverFireState { Idle, Cocking, Cooldown };

static RevolverFireState g_revState = RevolverFireState::Idle;
static ULONGLONG g_revStateStart = 0;
static bool g_mouseHeldByTrigger = false;

namespace {
    struct BoneConnection { int from; int to; };

    constexpr BoneConnection kTriggerConnections[] = {
        { 7, 6 }, { 6, 5 }, { 5, 4 }, { 4, 3 }, { 3, 2 }, { 2, 1 },
        { 5, 8 }, { 8, 9 }, { 9, 10 }, { 10, 11 },
        { 5, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 },
        { 1, 17 }, { 17, 18 }, { 18, 19 },
        { 1, 20 }, { 20, 21 }, { 21, 22 }
    };

    Vector3 GetSnapshotOrigin(const EntitySnapshot& snapshot) {
        if (IsValidPtr(snapshot.sceneNode)) {
            const Vector3 origin = mem.Read<Vector3>(snapshot.sceneNode + Offsets::m_vecAbsOrigin);
            if (std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z))
                return origin;
        }
        return snapshot.worldPos;
    }

    float DistanceToScreenSegment(float px, float py, const Vector3& a, const Vector3& b) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float lengthSq = dx * dx + dy * dy;
        if (lengthSq <= 0.001f) return std::hypot(px - a.x, py - a.y);

        const float projection = ((px - a.x) * dx + (py - a.y) * dy) / lengthSq;
        const float t = (std::clamp)(projection, 0.0f, 1.0f);
        return std::hypot(px - (a.x + dx * t), py - (a.y + dy * t));
    }

    bool TryGetSmokeSnapshotTarget(uintptr_t localPawn, int localTeam, uintptr_t& outTargetPawn) {
        outTargetPawn = 0;
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        if (screenWidth <= 0 || screenHeight <= 0) return false;

        Matrix4x4 viewMatrix{};
        if (!ReadViewMatrix(viewMatrix)) return false;

        const float centerX = screenWidth * 0.5f;
        const float centerY = screenHeight * 0.5f;
        float bestDistance = 1e9f;
        const FrameSnapshot& frame = GetCurrentFrameSnapshot();

        for (const EntitySnapshot& snapshot : frame.entities) {
            if (!IsValidPtr(snapshot.pawn) || snapshot.pawn == localPawn) continue;
            if (snapshot.lifeState != 0 || snapshot.health <= 0 || snapshot.health > 100) continue;
            if (!g_Triggerbot.shootTeammates && localTeam != 0 && snapshot.team == localTeam) continue;

            const Vector3 origin = GetSnapshotOrigin(snapshot);
            Vector3 feetScreen{};
            Vector3 headScreen{};
            const Vector3 fallbackHead{ origin.x, origin.y, origin.z + 72.0f };
            if (!WorldToScreen(origin, feetScreen, viewMatrix, screenWidth, screenHeight) ||
                !WorldToScreen(fallbackHead, headScreen, viewMatrix, screenWidth, screenHeight)) continue;

            const float projectedHeight = std::fabs(feetScreen.y - headScreen.y);
            if (projectedHeight < 8.0f || projectedHeight > screenHeight * 1.5f) continue;
            const float coarseHalfWidth = projectedHeight * 0.30f;
            if (centerX < headScreen.x - coarseHalfWidth || centerX > headScreen.x + coarseHalfWidth ||
                centerY < headScreen.y - projectedHeight * 0.08f || centerY > feetScreen.y + projectedHeight * 0.05f)
                continue;

            SkeletonPose pose{};
            if (!ReadSkeletonPose(snapshot.pawn, origin, pose)) continue;

            std::array<Vector3, kSkeletonBoneCount> projectedBones{};
            std::array<bool, kSkeletonBoneCount> projected{};
            for (size_t i = 0; i < kSkeletonBoneCount; ++i) {
                if (pose.valid[i])
                    projected[i] = WorldToScreen(pose.bones[i], projectedBones[i],
                        viewMatrix, screenWidth, screenHeight);
            }

            float entityDistance = 1e9f;
            for (const BoneConnection& connection : kTriggerConnections) {
                if (!projected[connection.from] || !projected[connection.to]) continue;
                entityDistance = (std::min)(entityDistance, DistanceToScreenSegment(centerX, centerY,
                    projectedBones[connection.from], projectedBones[connection.to]));
            }

            // Scale the capsule with distance while keeping it narrow enough to
            // avoid firing through the empty space around limbs.
            const float hitRadius = (std::clamp)(projectedHeight * 0.055f, 3.0f, 18.0f);
            if (entityDistance <= hitRadius && entityDistance < bestDistance) {
                bestDistance = entityDistance;
                outTargetPawn = snapshot.pawn;
            }
        }
        return IsValidPtr(outTargetPawn);
    }
}

static bool IsValidPlayerPawn(uintptr_t pawn, int localTeam, int localPlayerIndex,
    bool bypassVisibilityForFlash, bool bypassVisibilityForSmoke) {
    if (!IsValidPtr(pawn)) return false;
    if (!IsPawnAlive(pawn)) return false;

    int health = mem.Read<int>(pawn + Offsets::m_iHealth);
    if (health <= 0 || health > 100) return false;

    int team = mem.Read<uint8_t>(pawn + Offsets::m_iTeamNum);
    if (team != 2 && team != 3) return false;

    if (!g_Triggerbot.shootTeammates && localTeam != 0 && team == localTeam)
        return false;

    if (g_Triggerbot.requireVisible && !bypassVisibilityForFlash && !bypassVisibilityForSmoke &&
        !IsPawnVisibleToLocal(pawn, localPlayerIndex))
        return false;

    return true;
}

static void SendMouseDown() {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(INPUT));
}

static void SendMouseUp() {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(INPUT));
}

static void SimulateLeftClick() {
    SendMouseDown();
    SendMouseUp();
}

static void CancelRevolverHold() {
    if (g_mouseHeldByTrigger) {
        SendMouseUp();
        g_mouseHeldByTrigger = false;
    }
    g_revState = RevolverFireState::Idle;
}

static bool TryGetCrosshairTarget(uintptr_t entityList, uintptr_t localPawn, int localTeam, int localPlayerIndex,
    bool bypassVisibilityForFlash, bool bypassVisibilityForSmoke, uintptr_t& outTargetPawn)
{
    outTargetPawn = 0;
    int crosshairIndex = mem.Read<int>(localPawn + Offsets::m_iIDEntIndex);
    if (crosshairIndex <= 0 || crosshairIndex >= 0x8000) return false;

    uintptr_t targetPawn = GetEntityByIndexAuto(entityList, crosshairIndex);
    if (!IsValidPlayerPawn(targetPawn, localTeam, localPlayerIndex,
        bypassVisibilityForFlash, bypassVisibilityForSmoke)) return false;

    outTargetPawn = targetPawn;
    return true;
}

static void RunRevolverTriggerbot(ULONGLONG now, ULONGLONG& lastShotMs) {
    switch (g_revState) {
    case RevolverFireState::Idle:
        if (g_Triggerbot.delayMs > 0 && now - lastShotMs < static_cast<ULONGLONG>(g_Triggerbot.delayMs))
            return;
        SendMouseDown();
        g_mouseHeldByTrigger = true;
        g_revState = RevolverFireState::Cocking;
        g_revStateStart = now;
        break;
    case RevolverFireState::Cocking:
        if (now - g_revStateStart >= REV_COCK_MS) {
            SendMouseUp();
            g_mouseHeldByTrigger = false;
            lastShotMs = now;
            g_revState = RevolverFireState::Cooldown;
            g_revStateStart = now;
        }
        break;
    case RevolverFireState::Cooldown:
        if (now - g_revStateStart >= REV_CYCLE_MS)
            g_revState = RevolverFireState::Idle;
        break;
    }
}

void PollTriggerbotKeyBind() {
    if (!g_Triggerbot.waitingForHoldKey) return;
    for (int vk = 1; vk < 256; ++vk) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
        if (GetAsyncKeyState(vk) & 0x8000) {
            g_Triggerbot.holdKeyVk = vk;
            g_Triggerbot.waitingForHoldKey = false;
            break;
        }
    }
}

void RunTriggerbot() {
    if (!g_Triggerbot.enabled || !mem.clientModule || g_MenuOpen) {
        CancelRevolverHold();
        return;
    }

    if (g_Triggerbot.requireHoldKey) {
        if ((GetAsyncKeyState(g_Triggerbot.holdKeyVk) & 0x8000) == 0) {
            CancelRevolverHold();
            return;
        }
    }

    uintptr_t entityList = GetEntityListBase();
    if (!IsValidPtr(entityList)) {
        CancelRevolverHold();
        return;
    }

    uintptr_t localController = 0;
    uintptr_t localPawn = 0;
    int localTeam = 0;
    int localPlayerIndex = -1;
    ResolveActiveLocal(entityList, g_entityStride, localController, localPawn, localTeam, localPlayerIndex);
    if (!IsValidPtr(localPawn)) {
        CancelRevolverHold();
        return;
    }

    const bool localFlashed = IsLocalFlashed(g_Esp.flashThreshold);
    if (localFlashed && !g_Triggerbot.allowWhenFlashed) {
        CancelRevolverHold();
        return;
    }
    const bool bypassVisibilityForFlash = localFlashed && g_Triggerbot.allowWhenFlashed;
    // A target inside smoke can lose spotted state while the local smoke alpha
    // remains zero, so this user option controls the visibility bypass directly.
    const bool bypassVisibilityForSmoke = g_Triggerbot.allowWhenInSmoke;

    uintptr_t targetPawn = 0;
    if (!TryGetCrosshairTarget(entityList, localPawn, localTeam, localPlayerIndex,
        bypassVisibilityForFlash, bypassVisibilityForSmoke, targetPawn)) {
        if (!g_Triggerbot.allowWhenInSmoke ||
            !TryGetSmokeSnapshotTarget(localPawn, localTeam, targetPawn)) {
            CancelRevolverHold();
            return;
        }
    }

    (void)targetPawn;

    if (IsAntiSmokeActive()) {
        if (!g_Triggerbot.allowWhenInSmoke && IsLocalInSmoke(g_Esp.smokeThreshold)) {
            CancelRevolverHold();
            return;
        }
    }

    static ULONGLONG lastShotMs = 0;
    ULONGLONG now = GetTickCount64();

    int weaponId = GetActiveWeaponDefIndex(localPawn, entityList);
    if (weaponId == WEAPON_R8_REVOLVER) {
        RunRevolverTriggerbot(now, lastShotMs);
        return;
    }

    if (g_revState != RevolverFireState::Idle)
        CancelRevolverHold();

    if (g_Triggerbot.delayMs > 0 && now - lastShotMs < static_cast<ULONGLONG>(g_Triggerbot.delayMs))
        return;

    SimulateLeftClick();
    lastShotMs = now;
}
