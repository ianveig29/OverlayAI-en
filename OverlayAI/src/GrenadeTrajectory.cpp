// ============================================================
// GrenadeTrajectory.cpp
// Draws the trajectory of grenades in the air.
// ============================================================

#include "GrenadeTrajectory.h"

#include "Config.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include "WeaponInfo.h"
#include "WorldTransform.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <windows.h>

namespace {
    constexpr float kPi = 3.14159265359f;

    bool IsGrenade(int definitionIndex) {
        return definitionIndex >= 43 && definitionIndex <= 48;
    }

    ImU32 GetTrajectoryColor(int definitionIndex) {
        switch (definitionIndex) {
        case 43: return IM_COL32(150, 220, 255, 245); // Flashbang
        case 44: return IM_COL32(255, 90, 70, 245);   // HE
        case 45: return IM_COL32(150, 210, 165, 245); // Smoke
        case 46:
        case 48: return IM_COL32(255, 145, 45, 245);  // Fire
        case 47: return IM_COL32(100, 170, 255, 245); // Decoy
        default: return IM_COL32(235, 235, 235, 245);
        }
    }

    float GetFlightTime(int definitionIndex) {
        if (definitionIndex == 43 || definitionIndex == 44) return 1.55f;
        if (definitionIndex == 46 || definitionIndex == 48) return 2.0f;
        return 3.0f;
    }

    bool IsFiniteVector(const Vector3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
}

void RenderGrenadeTrajectory(int screenWidth, int screenHeight) {
    if (!g_Esp.showGrenadeTrajectory || screenWidth <= 0 || screenHeight <= 0) return;

    const bool leftHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool rightHeld = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool holdingThrow = leftHeld || rightHeld;
    if (g_Esp.grenadeTrajectoryMode == 1 && !holdingThrow) return;

// Gets the most recent player snapshot
    const FrameSnapshot& frame = GetCurrentFrameSnapshot();
    if (!IsValidPtr(frame.localPawn)) return;
    const uintptr_t entityList = GetEntityListBase();
    const uintptr_t weapon = GetActiveWeaponEntity(frame.localPawn, entityList);
    if (!IsValidPtr(weapon)) return;

    ActiveWeaponInfo weaponInfo{};
    if (!ReadActiveWeaponInfo(frame.localPawn, entityList, false, weaponInfo) ||
        !IsGrenade(weaponInfo.definitionIndex))
        return;

    float strength = 1.0f;
    if (leftHeld && rightHeld) {
        strength = 0.5f;
    } else if (rightHeld) {
        strength = 0.0f;
    } else if (leftHeld) {
        strength = 1.0f;
    }
    const bool throwAnimating = mem.Read<bool>(weapon + Offsets::m_bThrowAnimating);
    const float storedStrength = mem.Read<float>(weapon + Offsets::m_flThrowStrength);
    if ((holdingThrow || throwAnimating) && std::isfinite(storedStrength) &&
        storedStrength >= 0.0f && storedStrength <= 1.0f)
        strength = storedStrength;

    const uintptr_t sceneNode = mem.Read<uintptr_t>(frame.localPawn + Offsets::m_pGameSceneNode);
    if (!IsValidPtr(sceneNode)) return;
    const Vector3 origin = mem.Read<Vector3>(sceneNode + Offsets::m_vecAbsOrigin);
    Vector3 viewOffset = mem.Read<Vector3>(frame.localPawn + Offsets::m_vecViewOffset);
    if (!IsFiniteVector(origin)) return;
    if (!IsFiniteVector(viewOffset) || viewOffset.z < 20.0f || viewOffset.z > 100.0f)
        viewOffset = { 0.0f, 0.0f, 64.0f };

    Vector3 angles = mem.Read<Vector3>(mem.clientModule + Offsets::dwViewAngles);
    if (!IsFiniteVector(angles)) return;
    while (angles.x > 180.0f) angles.x -= 360.0f;
    while (angles.x < -180.0f) angles.x += 360.0f;
    const float adjustedPitch = angles.x - (90.0f - std::fabs(angles.x)) * (10.0f / 90.0f);
    const float pitch = adjustedPitch * kPi / 180.0f;
    const float yaw = angles.y * kPi / 180.0f;
    const float horizontal = std::cos(pitch);
    const Vector3 forward{
        horizontal * std::cos(yaw),
        horizontal * std::sin(yaw),
        -std::sin(pitch)
    };

    Vector3 position{
        origin.x + viewOffset.x + forward.x * 16.0f,
        origin.y + viewOffset.y + forward.y * 16.0f,
        origin.z + viewOffset.z + strength * 12.0f - 12.0f + forward.z * 16.0f
    };
    const float throwSpeed = 750.0f * 0.9f * (0.7f + 0.3f * strength);
    Vector3 velocity{
        forward.x * throwSpeed,
        forward.y * throwSpeed,
        forward.z * throwSpeed
    };
    const Vector3 playerVelocity = mem.Read<Vector3>(frame.localPawn + Offsets::m_vecAbsVelocity);
    if (IsFiniteVector(playerVelocity)) {
        velocity.x += playerVelocity.x * 1.25f;
        velocity.y += playerVelocity.y * 1.25f;
        velocity.z += playerVelocity.z * 1.25f;
    }

    constexpr float timeStep = 1.0f / 64.0f;
    constexpr float gravity = 320.0f;
    const float groundHeight = origin.z + 2.0f;
    const int maxSteps = static_cast<int>(GetFlightTime(weaponInfo.definitionIndex) / timeStep);
    std::vector<Vector3> points;
    points.reserve(maxSteps / 2 + 2);
    points.push_back(position);
    for (int step = 0; step < maxSteps; ++step) {
        Vector3 next{
            position.x + velocity.x * timeStep,
            position.y + velocity.y * timeStep,
            position.z + velocity.z * timeStep - 0.5f * gravity * timeStep * timeStep
        };
        velocity.z -= gravity * timeStep;

        if (next.z < groundHeight && velocity.z < 0.0f) {
            next.z = groundHeight;
            velocity.x *= 0.62f;
            velocity.y *= 0.62f;
            velocity.z *= -0.45f;
            if (std::hypot(velocity.x, velocity.y) < 18.0f && std::fabs(velocity.z) < 18.0f) {
                position = next;
                points.push_back(position);
                break;
            }
        }
        position = next;
        if ((step & 1) != 0) points.push_back(position);
    }
    if (points.size() < 2) return;

    Matrix4x4 viewMatrix{};
    if (!ReadViewMatrix(viewMatrix)) viewMatrix = frame.viewMatrix;
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImU32 color = GetTrajectoryColor(weaponInfo.definitionIndex);
    const ImU32 outline = IM_COL32(0, 0, 0, 190);
    Vector3 previousScreen{};
// Converts a 3D world position to 2D screen coordinates
    bool previousProjected = WorldToScreen(points.front(), previousScreen,
        viewMatrix, screenWidth, screenHeight);
    for (size_t index = 1; index < points.size(); ++index) {
        Vector3 currentScreen{};
// Converts a 3D world position to 2D screen coordinates
        const bool currentProjected = WorldToScreen(points[index], currentScreen,
            viewMatrix, screenWidth, screenHeight);
        if (previousProjected && currentProjected) {
            const ImVec2 a(previousScreen.x, previousScreen.y);
            const ImVec2 b(currentScreen.x, currentScreen.y);
            drawList->AddLine(a, b, outline, 4.0f);
            drawList->AddLine(a, b, color, 2.0f);
        }
        previousScreen = currentScreen;
        previousProjected = currentProjected;
    }

    Vector3 endScreen{};
// Converts a 3D world position to 2D screen coordinates
    if (WorldToScreen(points.back(), endScreen, viewMatrix, screenWidth, screenHeight)) {
        const ImVec2 end(endScreen.x, endScreen.y);
        drawList->AddCircleFilled(end, 6.0f, outline, 20);
        drawList->AddCircleFilled(end, 3.5f, color, 20);
    }
}
