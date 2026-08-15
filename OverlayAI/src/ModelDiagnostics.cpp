#include "ModelDiagnostics.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include <windows.h>

namespace {
    ModelDiagnostics g_diagnostics;
    ULONGLONG g_lastSampleMs = 0;
    ULONGLONG g_lastHitboxScanMs = 0;
    uintptr_t g_lastScannedModel = 0;

    bool ReadBytes(uintptr_t address, void* destination, size_t size) {
        if (!IsValidPtr(address) || !destination || size == 0) return false;
        SIZE_T bytesRead = 0;
        return ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(address),
            destination, size, &bytesRead) && bytesRead == size;
    }

    bool ReadString(uintptr_t address, std::string& output) {
        output.clear();
        if (!IsValidPtr(address)) return false;
        std::array<char, 192> buffer{};
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(address),
            buffer.data(), buffer.size() - 1, &bytesRead) || bytesRead == 0) return false;
        buffer.back() = '\0';
        const size_t length = strnlen_s(buffer.data(), buffer.size());
        if (length < 4 || length >= buffer.size() - 1) return false;
        for (size_t index = 0; index < length; ++index) {
            const unsigned char value = static_cast<unsigned char>(buffer[index]);
            if (value < 0x20 || value > 0x7E) return false;
        }
        output.assign(buffer.data(), length);
        return true;
    }

    bool IsModelResourceName(const std::string& value) {
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower.find(".vmdl") != std::string::npos ||
            lower.find("characters/models/") != std::string::npos;
    }

    void AddCandidate(std::vector<uintptr_t>& candidates, uintptr_t value) {
        if (!IsValidPtr(value)) return;
        if (std::find(candidates.begin(), candidates.end(), value) == candidates.end())
            candidates.push_back(value);
    }

    bool IsFiniteBound(float value) {
        return std::isfinite(value) && std::fabs(value) <= 256.0f;
    }

    bool FindHitboxSetList(uintptr_t model, uintptr_t& rootOut, int& offsetOut,
        int& setCountOut, int& hitboxCountOut, std::string& firstBoneOut) {
        rootOut = 0;
        offsetOut = -1;
        setCountOut = 0;
        hitboxCountOut = 0;
        firstBoneOut.clear();
        if (!IsValidPtr(model)) return false;

        std::array<unsigned char, 0x800> modelBlock{};
        if (!ReadBytes(model, modelBlock.data(), modelBlock.size())) return false;
        std::vector<uintptr_t> roots;
        roots.reserve(128);
        AddCandidate(roots, model);
        for (size_t offset = 0; offset + sizeof(uintptr_t) <= modelBlock.size(); offset += 8) {
            uintptr_t pointer = 0;
            memcpy(&pointer, modelBlock.data() + offset, sizeof(pointer));
            AddCandidate(roots, pointer);
        }

        for (uintptr_t root : roots) {
            std::array<unsigned char, 0x800> block{};
            if (!ReadBytes(root, block.data(), block.size())) continue;
            for (size_t offset = 0; offset + 0x18 <= block.size(); offset += 8) {
                uintptr_t setData = 0;
                uint32_t setCount = 0;
                memcpy(&setData, block.data() + offset, sizeof(setData));
                memcpy(&setCount, block.data() + offset + 8, sizeof(setCount));
                if (!IsValidPtr(setData) || setCount == 0 || setCount > 16) continue;

                std::array<unsigned char, 0x40> set{};
                if (!ReadBytes(setData, set.data(), set.size())) continue;
                uintptr_t hitboxData = 0;
                uint32_t hitboxCount = 0;
                memcpy(&hitboxData, set.data() + 0x10, sizeof(hitboxData));
                memcpy(&hitboxCount, set.data() + 0x18, sizeof(hitboxCount));
                if (!IsValidPtr(hitboxData) || hitboxCount == 0 || hitboxCount > 128) continue;

                std::array<unsigned char, 0x50> hitbox{};
                if (!ReadBytes(hitboxData, hitbox.data(), hitbox.size())) continue;
                uintptr_t boneNamePointer = 0;
                memcpy(&boneNamePointer, hitbox.data() + 0x10, sizeof(boneNamePointer));
                std::string boneName;
                if (!ReadString(boneNamePointer, boneName) || boneName.size() > 80) continue;

                float values[7]{};
                memcpy(values, hitbox.data() + 0x18, sizeof(values));
                bool boundsValid = true;
                for (float value : values) boundsValid = boundsValid && IsFiniteBound(value);
                int32_t group = 0;
                memcpy(&group, hitbox.data() + 0x38, sizeof(group));
                if (!boundsValid || values[6] < -1.0f || values[6] > 128.0f ||
                    group < 0 || group > 31) continue;

                rootOut = root;
                offsetOut = static_cast<int>(offset);
                setCountOut = static_cast<int>(setCount);
                hitboxCountOut = static_cast<int>(hitboxCount);
                firstBoneOut = std::move(boneName);
                return true;
            }
        }
        return false;
    }

    bool FindModelData(uintptr_t handle, uintptr_t& modelData, std::string& resourceName,
        int& validCandidates) {
        modelData = 0;
        resourceName.clear();
        validCandidates = 0;
        if (!IsValidPtr(handle)) return false;

        // CStrongHandle points to a resource binding. The first binding field
        // is the loaded CModel, which stores its resource name at +0x8.
        const uintptr_t boundModel = mem.Read<uintptr_t>(handle);
        if (IsValidPtr(boundModel)) {
            const uintptr_t boundNamePointer = mem.Read<uintptr_t>(boundModel + 0x8);
            std::string boundName;
            if (ReadString(boundNamePointer, boundName) && IsModelResourceName(boundName)) {
                modelData = boundModel;
                resourceName = std::move(boundName);
                validCandidates = 1;
                return true;
            }
        }

        std::vector<uintptr_t> firstLevel;
        firstLevel.reserve(40);
        AddCandidate(firstLevel, handle);
        std::array<uintptr_t, 24> pointers{};
        if (ReadBytes(handle, pointers.data(), sizeof(pointers))) {
            for (uintptr_t pointer : pointers) AddCandidate(firstLevel, pointer);
        }

        std::vector<uintptr_t> candidates = firstLevel;
        candidates.reserve(256);
        for (uintptr_t first : firstLevel) {
            std::array<uintptr_t, 16> nested{};
            if (!ReadBytes(first, nested.data(), sizeof(nested))) continue;
            for (uintptr_t pointer : nested) AddCandidate(candidates, pointer);
        }

        for (uintptr_t candidate : candidates) {
            std::array<unsigned char, 16> probe{};
            if (!ReadBytes(candidate, probe.data(), probe.size())) continue;
            ++validCandidates;

            uintptr_t namePointer = 0;
            memcpy(&namePointer, probe.data(), sizeof(namePointer));
            std::string name;
            if (ReadString(namePointer, name) && IsModelResourceName(name)) {
                modelData = candidate;
                resourceName = std::move(name);
                return true;
            }
            if (ReadString(candidate, name) && IsModelResourceName(name)) {
                modelData = candidate;
                resourceName = std::move(name);
                return true;
            }
        }
        return false;
    }
}

void SetModelDiagnosticsEnabled(bool enabled) {
    g_diagnostics.enabled = enabled;
    g_lastSampleMs = 0;
}

void ResetModelDiagnostics() {
    const bool enabled = g_diagnostics.enabled;
    g_diagnostics = {};
    g_diagnostics.enabled = enabled;
    g_lastSampleMs = 0;
    g_lastHitboxScanMs = 0;
    g_lastScannedModel = 0;
}

void UpdateModelDiagnostics() {
    if (!g_diagnostics.enabled || !mem.hProcess) return;
    const ULONGLONG now = GetTickCount64();
    if (g_lastSampleMs != 0 && now - g_lastSampleMs < 750) return;
    g_lastSampleMs = now;

    const FrameSnapshot& frame = GetCurrentFrameSnapshot();
    const EntitySnapshot* selected = nullptr;
    EntitySnapshot localFallback{};
    for (const EntitySnapshot& entity : frame.entities) {
        if (entity.lifeState == 0 && entity.health > 0 && entity.health <= 100 &&
            IsValidPtr(entity.sceneNode)) {
            selected = &entity;
            break;
        }
    }
    if (!selected && IsValidPtr(frame.localPawn)) {
        localFallback.pawn = frame.localPawn;
        localFallback.sceneNode = mem.Read<uintptr_t>(
            frame.localPawn + Offsets::m_pGameSceneNode);
        if (IsValidPtr(localFallback.sceneNode)) selected = &localFallback;
    }
    if (!selected) {
        ++g_diagnostics.readFailures;
        return;
    }

    g_diagnostics.pawn = selected->pawn;
    g_diagnostics.sceneNode = selected->sceneNode;
    g_diagnostics.hitboxSet = static_cast<int>(
        mem.Read<uint8_t>(selected->sceneNode + Offsets::m_nHitboxSet));
    const uintptr_t modelState = selected->sceneNode + Offsets::m_modelState;
    g_diagnostics.modelHandle = mem.Read<uintptr_t>(modelState + Offsets::m_hModel);

    const uintptr_t modelNamePointer = mem.Read<uintptr_t>(modelState + Offsets::m_ModelName);
    if (!ReadString(modelNamePointer, g_diagnostics.modelName))
        g_diagnostics.modelName.clear();

    if (!FindModelData(g_diagnostics.modelHandle, g_diagnostics.modelData,
        g_diagnostics.resourceName, g_diagnostics.validCandidates)) {
        g_diagnostics.modelData = 0;
        g_diagnostics.resourceName.clear();
    }
    if (IsValidPtr(g_diagnostics.modelData)) {
        // CModel adds a vtable before PermModelData_t. CUtlVector stores its
        // 32-bit element count at +0x10 from each schema vector field.
        g_diagnostics.externalPartCount = mem.Read<int>(g_diagnostics.modelData + 0x78);
        g_diagnostics.modelBoneCount = mem.Read<int>(g_diagnostics.modelData + 0x1A0);
        if (g_diagnostics.externalPartCount < 0 || g_diagnostics.externalPartCount > 64)
            g_diagnostics.externalPartCount = 0;
        if (g_diagnostics.modelBoneCount < 0 || g_diagnostics.modelBoneCount > 512)
            g_diagnostics.modelBoneCount = 0;
        const bool modelChanged = g_lastScannedModel != g_diagnostics.modelData;
        const bool retryMissing = g_diagnostics.modelHitboxCount == 0 &&
            (g_lastHitboxScanMs == 0 || now - g_lastHitboxScanMs >= 5000);
        if (modelChanged || retryMissing) {
            FindHitboxSetList(g_diagnostics.modelData, g_diagnostics.hitboxListRoot,
                g_diagnostics.hitboxListOffset, g_diagnostics.modelHitboxSetCount,
                g_diagnostics.modelHitboxCount, g_diagnostics.firstHitboxBone);
            g_lastScannedModel = g_diagnostics.modelData;
            g_lastHitboxScanMs = now;
        }
    } else {
        g_diagnostics.externalPartCount = 0;
        g_diagnostics.modelBoneCount = 0;
        g_diagnostics.hitboxListRoot = 0;
        g_diagnostics.hitboxListOffset = -1;
        g_diagnostics.modelHitboxSetCount = 0;
        g_diagnostics.modelHitboxCount = 0;
        g_diagnostics.firstHitboxBone.clear();
    }

    const uintptr_t boneArray = mem.Read<uintptr_t>(
        selected->sceneNode + Offsets::m_pBoneArray);
    std::array<float, 4> rotation{};
    g_diagnostics.boneRotationValid = ReadBytes(boneArray + 0x10,
        rotation.data(), sizeof(rotation));
    if (g_diagnostics.boneRotationValid) {
        float normSquared = 0.0f;
        for (float value : rotation) normSquared += value * value;
        g_diagnostics.boneRotationNorm = std::sqrt(normSquared);
        g_diagnostics.boneRotationValid = std::isfinite(g_diagnostics.boneRotationNorm) &&
            g_diagnostics.boneRotationNorm > 0.75f && g_diagnostics.boneRotationNorm < 1.25f;
    } else {
        g_diagnostics.boneRotationNorm = 0.0f;
    }
    ++g_diagnostics.samples;
}

const ModelDiagnostics& GetModelDiagnostics() {
    return g_diagnostics;
}
