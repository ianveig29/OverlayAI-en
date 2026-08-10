// ============================================================
// SmokeColor.cpp
// Allows changing the color of smoke grenades in the game.
// ============================================================

#include "SmokeColor.h"

#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include "Stats.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
    constexpr int kSmokeEntityScanLimit = 4096;
    constexpr ULONGLONG kSmokeColorScanIntervalMs = 75;
    constexpr ULONGLONG kNegativeClassRetryMs = 500;

    struct SmokeClassCacheEntry {
        uintptr_t identity = 0;
        bool isSmoke = false;
        ULONGLONG lastProbeMs = 0;
    };

    struct SmokeColorOverride {
        uintptr_t identity = 0;
        Vector3 original{};
        float colorScale = 255.0f;
        bool originalResolved = false;
    };

    std::unordered_map<uintptr_t, SmokeClassCacheEntry> g_classCache;
    std::unordered_map<uintptr_t, SmokeColorOverride> g_colorOverrides;
    ULONGLONG g_lastScanMs = 0;
    int g_detectedProjectiles = 0;
    int g_tintedProjectiles = 0;

    bool IsFiniteColor(const Vector3& color) {
        return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z) &&
            std::fabs(color.x) < 4096.0f && std::fabs(color.y) < 4096.0f &&
            std::fabs(color.z) < 4096.0f;
    }

    bool ReadRemoteString(uintptr_t address, char* output, size_t outputSize) {
        if (!IsValidPtr(address) || !output || outputSize < 2) return false;
        memset(output, 0, outputSize);
        SIZE_T bytesRead = 0;
        const bool read = ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(address),
            output, outputSize - 1, &bytesRead) && bytesRead > 0;
        Stats::rpmReadCount.fetch_add(1);
        output[outputSize - 1] = '\0';
        return read && output[0] != '\0';
    }

    bool IsSmokeProjectile(uintptr_t entity, ULONGLONG nowMs, uintptr_t& identityOut) {
        identityOut = mem.Read<uintptr_t>(entity + Offsets::m_pEntityIdentity);
        if (!IsValidPtr(identityOut)) return false;

        auto cached = g_classCache.find(entity);
        if (cached != g_classCache.end() && cached->second.identity == identityOut) {
            if (cached->second.isSmoke) return true;
            if (nowMs - cached->second.lastProbeMs < kNegativeClassRetryMs) return false;
        }

        const uintptr_t designerName = mem.Read<uintptr_t>(identityOut + Offsets::m_designerName);
        char name[64]{};
        bool isSmoke = false;
        if (ReadRemoteString(designerName, name, sizeof(name))) {
            std::string normalized(name);
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            isSmoke = normalized.find("smoke") != std::string::npos &&
                normalized.find("projectile") != std::string::npos;
        }
        g_classCache[entity] = { identityOut, isSmoke, nowMs };
        return isSmoke;
    }

    bool WriteSmokeColor(uintptr_t entity, const Vector3& color) {
        SIZE_T bytesWritten = 0;
        const bool wrote = WriteProcessMemory(mem.hProcess,
            reinterpret_cast<LPVOID>(entity + Offsets::m_vSmokeColor),
            &color, sizeof(color), &bytesWritten) && bytesWritten == sizeof(color);
        if (wrote) Stats::rpmWriteCount.fetch_add(1);
        return wrote;
    }

    std::vector<uintptr_t> ReadActiveEntities() {
        std::vector<uintptr_t> entities;
        const uintptr_t entitySystem = GetEntityListBase();
        if (!IsValidPtr(entitySystem) || g_entityStride < sizeof(uintptr_t) || g_entityStride > 0x100)
            return entities;

        // Temporary projectiles can live in chunks beyond the reported highest
        // networked index. Scan every entity chunk instead of truncating at it.
        constexpr int scanLimit = kSmokeEntityScanLimit;

        entities.reserve((std::min)(scanLimit, 512));
        constexpr int entriesPerBatch = 32;
        const size_t batchSize = static_cast<size_t>(g_entityStride) * entriesPerBatch;
        std::vector<uint8_t> batchBytes(batchSize);
        const int lastChunk = (scanLimit - 1) >> 9;
        for (int chunkIndex = 0; chunkIndex <= lastChunk; ++chunkIndex) {
            const uintptr_t chunk = mem.Read<uintptr_t>(entitySystem + 16 + 8 * chunkIndex);
            if (!IsValidPtr(chunk)) continue;

            for (int batchStart = 0; batchStart < 512; batchStart += entriesPerBatch) {
                const int globalStart = (chunkIndex << 9) + batchStart;
                if (globalStart >= scanLimit) break;
                const int entriesToRead = (std::min)(entriesPerBatch, scanLimit - globalStart);
                const size_t bytesRequested = static_cast<size_t>(g_entityStride) * entriesToRead;
                SIZE_T bytesRead = 0;
                (void)ReadProcessMemory(mem.hProcess,
                    reinterpret_cast<LPCVOID>(chunk + static_cast<uintptr_t>(batchStart) * g_entityStride),
                    batchBytes.data(), bytesRequested, &bytesRead);
                Stats::rpmReadCount.fetch_add(1);
                if (bytesRead < sizeof(uintptr_t)) continue;

                for (int entry = 0; entry < entriesToRead; ++entry) {
                    const size_t offset = static_cast<size_t>(entry) * g_entityStride;
                    if (offset + sizeof(uintptr_t) > bytesRead) break;
                    uintptr_t entity = 0;
                    memcpy(&entity, batchBytes.data() + offset, sizeof(entity));
                    if (IsValidPtr(entity)) entities.push_back(entity);
                }
            }
        }
        return entities;
    }
}

void RestoreSmokeColors() {
    if (!mem.hProcess) {
        g_colorOverrides.clear();
        return;
    }

    for (const auto& [entity, state] : g_colorOverrides) {
        if (!IsValidPtr(entity)) continue;
        const uintptr_t currentIdentity = mem.Read<uintptr_t>(entity + Offsets::m_pEntityIdentity);
        if (currentIdentity == state.identity)
            (void)WriteSmokeColor(entity, state.original);
    }
    g_colorOverrides.clear();
    g_classCache.clear();
    g_lastScanMs = 0;
    g_detectedProjectiles = 0;
    g_tintedProjectiles = 0;
}

void UpdateSmokeColors(bool enabled, const Vector3& rgb255) {
    if (!enabled) {
        if (!g_colorOverrides.empty()) RestoreSmokeColors();
        if (!g_classCache.empty()) g_classCache.clear();
        g_detectedProjectiles = 0;
        g_tintedProjectiles = 0;
        return;
    }
    if (!mem.hProcess || !mem.clientModule || !IsFiniteColor(rgb255)) return;

    const ULONGLONG nowMs = GetTickCount64();
    if (g_lastScanMs != 0 && nowMs - g_lastScanMs < kSmokeColorScanIntervalMs) return;
    g_lastScanMs = nowMs;

    if (g_classCache.size() > 4096) g_classCache.clear();
    std::unordered_set<uintptr_t> seenSmokeEntities;
    int tintedProjectiles = 0;
    const std::vector<uintptr_t> activeEntities = ReadActiveEntities();
    for (uintptr_t entity : activeEntities) {
        uintptr_t identity = 0;
        if (!IsSmokeProjectile(entity, nowMs, identity)) continue;
        seenSmokeEntities.insert(entity);

        auto overrideIt = g_colorOverrides.find(entity);
        if (overrideIt == g_colorOverrides.end() || overrideIt->second.identity != identity) {
            const Vector3 original = mem.Read<Vector3>(entity + Offsets::m_vSmokeColor);
            if (!IsFiniteColor(original)) continue;
            const float largest = (std::max)({ std::fabs(original.x), std::fabs(original.y),
                std::fabs(original.z) });
            const bool originalResolved = largest >= 0.001f;
            const float scale = originalResolved && largest <= 1.5f ? 1.0f : 255.0f;
            overrideIt = g_colorOverrides.insert_or_assign(entity,
                SmokeColorOverride{ identity, original, scale, originalResolved }).first;
        }

        const float tentativeScale = overrideIt->second.colorScale / 255.0f;
        const Vector3 tentativeTarget{
            (std::clamp)(rgb255.x, 0.0f, 255.0f) * tentativeScale,
            (std::clamp)(rgb255.y, 0.0f, 255.0f) * tentativeScale,
            (std::clamp)(rgb255.z, 0.0f, 255.0f) * tentativeScale
        };
        const Vector3 current = mem.Read<Vector3>(entity + Offsets::m_vSmokeColor);
        const float currentLargest = (std::max)({ std::fabs(current.x), std::fabs(current.y),
            std::fabs(current.z) });
        const bool currentMatchesTarget = IsFiniteColor(current) &&
            std::fabs(current.x - tentativeTarget.x) <= 0.01f &&
            std::fabs(current.y - tentativeTarget.y) <= 0.01f &&
            std::fabs(current.z - tentativeTarget.z) <= 0.01f;
        if (!overrideIt->second.originalResolved && IsFiniteColor(current) &&
            currentLargest >= 0.001f && !currentMatchesTarget) {
            overrideIt->second.original = current;
            overrideIt->second.colorScale = currentLargest <= 1.5f ? 1.0f : 255.0f;
            overrideIt->second.originalResolved = true;
        }

        const float finalScale = overrideIt->second.colorScale / 255.0f;
        const Vector3 target{
            (std::clamp)(rgb255.x, 0.0f, 255.0f) * finalScale,
            (std::clamp)(rgb255.y, 0.0f, 255.0f) * finalScale,
            (std::clamp)(rgb255.z, 0.0f, 255.0f) * finalScale
        };
        const bool alreadyTinted = IsFiniteColor(current) &&
            std::fabs(current.x - target.x) <= 0.01f &&
            std::fabs(current.y - target.y) <= 0.01f &&
            std::fabs(current.z - target.z) <= 0.01f;
        const bool wroteColor = !alreadyTinted && WriteSmokeColor(entity, target);
        if (alreadyTinted || wroteColor)
            ++tintedProjectiles;
    }

    for (auto it = g_colorOverrides.begin(); it != g_colorOverrides.end();) {
        if (seenSmokeEntities.find(it->first) == seenSmokeEntities.end())
            it = g_colorOverrides.erase(it);
        else
            ++it;
    }
    g_detectedProjectiles = static_cast<int>(seenSmokeEntities.size());
    g_tintedProjectiles = tintedProjectiles;
}

int GetDetectedSmokeProjectileCount() {
    return g_detectedProjectiles;
}

int GetTintedSmokeProjectileCount() {
    return g_tintedProjectiles;
}
