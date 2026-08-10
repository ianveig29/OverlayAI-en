#include "Skeleton.h"
#include "Memory.h"
#include "Offsets.h"
#include "Stats.h"
#include "WorldTransform.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace {
    struct BoneConnection { int from; int to; };

    struct CachedSkeleton {
        uintptr_t sceneNode = 0;
        uintptr_t boneArray = 0;
        ULONGLONG sourceRefreshMs = 0;
        ULONGLONG poseRefreshMs = 0;
        Vector3 poseOrigin{};
        SkeletonPose pose{};
        bool hasPose = false;
    };

    std::unordered_map<uintptr_t, CachedSkeleton> g_skeletonCache;

    constexpr BoneConnection kConnections[] = {
        { 7, 6 }, { 6, 5 }, { 5, 4 }, { 4, 3 }, { 3, 2 }, { 2, 1 },
        { 5, 8 }, { 8, 9 }, { 9, 10 }, { 10, 11 },
        { 5, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 },
        { 1, 17 }, { 17, 18 }, { 18, 19 },
        { 1, 20 }, { 20, 21 }, { 21, 22 }
    };

    bool IsFiniteBone(const Vector3& bone, const Vector3& origin) {
        if (!std::isfinite(bone.x) || !std::isfinite(bone.y) || !std::isfinite(bone.z)) return false;
        const float dx = bone.x - origin.x;
        const float dy = bone.y - origin.y;
        const float dz = bone.z - origin.z;
        return dx * dx + dy * dy + dz * dz < 160.0f * 160.0f;
    }
}

bool ReadSkeletonPose(uintptr_t pawn, const Vector3& origin, SkeletonPose& out) {
    out = {};
    if (!IsValidPtr(pawn) || Offsets::m_boneStride < sizeof(Vector3) || Offsets::m_boneStride > 0x100)
        return false;

    if (g_skeletonCache.size() > 256) g_skeletonCache.clear();
    CachedSkeleton& cached = g_skeletonCache[pawn];
    const ULONGLONG nowMs = GetTickCount64();

    // Translation is corrected from the current origin between reads, so a
    // moderate pose cadence saves RPM calls without adding positional lag.
    if (cached.poseRefreshMs != 0 && nowMs - cached.poseRefreshMs < 12) {
        if (!cached.hasPose) return false;

        const Vector3 delta = {
            origin.x - cached.poseOrigin.x,
            origin.y - cached.poseOrigin.y,
            origin.z - cached.poseOrigin.z
        };
        const float deltaLengthSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (std::isfinite(deltaLengthSq) && deltaLengthSq < 64.0f * 64.0f) {
            out = cached.pose;
            for (size_t i = 0; i < kSkeletonBoneCount; ++i) {
                if (!out.valid[i]) continue;
                out.bones[i].x += delta.x;
                out.bones[i].y += delta.y;
                out.bones[i].z += delta.z;
            }
            return true;
        }
    }

    if (!IsValidPtr(cached.boneArray) || nowMs - cached.sourceRefreshMs >= 2000) {
        cached.sceneNode = mem.Read<uintptr_t>(pawn + Offsets::m_pGameSceneNode);
        cached.boneArray = IsValidPtr(cached.sceneNode)
            ? mem.Read<uintptr_t>(cached.sceneNode + Offsets::m_pBoneArray)
            : 0;
        cached.sourceRefreshMs = nowMs;
    }
    cached.poseRefreshMs = nowMs;
    cached.hasPose = false;
    if (!IsValidPtr(cached.boneArray)) return false;

    const size_t byteCount = kSkeletonBoneCount * static_cast<size_t>(Offsets::m_boneStride);
    std::array<unsigned char, kSkeletonBoneCount * 0x100> buffer{};
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(cached.boneArray),
        buffer.data(), byteCount, &bytesRead) || bytesRead < byteCount) {
        cached.boneArray = 0;
        return false;
    }
    Stats::rpmReadCount.fetch_add(1);

    size_t validCount = 0;
    for (size_t i = 0; i < kSkeletonBoneCount; ++i) {
        Vector3 bone{};
        memcpy(&bone, buffer.data() + i * Offsets::m_boneStride, sizeof(bone));
        if (!IsFiniteBone(bone, origin)) continue;
        out.bones[i] = bone;
        out.valid[i] = true;
        ++validCount;
    }

    if (validCount < 16 || !out.valid[1] || !out.valid[6] || !out.valid[7]) return false;
    const float bodyHeight = out.bones[7].z - out.bones[1].z;
    if (bodyHeight <= 15.0f || bodyHeight >= 90.0f) return false;

    cached.pose = out;
    cached.poseOrigin = origin;
    cached.hasPose = true;
    return true;
}

void DrawSkeletonPose(ImDrawList* drawList, const SkeletonPose& pose,
    const Matrix4x4& viewMatrix, int screenWidth, int screenHeight,
    ImU32 color, float thickness, bool showJoints, float jointScale) {
    if (!drawList || screenWidth <= 0 || screenHeight <= 0) return;

    std::array<ImVec2, kSkeletonBoneCount> screenBones{};
    std::array<bool, kSkeletonBoneCount> projected{};
    for (size_t i = 0; i < kSkeletonBoneCount; ++i) {
        if (!pose.valid[i]) continue;
        Vector3 screen{};
        if (!WorldToScreen(pose.bones[i], screen, viewMatrix, screenWidth, screenHeight)) continue;
        if (screen.x < -screenWidth || screen.x > screenWidth * 2.0f ||
            screen.y < -screenHeight || screen.y > screenHeight * 2.0f) continue;
        screenBones[i] = ImVec2(screen.x, screen.y);
        projected[i] = true;
    }

    const float maxLineLengthSq = static_cast<float>(screenWidth * screenWidth + screenHeight * screenHeight) * 0.20f;
    const float safeThickness = (std::max)(0.5f, thickness);
    const float jointRadius = (std::max)(1.0f, 2.0f * jointScale);
    for (const BoneConnection& connection : kConnections) {
        if (!projected[connection.from] || !projected[connection.to]) continue;
        const ImVec2& a = screenBones[connection.from];
        const ImVec2& b = screenBones[connection.to];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        if (dx * dx + dy * dy > maxLineLengthSq) continue;
        drawList->AddLine(a, b, color, safeThickness);
    }

    if (showJoints) {
        constexpr int kMajorJoints[] = { 1, 5, 6, 7, 8, 9, 12, 13, 17, 18, 20, 21 };
        for (int index : kMajorJoints) {
            if (projected[index]) drawList->AddCircleFilled(screenBones[index], jointRadius, color);
        }
    }
}

void DrawCustomGlowPose(ImDrawList* drawList, const SkeletonPose& pose,
    const Matrix4x4& viewMatrix, int screenWidth, int screenHeight,
    ImU32 color, float bodyHeight, float bodyScale, float softness, int layers) {
    if (!drawList || bodyHeight < 8.0f || screenWidth <= 0 || screenHeight <= 0) return;

    std::array<ImVec2, kSkeletonBoneCount> screenBones{};
    std::array<bool, kSkeletonBoneCount> projected{};
    for (size_t index = 0; index < kSkeletonBoneCount; ++index) {
        if (!pose.valid[index]) continue;
        Vector3 screen{};
        if (!WorldToScreen(pose.bones[index], screen, viewMatrix, screenWidth, screenHeight)) continue;
        if (screen.x < -screenWidth || screen.x > screenWidth * 2.0f ||
            screen.y < -screenHeight || screen.y > screenHeight * 2.0f) continue;
        screenBones[index] = ImVec2(screen.x, screen.y);
        projected[index] = true;
    }

    const float safeScale = std::clamp(bodyScale, 0.55f, 2.0f);
    const float safeSoftness = std::clamp(softness, 0.0f, 18.0f);
    const int safeLayers = std::clamp(layers, 1, 8);
    const ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    const float sourceAlpha = std::clamp(rgba.w, 0.0f, 1.0f);
    const float maxLineLengthSq = static_cast<float>(screenWidth * screenWidth +
        screenHeight * screenHeight) * 0.20f;

    struct BodySegment {
        int from;
        int to;
        float widthFactor;
    };
    constexpr BodySegment kBodySegments[] = {
        { 6, 7, 0.060f },
        { 5, 8, 0.075f }, { 8, 9, 0.068f }, { 9, 10, 0.058f }, { 10, 11, 0.046f },
        { 5, 12, 0.075f }, { 12, 13, 0.068f }, { 13, 14, 0.058f }, { 14, 15, 0.046f },
        { 1, 17, 0.105f }, { 17, 18, 0.105f }, { 18, 19, 0.080f },
        { 1, 20, 0.105f }, { 20, 21, 0.105f }, { 21, 22, 0.080f }
    };

    auto drawCapsule = [&](const ImVec2& a, const ImVec2& b, float width, ImU32 passColor) {
        drawList->AddLine(a, b, passColor, width);
        const float radius = width * 0.5f;
        drawList->AddCircleFilled(a, radius, passColor, 20);
        drawList->AddCircleFilled(b, radius, passColor, 20);
    };

    auto drawTorso = [&](float expansion, ImU32 passColor) {
        if (!projected[8] || !projected[12] || !projected[17] || !projected[20]) return;

        std::array<ImVec2, 4> torso = {
            screenBones[8], screenBones[12], screenBones[20], screenBones[17]
        };
        ImVec2 center{};
        for (const ImVec2& point : torso) {
            center.x += point.x;
            center.y += point.y;
        }
        center.x *= 0.25f;
        center.y *= 0.25f;

        // Expand the quadrilateral radially so every halo pass remains a
        // continuous silhouette instead of exposing the underlying skeleton.
        for (ImVec2& point : torso) {
            const float dx = point.x - center.x;
            const float dy = point.y - center.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.001f) {
                point.x += dx / length * expansion;
                point.y += dy / length * expansion;
            }
        }
        drawList->AddConvexPolyFilled(torso.data(), static_cast<int>(torso.size()), passColor);
    };

    // Draw outside-in. Every pass is a filled body mask; the translucent outer
    // masks create the halo while the final pass provides a coherent interior.
    for (int pass = safeLayers; pass >= 0; --pass) {
        const float t = static_cast<float>(pass) / static_cast<float>(safeLayers);
        const float expansion = safeSoftness * t;
        const float passAlpha = sourceAlpha * (pass == 0
            ? 0.30f
            : 0.025f + 0.060f * (1.0f - t));
        const ImU32 passColor = ImGui::ColorConvertFloat4ToU32(
            ImVec4(rgba.x, rgba.y, rgba.z, std::clamp(passAlpha, 0.0f, 1.0f)));

        drawTorso(expansion, passColor);

        for (const BodySegment& segment : kBodySegments) {
            if (!projected[segment.from] || !projected[segment.to]) continue;
            const ImVec2& a = screenBones[segment.from];
            const ImVec2& b = screenBones[segment.to];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            if (dx * dx + dy * dy > maxLineLengthSq) continue;
            const float width = (std::max)(2.0f,
                bodyHeight * segment.widthFactor * safeScale + expansion * 2.0f);
            drawCapsule(a, b, width, passColor);
        }

        if (projected[7]) {
            const float headWidth = bodyHeight * 0.055f * safeScale + expansion;
            const float headHeight = bodyHeight * 0.072f * safeScale + expansion;
            float rotation = 0.0f;
            if (projected[6]) {
                const ImVec2 delta = {
                    screenBones[7].x - screenBones[6].x,
                    screenBones[7].y - screenBones[6].y
                };
                rotation = std::atan2(delta.y, delta.x) - 1.57079632679f;
            }
            drawList->AddEllipseFilled(screenBones[7],
                ImVec2((std::max)(2.0f, headWidth), (std::max)(2.0f, headHeight)),
                passColor, rotation, 28);
        }
    }
}
