#include "Esp.h"
#include "Config.h"
#include "Draw.h"
#include "Entity.h"
#include "WorldTransform.h"
#include "Memory.h"
#include "Offsets.h"
#include "imgui.h"
#include "Skeleton.h"
#include "OtherGlow.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <string>
#include <chrono>
#include <sstream>
#include <iomanip>
#include "Types.h"
#include <cstdarg>
#include <cstdint>
#include "Stats.h"
#include "Aimlock.h"
#include "WeaponIcons.h"
#include "WeaponInfo.h"
#include <array>
#include <cfloat>
#include <cstring>
#include <vector>

namespace {
    constexpr std::ptrdiff_t OFF_m_Collision = 0xD28;
    constexpr std::ptrdiff_t OFF_m_pCollision = 0x340;
    constexpr std::ptrdiff_t OFF_collision_m_vecMins = 0x40;
    constexpr std::ptrdiff_t OFF_collision_m_vecMaxs = 0x4C;

    struct CollisionBoundsBlock {
        Vector3 mins{};
        Vector3 maxs{};
    };

    struct CachedCollisionBounds {
        uintptr_t collisionPtr = 0;
        ULONGLONG sourceRefreshMs = 0;
        ULONGLONG boundsRefreshMs = 0;
        Vector3 mins{};
        Vector3 maxs{};
        bool valid = false;
    };

    std::unordered_map<uintptr_t, CachedCollisionBounds> g_collisionBoundsCache;

    struct CachedActiveWeapon {
        ActiveWeaponInfo info{};
        ULONGLONG refreshMs = 0;
        bool includesAmmo = false;
        bool valid = false;
    };

    std::unordered_map<uintptr_t, CachedActiveWeapon> g_activeWeaponCache;

    struct CachedBombCarrier {
        uintptr_t c4Entity = 0;
        uintptr_t identity = 0;
        ULONGLONG ownerRefreshMs = 0;
        ULONGLONG nextScanMs = 0;
        int scanChunk = 0;
        uint32_t ownerHandle = 0;
    };

    CachedBombCarrier g_bombCarrierCache;

    struct CachedC4Class {
        uintptr_t identity = 0;
        bool isC4 = false;
    };

    std::unordered_map<uintptr_t, CachedC4Class> g_c4ClassCache;

    static bool ReadCachedActiveWeapon(uintptr_t pawn, uintptr_t entityList,
        bool includeAmmo, ActiveWeaponInfo& out) {
        if (g_activeWeaponCache.size() > 256) g_activeWeaponCache.clear();

        CachedActiveWeapon& cached = g_activeWeaponCache[pawn];
        const ULONGLONG nowMs = GetTickCount64();
        if (cached.refreshMs != 0 && nowMs - cached.refreshMs < 150 &&
            (!includeAmmo || cached.includesAmmo)) {
            out = cached.info;
            return cached.valid;
        }

        cached.valid = ReadActiveWeaponInfo(pawn, entityList, includeAmmo, cached.info);
        cached.includesAmmo = includeAmmo;
        cached.refreshMs = nowMs;
        out = cached.info;
        return cached.valid;
    }

    static bool IsC4Entity(uintptr_t entity, uintptr_t& identityOut) {
        identityOut = mem.Read<uintptr_t>(entity + Offsets::m_pEntityIdentity);
        if (!IsValidPtr(identityOut)) return false;

        const auto cached = g_c4ClassCache.find(entity);
        if (cached != g_c4ClassCache.end() && cached->second.identity == identityOut)
            return cached->second.isC4;

        const uintptr_t nameAddress = mem.Read<uintptr_t>(
            identityOut + Offsets::m_designerName);
        if (!IsValidPtr(nameAddress)) return false;

        char name[32]{};
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(nameAddress),
            name, sizeof(name) - 1, &bytesRead) || bytesRead == 0)
            return false;
        const bool isC4 = strcmp(name, "weapon_c4") == 0;
        if (g_c4ClassCache.size() > 4096) g_c4ClassCache.clear();
        g_c4ClassCache[entity] = { identityOut, isC4 };
        return isC4;
    }

    static uintptr_t FindC4Entity(uintptr_t entityList, int chunkIndex,
        uintptr_t& identityOut) {
        if (!IsValidPtr(entityList) || (g_entityStride != 0x70 && g_entityStride != 0x78))
            return 0;

        // Some builds expose a wrapper at dwWeaponC4. Validate both possible
        // indirections before falling back to the global entity table.
        if (chunkIndex == 0) {
            const uintptr_t globalCandidate = mem.Read<uintptr_t>(
                mem.clientModule + Offsets::dwWeaponC4);
            const uintptr_t candidates[] = {
                globalCandidate,
                IsValidPtr(globalCandidate) ? mem.Read<uintptr_t>(globalCandidate) : 0
            };
            for (uintptr_t candidate : candidates) {
                if (IsValidPtr(candidate) && IsC4Entity(candidate, identityOut))
                    return candidate;
            }
        }

        const size_t chunkSize = static_cast<size_t>(g_entityStride) * 512;
        std::vector<uint8_t> chunkBytes(chunkSize);
        const uintptr_t chunk = mem.Read<uintptr_t>(
            entityList + 16 + 8 * chunkIndex);
        if (!IsValidPtr(chunk)) return 0;

        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(chunk),
            chunkBytes.data(), chunkBytes.size(), &bytesRead))
            return 0;
        for (int entry = 0; entry < 512; ++entry) {
            const size_t offset = static_cast<size_t>(entry) * g_entityStride;
            if (offset + sizeof(uintptr_t) > bytesRead) break;
            uintptr_t entity = 0;
            memcpy(&entity, chunkBytes.data() + offset, sizeof(entity));
            if (IsValidPtr(entity) && IsC4Entity(entity, identityOut))
                return entity;
        }
        return 0;
    }

    static uint32_t ReadCachedBombCarrier(uintptr_t entityList) {
        const ULONGLONG nowMs = GetTickCount64();
        if (IsValidPtr(g_bombCarrierCache.c4Entity)) {
            const uintptr_t currentIdentity = mem.Read<uintptr_t>(
                g_bombCarrierCache.c4Entity + Offsets::m_pEntityIdentity);
            if (currentIdentity != g_bombCarrierCache.identity) {
                g_bombCarrierCache.c4Entity = 0;
                g_bombCarrierCache.identity = 0;
                g_bombCarrierCache.ownerHandle = 0;
            }
        }

        if (!IsValidPtr(g_bombCarrierCache.c4Entity)) {
            if (nowMs < g_bombCarrierCache.nextScanMs)
                return 0;
            g_bombCarrierCache.c4Entity = FindC4Entity(
                entityList, g_bombCarrierCache.scanChunk, g_bombCarrierCache.identity);
            g_bombCarrierCache.ownerRefreshMs = 0;
            g_bombCarrierCache.ownerHandle = 0;
            if (IsValidPtr(g_bombCarrierCache.c4Entity)) {
                g_bombCarrierCache.scanChunk = 0;
                g_bombCarrierCache.nextScanMs = 0;
            } else {
                ++g_bombCarrierCache.scanChunk;
                if (g_bombCarrierCache.scanChunk >= 8) {
                    g_bombCarrierCache.scanChunk = 0;
                    g_bombCarrierCache.nextScanMs = nowMs + 2000;
                } else {
                    g_bombCarrierCache.nextScanMs = nowMs + 50;
                }
            }
        }

        if (!IsValidPtr(g_bombCarrierCache.c4Entity)) return 0;
        if (g_bombCarrierCache.ownerRefreshMs != 0 &&
            nowMs - g_bombCarrierCache.ownerRefreshMs < 100)
            return g_bombCarrierCache.ownerHandle;

        g_bombCarrierCache.ownerHandle = 0;
        g_bombCarrierCache.ownerRefreshMs = nowMs;
        const uint32_t ownerHandle = mem.Read<uint32_t>(
            g_bombCarrierCache.c4Entity + Offsets::m_hOwnerEntity);
        if (!ownerHandle || ownerHandle == 0xFFFFFFFF) return 0;
        g_bombCarrierCache.ownerHandle = ownerHandle;
        return g_bombCarrierCache.ownerHandle;
    }

    struct EspLabel {
        int anchor = EspTextTop;
        ImU32 color = IM_COL32_WHITE;
        std::string text;
        std::string icon;
        float iconSize = 0.0f;
        ImU32 iconColor = IM_COL32_WHITE;
        ImU32 backgroundColor = IM_COL32(0, 0, 0, 170);
    };

    static ImVec2 CalcEspLabelSize(const EspLabel& label) {
        const ImVec2 textSize = label.text.empty() ? ImVec2{} : ImGui::CalcTextSize(label.text.c_str());
        ImVec2 iconSize{};
        if (!label.icon.empty() && GetWeaponIconFont()) {
            iconSize = GetWeaponIconFont()->CalcTextSizeA(
                label.iconSize, FLT_MAX, 0.0f, label.icon.c_str());
        }
        const float gap = (iconSize.x > 0.0f && textSize.x > 0.0f) ? 4.0f : 0.0f;
        return ImVec2(iconSize.x + gap + textSize.x, (std::max)(iconSize.y, textSize.y));
    }

    static void DrawEspLabel(ImDrawList* drawList, const ImVec2& position,
        const ImVec2& size, const EspLabel& label) {
        drawList->AddRectFilled(
            ImVec2(position.x - 2.0f, position.y - 1.0f),
            ImVec2(position.x + size.x + 2.0f, position.y + size.y + 1.0f),
            label.backgroundColor);

        float x = position.x;
        if (!label.icon.empty() && GetWeaponIconFont()) {
            const ImVec2 iconSize = GetWeaponIconFont()->CalcTextSizeA(
                label.iconSize, FLT_MAX, 0.0f, label.icon.c_str());
            drawList->AddText(GetWeaponIconFont(), label.iconSize,
                ImVec2(x, position.y + (size.y - iconSize.y) * 0.5f),
                label.iconColor, label.icon.c_str());
            x += iconSize.x;
            if (!label.text.empty()) x += 4.0f;
        }
        if (!label.text.empty()) {
            const ImVec2 textSize = ImGui::CalcTextSize(label.text.c_str());
            drawList->AddText(ImVec2(x, position.y + (size.y - textSize.y) * 0.5f),
                label.color, label.text.c_str());
        }
    }

    static void DrawEspLabels(ImDrawList* drawList, float boxX, float boxY,
        float boxW, float boxH, float leftGap, float rightGap,
        const std::vector<EspLabel>& labels) {
        constexpr float spacing = 2.0f;
        constexpr float edgeGap = 4.0f;
        std::array<std::vector<const EspLabel*>, 4> grouped;
        for (const EspLabel& label : labels) {
            if ((!label.text.empty() || !label.icon.empty()) &&
                label.anchor >= EspTextTop && label.anchor <= EspTextRight)
                grouped[static_cast<size_t>(label.anchor)].push_back(&label);
        }

        float topOffset = edgeGap;
        for (const EspLabel* label : grouped[EspTextTop]) {
            const ImVec2 size = CalcEspLabelSize(*label);
            topOffset += size.y;
            DrawEspLabel(drawList,
                ImVec2(boxX + boxW * 0.5f - size.x * 0.5f, boxY - topOffset),
                size, *label);
            topOffset += spacing;
        }

        float bottomOffset = edgeGap;
        for (const EspLabel* label : grouped[EspTextBottom]) {
            const ImVec2 size = CalcEspLabelSize(*label);
            DrawEspLabel(drawList,
                ImVec2(boxX + boxW * 0.5f - size.x * 0.5f, boxY + boxH + bottomOffset),
                size, *label);
            bottomOffset += size.y + spacing;
        }

        auto drawSide = [&](int anchor, bool left) {
            const auto& sideLabels = grouped[static_cast<size_t>(anchor)];
            float totalHeight = 0.0f;
            for (const EspLabel* label : sideLabels)
                totalHeight += CalcEspLabelSize(*label).y;
            if (sideLabels.size() > 1)
                totalHeight += spacing * static_cast<float>(sideLabels.size() - 1);

            float y = boxY + boxH * 0.5f - totalHeight * 0.5f;
            for (const EspLabel* label : sideLabels) {
                const ImVec2 size = CalcEspLabelSize(*label);
                const float x = left ? boxX - leftGap - size.x : boxX + boxW + rightGap;
                DrawEspLabel(drawList, ImVec2(x, y), size, *label);
                y += size.y + spacing;
            }
        };

        drawSide(EspTextLeft, true);
        drawSide(EspTextRight, false);
    }

    static bool IsReasonableBounds(const Vector3& mins, const Vector3& maxs) {
        if (!std::isfinite(mins.x) || !std::isfinite(mins.y) || !std::isfinite(mins.z)) return false;
        if (!std::isfinite(maxs.x) || !std::isfinite(maxs.y) || !std::isfinite(maxs.z)) return false;
        if (!(mins.x < maxs.x && mins.y < maxs.y && mins.z < maxs.z)) return false;
        const float width = maxs.x - mins.x;
        const float depth = maxs.y - mins.y;
        const float height = maxs.z - mins.z;
        return width > 2.0f && width < 128.0f && depth > 2.0f && depth < 128.0f && height > 20.0f && height < 128.0f;
    }

    static bool ReadCollisionBounds(uintptr_t pawn, Vector3& minsOut, Vector3& maxsOut) {
        if (g_collisionBoundsCache.size() > 256) g_collisionBoundsCache.clear();
        CachedCollisionBounds& cached = g_collisionBoundsCache[pawn];
        const ULONGLONG nowMs = GetTickCount64();

        if (cached.boundsRefreshMs != 0 && nowMs - cached.boundsRefreshMs < 32) {
            minsOut = cached.mins;
            maxsOut = cached.maxs;
            return cached.valid;
        }
        cached.boundsRefreshMs = nowMs;

        if (!IsValidPtr(cached.collisionPtr) || nowMs - cached.sourceRefreshMs >= 2000) {
            cached.collisionPtr = mem.Read<uintptr_t>(pawn + OFF_m_pCollision);
            cached.sourceRefreshMs = nowMs;
        }

        CollisionBoundsBlock bounds{};
        SIZE_T bytesRead = 0;
        if (IsValidPtr(cached.collisionPtr) &&
            ReadProcessMemory(mem.hProcess,
                reinterpret_cast<LPCVOID>(cached.collisionPtr + OFF_collision_m_vecMins),
                &bounds, sizeof(bounds), &bytesRead) && bytesRead == sizeof(bounds) &&
            IsReasonableBounds(bounds.mins, bounds.maxs)) {
            cached.mins = bounds.mins;
            cached.maxs = bounds.maxs;
            cached.valid = true;
        } else {
            bytesRead = 0;
            if (ReadProcessMemory(mem.hProcess,
                reinterpret_cast<LPCVOID>(pawn + OFF_m_Collision + OFF_collision_m_vecMins),
                &bounds, sizeof(bounds), &bytesRead) && bytesRead == sizeof(bounds) &&
                IsReasonableBounds(bounds.mins, bounds.maxs)) {
                cached.mins = bounds.mins;
                cached.maxs = bounds.maxs;
                cached.valid = true;
            }
        }

        minsOut = cached.mins;
        maxsOut = cached.maxs;
        return cached.valid;
    }

    static bool ComputeCollisionScreenBox(const Vector3& mins, const Vector3& maxs,
        const Vector3& origin, const Matrix4x4& viewMatrix,
        int screenWidth, int screenHeight, float& xOut, float& yOut, float& wOut, float& hOut)
    {
        const Vector3 corners[8] = {
            { origin.x + mins.x, origin.y + mins.y, origin.z + mins.z },
            { origin.x + mins.x, origin.y + mins.y, origin.z + maxs.z },
            { origin.x + mins.x, origin.y + maxs.y, origin.z + mins.z },
            { origin.x + mins.x, origin.y + maxs.y, origin.z + maxs.z },
            { origin.x + maxs.x, origin.y + mins.y, origin.z + mins.z },
            { origin.x + maxs.x, origin.y + mins.y, origin.z + maxs.z },
            { origin.x + maxs.x, origin.y + maxs.y, origin.z + mins.z },
            { origin.x + maxs.x, origin.y + maxs.y, origin.z + maxs.z }
        };

        float minX = FLT_MAX, minY = FLT_MAX;
        float maxX = -FLT_MAX, maxY = -FLT_MAX;
        int projectedCorners = 0;

        for (const Vector3& corner : corners) {
            Vector3 screen{};
            if (!WorldToScreen(corner, screen, viewMatrix, screenWidth, screenHeight)) continue;
            ++projectedCorners;
            minX = (std::min)(minX, screen.x);
            minY = (std::min)(minY, screen.y);
            maxX = (std::max)(maxX, screen.x);
            maxY = (std::max)(maxY, screen.y);
        }

        // A partially projected volume is crossing the camera plane and cannot
        // produce a reliable player box. Let the caller use its safe fallback.
        if (projectedCorners != 8) return false;
        const float width = maxX - minX;
        const float height = maxY - minY;
        if (!(width > 2.0f && height > 2.0f) || width > height * 1.5f) return false;

        xOut = minX;
        yOut = minY;
        wOut = width;
        hOut = height;
        return true;
    }

    static bool ProjectExtended(const Vector3& world, Vector3& screen,
        const Matrix4x4& viewMatrix, int screenWidth, int screenHeight)
    {
        float clipW = 0.0f;
        const bool inRegularRange = WorldToScreen(world, screen, viewMatrix,
            screenWidth, screenHeight, nullptr, &clipW);
        if (!inRegularRange && (!std::isfinite(clipW) || clipW <= 0.01f))
            return false;
        return std::isfinite(screen.x) && std::isfinite(screen.y);
    }

    static bool ComputeSkeletonScreenBox(const SkeletonPose& pose,
        const Matrix4x4& viewMatrix, int screenWidth, int screenHeight,
        float& xOut, float& yOut, float& wOut, float& hOut)
    {
        float minX = FLT_MAX, minY = FLT_MAX;
        float maxX = -FLT_MAX, maxY = -FLT_MAX;
        int projectedBones = 0;
        for (size_t index = 0; index < kSkeletonBoneCount; ++index) {
            if (!pose.valid[index]) continue;
            Vector3 screen{};
            if (!ProjectExtended(pose.bones[index], screen, viewMatrix, screenWidth, screenHeight))
                continue;
            ++projectedBones;
            minX = (std::min)(minX, screen.x);
            minY = (std::min)(minY, screen.y);
            maxX = (std::max)(maxX, screen.x);
            maxY = (std::max)(maxY, screen.y);
        }
        if (projectedBones < 3 || maxX < 0.0f || minX > screenWidth ||
            maxY < 0.0f || minY > screenHeight)
            return false;

        const float rawWidth = maxX - minX;
        const float rawHeight = maxY - minY;
        if (rawWidth <= 1.0f || rawHeight <= 1.0f) return false;

        const float paddingX = (std::max)(3.0f, rawWidth * 0.12f);
        const float paddingY = (std::max)(2.0f, rawHeight * 0.025f);
        minX = std::clamp(minX - paddingX, -screenWidth * 0.25f, screenWidth * 1.25f);
        maxX = std::clamp(maxX + paddingX, -screenWidth * 0.25f, screenWidth * 1.25f);
        minY = std::clamp(minY - paddingY, -screenHeight * 0.25f, screenHeight * 1.25f);
        maxY = std::clamp(maxY + paddingY, -screenHeight * 0.25f, screenHeight * 1.25f);

        const float width = maxX - minX;
        const float height = maxY - minY;
        if (width <= 2.0f || height <= 2.0f) return false;
        xOut = minX;
        yOut = minY;
        wOut = width;
        hOut = height;
        return true;
    }

    struct PreparedEspEntity {
        Vector3 feetWorld{};
        Vector3 collisionMins{};
        Vector3 collisionMaxs{};
        bool hasCollisionBounds = false;
        SkeletonPose skeletonPose{};
        bool hasSkeletonPose = false;
        std::string playerName;
        int armor = 0;
        bool hasHelmet = false;
        bool hasBomb = false;
        ActiveWeaponInfo activeWeapon{};
        bool hasActiveWeapon = false;
    };
}

void RenderESP(int screenWidth, int screenHeight) {
    if (!mem.clientModule) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    // Draw C4 carrier marker and defuse-kit marker per-player (inside per-entity loop)

    int drawn = 0;
    int scanned = 0;

    // Performance measurements (ms)
    double perf_totalMs = 0.0;
    double perf_readMs = 0.0;
    double perf_w2sMs = 0.0;
    double perf_drawMs = 0.0;
    auto perf_frameStart = std::chrono::high_resolution_clock::now();

    // The main loop refreshes the snapshot before gameplay features run. ESP is
    // intentionally rendered last so the camera matrix is sampled near Present.
    const FrameSnapshot& frame = GetCurrentFrameSnapshot();
    uintptr_t entityList = GetEntityListBase();
    uintptr_t localController = frame.localController;
    uintptr_t localPawn = frame.localPawn;
    int localTeam = frame.localTeam;
    int localPlayerIndex = frame.localPlayerIndex;

    if (!IsValidPtr(entityList)) return;
    const uint32_t bombOwnerHandle = g_Esp.showBombCarrier
        ? ReadCachedBombCarrier(entityList)
        : 0;

    // Finish every per-entity memory read before sampling the camera. This keeps
    // all boxes on one late matrix even when the view changes rapidly.
    static std::vector<PreparedEspEntity> preparedEntities;
    static std::vector<uint8_t> preparedValid;
    preparedEntities.assign(frame.entities.size(), PreparedEspEntity{});
    preparedValid.assign(frame.entities.size(), 0);
    int weaponResolvedFrame = 0;
    int weaponFailedFrame = 0;
    for (size_t entityIndex = 0; entityIndex < frame.entities.size(); ++entityIndex) {
        const EntitySnapshot& snap = frame.entities[entityIndex];
        if (snap.lifeState != 0 || snap.health <= 0 || snap.health > 100) continue;

        const bool isTeammate = (localTeam != 0 && snap.team == localTeam);
        if (isTeammate && !g_Esp.showTeammates) continue;

        PreparedEspEntity prepared{};
        prepared.feetWorld = IsValidPtr(snap.sceneNode)
            ? mem.Read<Vector3>(snap.sceneNode + Offsets::m_vecAbsOrigin)
            : GetPawnWorldPos(snap.pawn);
        if (!std::isfinite(prepared.feetWorld.x) || !std::isfinite(prepared.feetWorld.y) ||
            !std::isfinite(prepared.feetWorld.z) ||
            (std::fabs(prepared.feetWorld.x) < 1.0f && std::fabs(prepared.feetWorld.y) < 1.0f &&
                std::fabs(prepared.feetWorld.z) < 1.0f)) {
            prepared.feetWorld = snap.worldPos;
        }
        if (!std::isfinite(prepared.feetWorld.x) || !std::isfinite(prepared.feetWorld.y) ||
            !std::isfinite(prepared.feetWorld.z)) continue;
        if (std::fabs(prepared.feetWorld.x) < 1.0f && std::fabs(prepared.feetWorld.y) < 1.0f &&
            std::fabs(prepared.feetWorld.z) < 1.0f) continue;

        prepared.hasCollisionBounds = ReadCollisionBounds(
            snap.pawn, prepared.collisionMins, prepared.collisionMaxs);
        if (g_Esp.showSkeleton || g_Esp.enableOtherGlow)
            prepared.hasSkeletonPose = ReadSkeletonPose(snap.pawn, prepared.feetWorld, prepared.skeletonPose);

        if (g_Esp.showNames) {
            char playerName[128] = {};
            ReadPlayerName(snap.controller, playerName, sizeof(playerName));
            if (playerName[0] == '\0') snprintf(playerName, sizeof(playerName), "Player %d", snap.index);
            prepared.playerName = playerName;
        }

        prepared.armor = snap.armor;
        prepared.hasHelmet = snap.hasHelmet;
        if ((g_Esp.showArmorBar || g_Esp.showArmorText) && !snap.hasHelmet)
            GetPlayerArmorInfo(snap.pawn, prepared.armor, prepared.hasHelmet);

        if (g_Esp.showActiveWeapon) {
            const bool includeAmmo = g_Esp.showActiveWeapon && g_Esp.showWeaponAmmo;
            prepared.hasActiveWeapon = ReadCachedActiveWeapon(
                snap.pawn, entityList, includeAmmo, prepared.activeWeapon);
            if (g_Esp.showActiveWeapon) {
                if (prepared.hasActiveWeapon) {
                    ++weaponResolvedFrame;
                    Stats::weaponLastDefinition.store(prepared.activeWeapon.definitionIndex);
                    Stats::weaponLastClip.store(prepared.activeWeapon.clipAmmo);
                } else {
                    ++weaponFailedFrame;
                }
            }
        }
        prepared.hasBomb = snap.team == 2 && bombOwnerHandle != 0 &&
            snap.pawnHandle == bombOwnerHandle;

        preparedEntities[entityIndex] = std::move(prepared);
        preparedValid[entityIndex] = 1;
    }
    Stats::weaponResolvedCount.store(weaponResolvedFrame);
    Stats::weaponFailedCount.store(weaponFailedFrame);

    Matrix4x4 viewMatrix{};
    if (!ReadViewMatrix(viewMatrix)) viewMatrix = frame.viewMatrix;

    if (g_Aim.enabled && g_Aim.drawFov) {
        const float fovDegrees = GetAimFovDegreesForScope(frame.localScopeLevel);
        const float radius = CalculateAimFovRadiusPixels(
            viewMatrix, screenWidth, screenHeight, fovDegrees);
        if (radius > 0.0f) {
            drawList->AddCircle(ImVec2(screenWidth * 0.5f, screenHeight * 0.5f),
                radius, IM_COL32(200, 40, 40, 180), 96, 2.0f);
        }
    }

    // iterate over snapshot entities (already read)
    for (size_t entityIndex = 0; entityIndex < frame.entities.size(); ++entityIndex) {
        const EntitySnapshot& snap = frame.entities[entityIndex];
        ++scanned;

        bool aliveOnPawn = (snap.lifeState == 0);
        // We no longer have controller here; assume snapshot contains authoritative lifeState
        if (!aliveOnPawn) continue;

        int team = snap.team;
        bool isTeammate = (localTeam != 0 && team == localTeam);
        if (isTeammate && !g_Esp.showTeammates) continue;

        int health = snap.health;
        if (health <= 0 || health > 100) continue;

        if (!preparedValid[entityIndex]) continue;
        const PreparedEspEntity& prepared = preparedEntities[entityIndex];

        bool visible = IsPawnVisibleInSnapshot(snap, localPlayerIndex);

        const Vector3& feetWorld = prepared.feetWorld;

        Vector3 headWorld = { feetWorld.x, feetWorld.y, feetWorld.z + 72.0f };
        Vector3 feetScreen, headScreen;
        auto t_w2s_start = std::chrono::high_resolution_clock::now();
        const bool feetProjected = ProjectExtended(
            feetWorld, feetScreen, viewMatrix, screenWidth, screenHeight);
        const bool headProjected = ProjectExtended(
            headWorld, headScreen, viewMatrix, screenWidth, screenHeight);
        auto t_w2s_end = std::chrono::high_resolution_clock::now();
        perf_w2sMs += std::chrono::duration<double, std::milli>(t_w2s_end - t_w2s_start).count();

        float boxHeight = 0.0f;
        float boxWidth = 0.0f;
        float topLeftX = 0.0f;
        float topLeftY = 0.0f;
        bool hasScreenBox = false;

        float collisionBoxX = 0.0f, collisionBoxY = 0.0f, collisionBoxW = 0.0f, collisionBoxH = 0.0f;
        if (prepared.hasCollisionBounds &&
            ComputeCollisionScreenBox(prepared.collisionMins, prepared.collisionMaxs,
            feetWorld, viewMatrix, screenWidth, screenHeight,
            collisionBoxX, collisionBoxY, collisionBoxW, collisionBoxH))
        {
            topLeftX = collisionBoxX;
            topLeftY = collisionBoxY;
            boxWidth = collisionBoxW;
            boxHeight = collisionBoxH;
            hasScreenBox = true;
        } else if (prepared.hasSkeletonPose &&
            ComputeSkeletonScreenBox(prepared.skeletonPose, viewMatrix,
                screenWidth, screenHeight, topLeftX, topLeftY, boxWidth, boxHeight)) {
            hasScreenBox = true;
        } else if (feetProjected && headProjected) {
            const float fallbackHeight = feetScreen.y - headScreen.y;
            if (fallbackHeight > 2.0f) {
                boxHeight = (std::min)(fallbackHeight, screenHeight * 1.5f);
                boxWidth = boxHeight / 1.8f;
                topLeftX = headScreen.x - boxWidth * 0.5f;
                topLeftY = std::clamp(headScreen.y,
                    -screenHeight * 0.25f, screenHeight * 1.25f);
                hasScreenBox = true;
            }
        }

        if (!hasScreenBox || boxHeight <= 2.0f || boxWidth <= 2.0f) continue;

        // start draw timer for this entity
        auto t_draw_start = std::chrono::high_resolution_clock::now();

        ImU32 boxColor = GetVisibilityColor(visible, g_Esp.visibilityBoxes,
            g_Esp.boxVisibleR, g_Esp.boxVisibleG, g_Esp.boxVisibleB,
            g_Esp.boxHiddenR, g_Esp.boxHiddenG, g_Esp.boxHiddenB,
            isTeammate ? g_Esp.teamBoxR : g_Esp.enemyBoxR,
            isTeammate ? g_Esp.teamBoxG : g_Esp.enemyBoxG,
            isTeammate ? g_Esp.teamBoxB : g_Esp.enemyBoxB);

        if (g_Esp.enableOtherGlow && prepared.hasSkeletonPose) {
            int glowR = g_Esp.glowStaticR;
            int glowG = g_Esp.glowStaticG;
            int glowB = g_Esp.glowStaticB;
            if (g_Esp.otherGlowUseStaticColor) {
                glowR = g_Esp.otherGlowStaticR;
                glowG = g_Esp.otherGlowStaticG;
                glowB = g_Esp.otherGlowStaticB;
            } else {
                glowR = visible ? g_Esp.glowVisibleR : g_Esp.glowInvisibleR;
                glowG = visible ? g_Esp.glowVisibleG : g_Esp.glowInvisibleG;
                glowB = visible ? g_Esp.glowVisibleB : g_Esp.glowInvisibleB;
            }
            const ImU32 otherGlowColor = IM_COL32(glowR, glowG, glowB,
                std::clamp(g_Esp.otherGlowAlpha, 0, 255));
            SubmitOtherGlowPose(prepared.skeletonPose, viewMatrix,
                screenWidth, screenHeight, otherGlowColor, boxHeight,
                g_Esp.otherGlowBodyScale);
        }

        if (g_Esp.showSkeleton && prepared.hasSkeletonPose) {
            ImU32 skeletonColor = IM_COL32(
                g_Esp.skeletonR, g_Esp.skeletonG, g_Esp.skeletonB, g_Esp.skeletonAlpha);
            if (g_Esp.skeletonColorMode == 0) {
                skeletonColor = visible
                    ? IM_COL32(g_Esp.skeletonVisibleR, g_Esp.skeletonVisibleG,
                        g_Esp.skeletonVisibleB, g_Esp.skeletonAlpha)
                    : IM_COL32(g_Esp.skeletonHiddenR, g_Esp.skeletonHiddenG,
                        g_Esp.skeletonHiddenB, g_Esp.skeletonAlpha);
            } else if (g_Esp.skeletonColorMode == 2) {
                skeletonColor = boxColor;
            }
            DrawSkeletonPose(drawList, prepared.skeletonPose, viewMatrix, screenWidth, screenHeight,
                skeletonColor, g_Esp.skeletonThickness, g_Esp.skeletonShowJoints, g_Esp.skeletonScale);
        }

        const float barThickness = 3.0f;
        const float barPadding = 5.0f;
        float hpBarX = topLeftX - barPadding - barThickness;
        float armorBarX = topLeftX + boxWidth + barPadding;
        if (g_Esp.showBoxes) {
        auto t_draw_start = std::chrono::high_resolution_clock::now();
        if (g_Esp.showHpBar) {
                drawList->AddRectFilled({ hpBarX, topLeftY }, { hpBarX + barThickness, topLeftY + boxHeight }, IM_COL32(21, 21, 21, 200));
                float hpFactor = (std::min)(1.0f, (std::max)(0.0f, static_cast<float>(health) / 100.0f));
                float hpBarHeight = boxHeight * hpFactor;
                float hpBarTopY = topLeftY + boxHeight - hpBarHeight;
                ImU32 hpFill = IM_COL32(
                    (ImU32)((1.0f - hpFactor) * 255.0f),
                    (ImU32)(hpFactor * 255.0f),
                    0, 255);
                drawList->AddRectFilled({ hpBarX, hpBarTopY }, { hpBarX + barThickness, topLeftY + boxHeight }, hpFill);
                drawList->AddRect({ hpBarX - 1, topLeftY - 1 }, { hpBarX + barThickness + 1, topLeftY + boxHeight + 1 }, IM_COL32(0, 0, 0, 180));
            }

        if (g_Esp.showArmorBar) {
            int armor = prepared.armor;
            bool hasHelmet = prepared.hasHelmet;
                drawList->AddRectFilled({ armorBarX, topLeftY }, { armorBarX + barThickness, topLeftY + boxHeight }, IM_COL32(21, 21, 21, 200));
                float armorFactor = (std::min)(1.0f, (std::max)(0.0f, static_cast<float>(armor) / 100.0f));
                float armorBarHeight = boxHeight * armorFactor;
                float armorBarTopY = topLeftY + boxHeight - armorBarHeight;
                ImU32 armorFill = EspColor(g_Esp.armorBarR, g_Esp.armorBarG, g_Esp.armorBarB);
                if (hasHelmet)
                    armorFill = EspColor(
                        (std::min)(255, g_Esp.armorBarR + 40),
                        (std::min)(255, g_Esp.armorBarG + 40),
                        255);
                drawList->AddRectFilled({ armorBarX, armorBarTopY }, { armorBarX + barThickness, topLeftY + boxHeight }, armorFill);
                drawList->AddRect({ armorBarX - 1, topLeftY - 1 }, { armorBarX + barThickness + 1, topLeftY + boxHeight + 1 }, IM_COL32(0, 0, 0, 180));
            }

            DrawEspBox(drawList, topLeftX, topLeftY, boxWidth, boxHeight, boxColor, g_Esp.boxOutlineThickness);
        }

        std::vector<EspLabel> labels;
        labels.reserve(8);
        if (g_Esp.showNames) {
            const ImU32 nameColor = GetVisibilityColor(visible, g_Esp.visibilityNames,
                g_Esp.nameVisibleR, g_Esp.nameVisibleG, g_Esp.nameVisibleB,
                g_Esp.nameHiddenR, g_Esp.nameHiddenG, g_Esp.nameHiddenB,
                g_Esp.nameTextR, g_Esp.nameTextG, g_Esp.nameTextB);
            labels.push_back({ g_Esp.nameTextAnchor, nameColor, prepared.playerName });
        }
        if (g_Esp.showHpText) {
            char hpBuf[32] = {};
            snprintf(hpBuf, sizeof(hpBuf), "%d HP", health);
            labels.push_back({ g_Esp.hpTextAnchor,
                EspColor(g_Esp.hpTextR, g_Esp.hpTextG, g_Esp.hpTextB), hpBuf });
        }
        if (g_Esp.showArmorText) {
            char armorBuf[32] = {};
            snprintf(armorBuf, sizeof(armorBuf), "%d ARM", (std::max)(0, prepared.armor));
            labels.push_back({ g_Esp.armorTextAnchor,
                EspColor(g_Esp.armorBarR, g_Esp.armorBarG, g_Esp.armorBarB), armorBuf });
        }
        if (g_Esp.showActiveWeapon && prepared.hasActiveWeapon) {
            EspLabel weaponLabel{};
            weaponLabel.anchor = g_Esp.weaponTextAnchor;
            weaponLabel.color = EspColor(
                g_Esp.weaponTextR, g_Esp.weaponTextG, g_Esp.weaponTextB);
            weaponLabel.iconColor = EspColor(
                g_Esp.weaponIconR, g_Esp.weaponIconG, g_Esp.weaponIconB);
            weaponLabel.backgroundColor = IM_COL32(
                g_Esp.weaponBackgroundR, g_Esp.weaponBackgroundG,
                g_Esp.weaponBackgroundB, g_Esp.weaponBackgroundAlpha);

            const bool wantsIcon = g_Esp.weaponDisplayMode != 0;
            const bool iconAvailable = wantsIcon &&
                HasWeaponIcon(prepared.activeWeapon.definitionIndex);
            if (iconAvailable) {
                weaponLabel.icon = GetWeaponIconUtf8(prepared.activeWeapon.definitionIndex);
                weaponLabel.iconSize = g_Esp.weaponIconSize;
                if (g_Esp.weaponDisplayMode == 2) {
                    weaponLabel.text = FormatWeaponDisplayText(
                        prepared.activeWeapon, g_Esp.showWeaponAmmo);
                } else if (g_Esp.showWeaponAmmo && prepared.activeWeapon.clipAmmo >= 0) {
                    weaponLabel.text = std::to_string(prepared.activeWeapon.clipAmmo);
                }
            } else {
                weaponLabel.text = FormatWeaponDisplayText(
                    prepared.activeWeapon, g_Esp.showWeaponAmmo);
            }
            labels.push_back(std::move(weaponLabel));
        }

        auto addEquipmentLabel = [&](bool show, const char* text, const std::string& icon,
            int red, int green, int blue) {
            if (!show) return;
            EspLabel label{};
            label.anchor = g_Esp.equipmentTextAnchor;
            label.color = EspColor(red, green, blue);
            label.iconColor = label.color;
            label.iconSize = g_Esp.equipmentIconSize;
            label.backgroundColor = IM_COL32(
                g_Esp.weaponBackgroundR, g_Esp.weaponBackgroundG,
                g_Esp.weaponBackgroundB, g_Esp.weaponBackgroundAlpha);
            if (g_Esp.equipmentDisplayMode != 0 && !icon.empty())
                label.icon = icon;
            if (g_Esp.equipmentDisplayMode != 1 || icon.empty())
                label.text = text;
            labels.push_back(std::move(label));
        };

        addEquipmentLabel(g_Esp.showBombCarrier && prepared.hasBomb,
            "C4", GetWeaponIconUtf8(49),
            g_Esp.bombIconR, g_Esp.bombIconG, g_Esp.bombIconB);
        addEquipmentLabel(g_Esp.showDefuseKits && snap.team == 3 && snap.hasDefuser,
            "KIT", GetEquipmentIconUtf8(0xE066),
            g_Esp.defuseIconR, g_Esp.defuseIconG, g_Esp.defuseIconB);
        addEquipmentLabel(g_Esp.showArmorIndicator && prepared.armor > 0,
            "CHALECO", GetEquipmentIconUtf8(0xE064),
            g_Esp.armorIconR, g_Esp.armorIconG, g_Esp.armorIconB);
        addEquipmentLabel(g_Esp.showHelmetIndicator && prepared.hasHelmet,
            "CASCO", GetEquipmentIconUtf8(0xE065),
            g_Esp.helmetIconR, g_Esp.helmetIconG, g_Esp.helmetIconB);

        const float leftTextGap = 8.0f +
            ((g_Esp.showBoxes && g_Esp.showHpBar) ? barPadding + barThickness : 0.0f);
        const float rightTextGap = 8.0f +
            ((g_Esp.showBoxes && g_Esp.showArmorBar) ? barPadding + barThickness : 0.0f);
        DrawEspLabels(drawList, topLeftX, topLeftY, boxWidth, boxHeight,
            leftTextGap, rightTextGap, labels);

        auto t_draw_end = std::chrono::high_resolution_clock::now();
        perf_drawMs += std::chrono::duration<double, std::milli>(t_draw_end - t_draw_start).count();
        ++drawn;
    }

    auto perf_frameEnd = std::chrono::high_resolution_clock::now();
    perf_totalMs = std::chrono::duration<double, std::milli>(perf_frameEnd - perf_frameStart).count();
    Stats::lastRenderMs.store(perf_totalMs);

    if (g_Esp.showDebug) {
        char line1[192];
        char line2[192];
        char line3[192];
        snprintf(line1, sizeof(line1),
            "ESP drawn:%d scanned:%d stride:0x%llX team:%d idx:%d",
            drawn, scanned,
            (unsigned long long)g_entityStride, localTeam, localPlayerIndex);
        snprintf(line2, sizeof(line2),
            "pawn:0x%llX ctrl:0x%llX list:0x%llX",
            (unsigned long long)localPawn,
            (unsigned long long)localController,
            (unsigned long long)entityList);
        snprintf(line3, sizeof(line3), "Weapon OK:%d Fail:%d last ID:%d ammo:%d",
            Stats::weaponResolvedCount.load(), Stats::weaponFailedCount.load(),
            Stats::weaponLastDefinition.load(), Stats::weaponLastClip.load());
        DrawOutlinedText(drawList, ImVec2(10.0f, (float)screenHeight - 82.0f), IM_COL32(255, 255, 0, 255), line1);
        DrawOutlinedText(drawList, ImVec2(10.0f, (float)screenHeight - 22.0f), IM_COL32(255, 255, 0, 255), line2);
        DrawOutlinedText(drawList, ImVec2(10.0f, (float)screenHeight - 42.0f), IM_COL32(255, 255, 0, 255), line3);

        // Performance debug
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "perf total=" << perf_totalMs << "ms read=" << perf_readMs << "ms w2s=" << perf_w2sMs << "ms draw=" << perf_drawMs << "ms";
        std::string perfStr = ss.str();
        DrawOutlinedText(drawList, ImVec2(10.0f, (float)screenHeight - 62.0f), IM_COL32(200, 200, 50, 255), perfStr.c_str());
    }

   
   
}
