#pragma once

// ============================================================
// Skeleton.h
// Skeleton function declarations.
// ============================================================

#include "Types.h"
#include "imgui.h"
#include <array>
#include <cstdint>

constexpr size_t kSkeletonBoneCount = 23;

struct SkeletonPose {
    std::array<Vector3, kSkeletonBoneCount> bones{};
    std::array<bool, kSkeletonBoneCount> valid{};
};

bool ReadSkeletonPose(uintptr_t pawn, const Vector3& origin, SkeletonPose& out);
void DrawSkeletonPose(ImDrawList* drawList, const SkeletonPose& pose,
    const Matrix4x4& viewMatrix, int screenWidth, int screenHeight,
    ImU32 color, float thickness = 2.0f, bool showJoints = true,
    float jointScale = 1.0f);
void DrawCustomGlowPose(ImDrawList* drawList, const SkeletonPose& pose,
    const Matrix4x4& viewMatrix, int screenWidth, int screenHeight,
    ImU32 color, float bodyHeight, float bodyScale, float softness, int layers);
