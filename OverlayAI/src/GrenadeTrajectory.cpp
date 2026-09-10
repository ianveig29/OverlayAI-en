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

    // ========== 1. THROW STRENGTH (Click Left vs Right vs Both) ==========
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

    // ========== 2. EYE POSITION ==========
    const uintptr_t sceneNode = mem.Read<uintptr_t>(frame.localPawn + Offsets::m_pGameSceneNode);
    if (!IsValidPtr(sceneNode)) return;
    const Vector3 origin = mem.Read<Vector3>(sceneNode + Offsets::m_vecAbsOrigin);
    Vector3 viewOffset = mem.Read<Vector3>(frame.localPawn + Offsets::m_vecViewOffset);
    if (!IsFiniteVector(origin)) return;
    if (!IsFiniteVector(viewOffset) || viewOffset.z < 20.0f || viewOffset.z > 100.0f)
        viewOffset = { 0.0f, 0.0f, 64.0f };

    const Vector3 eyePos = origin + viewOffset;

    // ========== 3. CAMERA ANGLES & FORWARD VECTOR ==========
    Vector3 angles = mem.Read<Vector3>(mem.clientModule + Offsets::dwViewAngles);
    if (!IsFiniteVector(angles)) return;
    
    // Normalize pitch to [-180, 180] range
    while (angles.x > 180.0f) angles.x -= 360.0f;
    while (angles.x < -180.0f) angles.x += 360.0f;
    
    // Convert angles to radians
    const float pitch = angles.x * kPi / 180.0f;
    const float yaw = angles.y * kPi / 180.0f;
    
    // Calculate forward vector from pitch and yaw
    const float horizontal = std::cos(pitch);
    const Vector3 forward{
        horizontal * std::cos(yaw),
        horizontal * std::sin(yaw),
        -std::sin(pitch)
    };

    // ========== 4. INITIAL POSITION & VELOCITY ==========
    // Position offset slightly in front of player eyes
    Vector3 position = eyePos + forward * 16.0f;
    
    // Base throw speed: 677.5 units/sec at full strength, scaled by throw power
    const float throwSpeed = 750.0f * 0.9f * (0.7f + 0.3f * strength);
    Vector3 velocity = forward * throwSpeed;

    // Apply player inertia (jumpthrow, running throw, etc.)
    const Vector3 playerVelocity = mem.Read<Vector3>(frame.localPawn + Offsets::m_vecAbsVelocity);
    if (IsFiniteVector(playerVelocity)) {
        velocity.x += playerVelocity.x * 1.25f;
        velocity.y += playerVelocity.y * 1.25f;
        velocity.z += playerVelocity.z * 1.25f;
    }

    // ========== 5. PHYSICS SIMULATION ==========
    constexpr float timeStep = 1.0f / 64.0f;        // 64 ticks per second
    constexpr float gravity = 320.0f;               // CS2 gravity constant
    constexpr float airResistance = 0.002f;         // Drag coefficient for air resistance
    const float groundHeight = origin.z + 2.0f;     // Ground level with small offset
    const int maxSteps = static_cast<int>(GetFlightTime(weaponInfo.definitionIndex) / timeStep);
    
    std::vector<Vector3> points;
    points.reserve(maxSteps / 2 + 2);
    points.push_back(position);

    for (int step = 0; step < maxSteps; ++step) {
        // Apply gravity to vertical velocity
        velocity.z -= gravity * timeStep;
        
        // Apply air resistance (drag) to all velocity components
        velocity.x *= (1.0f - airResistance * timeStep);
        velocity.y *= (1.0f - airResistance * timeStep);
        velocity.z *= (1.0f - airResistance * timeStep);

        // Update position based on current velocity
        Vector3 next{
            position.x + velocity.x * timeStep,
            position.y + velocity.y * timeStep,
            position.z + velocity.z * timeStep
        };

        // Ground collision detection and bounce
        if (next.z < groundHeight && velocity.z < 0.0f) {
            next.z = groundHeight;
            // Apply energy loss on bounce
            velocity.x *= 0.62f;  // Horizontal bounce coefficient
            velocity.y *= 0.62f;
            velocity.z *= -0.45f; // Vertical bounce coefficient (inverted)
            
            // Stop simulation if grenade is essentially stationary
            if (std::hypot(velocity.x, velocity.y) < 18.0f && std::fabs(velocity.z) < 18.0f) {
                position = next;
                points.push_back(position);
                break;
            }
        }

        position = next;
        // Store every other point to reduce point density
        if ((step & 1) != 0) {
            points.push_back(position);
        }
    }

    if (points.size() < 2) return;

    // ========== 6. SCREEN RENDERING ==========
    Matrix4x4 viewMatrix{};
    if (!ReadViewMatrix(viewMatrix)) viewMatrix = frame.viewMatrix;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImU32 color = GetTrajectoryColor(weaponInfo.definitionIndex);
    const ImU32 outline = IM_COL32(0, 0, 0, 190);

    Vector3 previousScreen{};
    bool previousProjected = WorldToScreen(points.front(), previousScreen,
        viewMatrix, screenWidth, screenHeight);

    // Draw trajectory line segments
    for (size_t index = 1; index < points.size(); ++index) {
        Vector3 currentScreen{};
        const bool currentProjected = WorldToScreen(points[index], currentScreen,
            viewMatrix, screenWidth, screenHeight);

        if (previousProjected && currentProjected) {
            const ImVec2 a(previousScreen.x, previousScreen.y);
            const ImVec2 b(currentScreen.x, currentScreen.y);
            drawList->AddLine(a, b, outline, 4.0f); // Outline
            drawList->AddLine(a, b, color, 2.0f);   // Main line
        }

        previousScreen = currentScreen;
        previousProjected = currentProjected;
    }

    // Draw endpoint indicator
    Vector3 endScreen{};
    if (WorldToScreen(points.back(), endScreen, viewMatrix, screenWidth, screenHeight)) {
        const ImVec2 end(endScreen.x, endScreen.y);
        drawList->AddCircleFilled(end, 6.0f, outline, 20); // Outline circle
        drawList->AddCircleFilled(end, 3.5f, color, 20);   // Center circle
    }
}
