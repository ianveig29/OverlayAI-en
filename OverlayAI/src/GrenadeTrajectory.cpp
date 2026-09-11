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
    
    // Normalize view angles
    while (angles.x > 89.0f) angles.x -= 360.0f;
    while (angles.x < -89.0f) angles.x += 360.0f;
    while (angles.y > 180.0f) angles.y -= 360.0f;
    while (angles.y < -180.0f) angles.y += 360.0f;

    // CS2 Grenade pitch adjustment: tilted slightly upwards towards horizontal throws
    const float adjustedPitch = angles.x - (90.0f - std::fabs(angles.x)) * (10.0f / 90.0f);
    const float pitchRad = adjustedPitch * (kPi / 180.0f);
    const float yawRad = angles.y * (kPi / 180.0f);

    const float cosPitch = std::cos(pitchRad);
    const Vector3 forward{
        cosPitch * std::cos(yawRad),
        cosPitch * std::sin(yawRad),
        -std::sin(pitchRad)
    };

    const Vector3 eyePos{
        origin.x + viewOffset.x,
        origin.y + viewOffset.y,
        origin.z + viewOffset.z
    };

    // CS2 grenade spawn position: 16 units forward from eye, slight vertical offset based on throw strength
    Vector3 position{
        eyePos.x + forward.x * 16.0f,
        eyePos.y + forward.y * 16.0f,
        eyePos.z + forward.z * 16.0f + (strength * 12.0f - 12.0f)
    };

    // CS2 Grenade velocity physics:
    // Base speed ~750 u/s scaled with throw strength (0.7 + 0.3 * strength) * 0.9
    const float throwSpeed = 750.0f * (0.7f + 0.3f * strength) * 0.9f;
    Vector3 velocity{
        forward.x * throwSpeed,
        forward.y * throwSpeed,
        forward.z * throwSpeed
    };

    // Transfer 1.25x player velocity component if moving
    const Vector3 playerVelocity = mem.Read<Vector3>(frame.localPawn + Offsets::m_vecAbsVelocity);
    if (IsFiniteVector(playerVelocity)) {
        velocity.x += playerVelocity.x * 1.25f;
        velocity.y += playerVelocity.y * 1.25f;
        velocity.z += playerVelocity.z * 1.25f;
    }

    constexpr float timeStep = 1.0f / 128.0f;
    constexpr float gravity = 800.0f * 0.40f; // 320 units/s^2
    constexpr float airDrag = 0.2f;           // Air resistance
    const float flightDuration = GetFlightTime(weaponInfo.definitionIndex);
    const int maxSteps = static_cast<int>(flightDuration / timeStep);

    std::vector<Vector3> points;
    points.reserve(maxSteps / 2 + 2);
    points.push_back(position);

    for (int step = 0; step < maxSteps; ++step) {
        // Integrate drag and gravity
        velocity.x *= (1.0f - airDrag * timeStep);
        velocity.y *= (1.0f - airDrag * timeStep);
        velocity.z -= gravity * timeStep;

        position.x += velocity.x * timeStep;
        position.y += velocity.y * timeStep;
        position.z += velocity.z * timeStep;

        // Sample every 2 ticks for smooth visual rendering
        if ((step % 2) == 0) {
            points.push_back(position);
        }
    }
    if (points.empty() || points.back().x != position.x) {
        points.push_back(position);
    }

    if (points.size() < 2) return;

    Matrix4x4 viewMatrix{};
    if (!ReadViewMatrix(viewMatrix)) viewMatrix = frame.viewMatrix;
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImU32 color = GetTrajectoryColor(weaponInfo.definitionIndex);
    const ImU32 outline = IM_COL32(0, 0, 0, 220);

    Vector3 previousScreen{};
    bool previousProjected = WorldToScreen(points.front(), previousScreen,
        viewMatrix, screenWidth, screenHeight);

    for (size_t index = 1; index < points.size(); ++index) {
        Vector3 currentScreen{};
        const bool currentProjected = WorldToScreen(points[index], currentScreen,
            viewMatrix, screenWidth, screenHeight);
        if (previousProjected && currentProjected) {
            const ImVec2 a(previousScreen.x, previousScreen.y);
            const ImVec2 b(currentScreen.x, currentScreen.y);
            // Multi-pass outline for crisp visibility
            drawList->AddLine(a, b, outline, 3.5f);
            drawList->AddLine(a, b, color, 1.8f);
        }
        previousScreen = currentScreen;
        previousProjected = currentProjected;
    }

    // Impact / End-of-flight indicator
    Vector3 endScreen{};
    if (WorldToScreen(points.back(), endScreen, viewMatrix, screenWidth, screenHeight)) {
        const ImVec2 end(endScreen.x, endScreen.y);
        drawList->AddCircleFilled(end, 6.5f, outline, 24);
        drawList->AddCircleFilled(end, 4.0f, color, 24);
        drawList->AddCircle(end, 7.5f, color, 24, 1.2f);
    }
}
