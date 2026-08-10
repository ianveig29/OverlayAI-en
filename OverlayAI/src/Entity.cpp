#include "Entity.h"
#include "WorldTransform.h"
#include <unordered_set>
#include "Memory.h"
#include "Stats.h"
#include "Offsets.h"
#include "Config.h"
#include <cmath>
#include <cstdarg>
#include "Types.h"
#include <Windows.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstring>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <array>
#include "Entity.h"


uintptr_t g_entityStride = 0x70;

static uintptr_t GetEntityByIndex(uintptr_t entityList, int index, uintptr_t stride) {
    if (!entityList || index <= 0 || index >= 0x8000 || stride == 0) return 0;
    uintptr_t chunk = mem.Read<uintptr_t>(entityList + 8 * ((index & 0x7FFF) >> 9) + 16);
    if (!IsValidPtr(chunk)) return 0;
    return mem.Read<uintptr_t>(chunk + stride * (index & 0x1FF));
}

namespace {
    struct EntityChunkSnapshot {
        std::vector<uint8_t> bytes;
        bool valid = false;
    };

    class FrameEntityTable {
    public:
        FrameEntityTable(uintptr_t entityList, uintptr_t stride)
            : entityList_(entityList), stride_(stride) {}

        uintptr_t Read(int index) {
            if (index <= 0 || index >= 0x8000 || stride_ == 0) return 0;
            const int chunkIndex = (index & 0x7FFF) >> 9;
            auto [it, inserted] = chunks_.try_emplace(chunkIndex);
            EntityChunkSnapshot& snapshot = it->second;
            if (inserted) LoadChunk(chunkIndex, snapshot);
            if (!snapshot.valid) return 0;

            const size_t offset = stride_ * static_cast<size_t>(index & 0x1FF);
            if (offset + sizeof(uintptr_t) > snapshot.bytes.size()) return 0;
            uintptr_t entity = 0;
            memcpy(&entity, snapshot.bytes.data() + offset, sizeof(entity));
            return entity;
        }

    private:
        void LoadChunk(int chunkIndex, EntityChunkSnapshot& snapshot) {
            const uintptr_t chunk = mem.Read<uintptr_t>(entityList_ + 8 * chunkIndex + 16);
            if (!IsValidPtr(chunk)) return;

            snapshot.bytes.resize(stride_ * 512);
            SIZE_T bytesRead = 0;
            snapshot.valid = ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(chunk),
                snapshot.bytes.data(), snapshot.bytes.size(), &bytesRead) &&
                bytesRead == snapshot.bytes.size();
            Stats::rpmReadCount.fetch_add(1);
            if (!snapshot.valid) snapshot.bytes.clear();
        }

        uintptr_t entityList_ = 0;
        uintptr_t stride_ = 0;
        std::unordered_map<int, EntityChunkSnapshot> chunks_;
    };
}

// Double-buffered frame snapshot storage
static FrameSnapshot g_snapBuffers[2];
static std::atomic<int> g_activeSnap = 0; // index of buffer renderer should read
static std::mutex g_buildMutex;

bool BuildFrameSnapshot(FrameSnapshot& out, int maxPlayers) {
    std::lock_guard<std::mutex> lk(g_buildMutex);
    out.entities.clear();

    // Read view matrix
    Matrix4x4 vm{};
    auto t_build_start = std::chrono::high_resolution_clock::now();
    if (!ReadViewMatrix(vm)) return false;
    out.viewMatrix = vm;

    uintptr_t entityList = GetEntityListBase();
    if (!IsValidPtr(entityList)) return false;

    out.localController = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerController);
    out.localPawn = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerPawn);
    out.localTeam = 0;
    out.localPlayerIndex = -1;
    if (IsValidPtr(out.localController))
        g_entityStride = DetectEntityStride(entityList, out.localController, out.localPawn);
    ResolveActiveLocal(entityList, g_entityStride, out.localController, out.localPawn, out.localTeam, out.localPlayerIndex);

    out.localScopeLevel = 0;
    if (IsValidPtr(out.localPawn) && mem.Read<bool>(out.localPawn + Offsets::m_bIsScoped)) {
        const uintptr_t activeWeapon = GetActiveWeaponEntity(out.localPawn, entityList);
        const int zoomLevel = IsValidPtr(activeWeapon)
            ? mem.Read<int>(activeWeapon + Offsets::m_zoomLevel)
            : 0;
        out.localScopeLevel = (zoomLevel >= 1 && zoomLevel <= 2) ? zoomLevel : 1;
    }

    // iterate controllers and build entity snapshots
    FrameEntityTable entityTable(entityList, g_entityStride);
    std::unordered_set<uintptr_t> seenControllers;
    std::unordered_set<uintptr_t> seenPawns;

    for (int i = 1; i <= maxPlayers; ++i) {
        uintptr_t controller = entityTable.Read(i);
        if (!IsValidPtr(controller)) continue;
        if (seenControllers.find(controller) != seenControllers.end()) continue;
        seenControllers.insert(controller);

        uint32_t pawnHandle = mem.Read<uint32_t>(controller + Offsets::m_hPlayerPawn);
        if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;

        uintptr_t pawn = entityTable.Read(pawnHandle & 0x7FFF);
        if (!IsValidPtr(pawn) || pawn == out.localPawn) continue;
        if (seenPawns.find(pawn) != seenPawns.end()) continue;
        seenPawns.insert(pawn);

        EntitySnapshot s;
        if (!ReadEntitySnapshot(controller, pawn, s)) continue;
        s.pawnHandle = pawnHandle;
        s.controller = controller;
        s.index = i;
        out.entities.push_back(s);
    }

    auto t_build_end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t_build_end - t_build_start).count();
    Stats::lastSnapshotBuildMs.store(ms);
    return true;
}

// call to atomically update global snapshot (build into inactive buffer then swap)
bool RefreshGlobalSnapshot() {
    int inactive = (g_activeSnap.load() ^ 1);
    FrameSnapshot tmp;
    if (!BuildFrameSnapshot(tmp)) return false;
    g_snapBuffers[inactive] = std::move(tmp);
    g_activeSnap.store(inactive);
    return true;
}

// Get current active snapshot for reading (no lock; snapshot is replaced atomically)
const FrameSnapshot& GetCurrentFrameSnapshot() {
    return g_snapBuffers[g_activeSnap.load()];
}

uintptr_t GetEntityListBase() {
    uintptr_t list = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwEntityList);
    if (IsValidPtr(list)) return list;
    return mem.clientModule + Offsets::dwEntityList;
}

uintptr_t GetEntityByIndexAuto(uintptr_t entityList, int index) {
    return GetEntityByIndex(entityList, index, g_entityStride);
}

uintptr_t DetectEntityStride(uintptr_t entityList, uintptr_t localController, uintptr_t localPawn) {
    (void)localPawn;
    static uintptr_t cachedEntityList = 0;
    static uintptr_t cachedController = 0;
    static uintptr_t cachedStride = 0;
    static int cachedControllerIndex = -1;

    if (!IsValidPtr(localController)) return g_entityStride;

    if (entityList == cachedEntityList && localController == cachedController &&
        cachedControllerIndex > 0 && GetEntityByIndex(entityList, cachedControllerIndex, cachedStride) == localController)
        return cachedStride;

    // Controller slots remain available while dead or spectating, so this is a
    // more reliable discriminator than requiring the current pawn to resolve.
    const uintptr_t alternativeStride = (g_entityStride == 0x70) ? 0x78 : 0x70;
    const uintptr_t strides[] = { g_entityStride, alternativeStride };
    for (uintptr_t stride : strides) {
        for (int index = 1; index <= 64; ++index) {
            if (GetEntityByIndex(entityList, index, stride) == localController) {
                cachedEntityList = entityList;
                cachedController = localController;
                cachedStride = stride;
                cachedControllerIndex = index;
                return stride;
            }
        }
    }

    // Preserve the last verified stride if the controller list is temporarily
    // unavailable during a map or round transition.
    return (g_entityStride == 0x70 || g_entityStride == 0x78) ? g_entityStride : 0x70;
}

Vector3 GetPawnWorldPos(uintptr_t pawn) {
    uintptr_t sceneNode = mem.Read<uintptr_t>(pawn + Offsets::m_pGameSceneNode);
    if (IsValidPtr(sceneNode)) {
        Vector3 pos = mem.Read<Vector3>(sceneNode + Offsets::m_vecAbsOrigin);
        if (std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z)) {
            if (fabs(pos.x) > 1.0f || fabs(pos.y) > 1.0f || fabs(pos.z) > 1.0f)
                return pos;
        }
    }
    return mem.Read<Vector3>(pawn + Offsets::m_vOldOrigin);
}

bool IsPawnAlive(uintptr_t pawn) {
    return mem.Read<uint8_t>(pawn + Offsets::m_lifeState) == 0;
}

void ReadPlayerName(uintptr_t controller, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!IsValidPtr(controller)) return;

    struct CachedName {
        std::string value;
        ULONGLONG refreshMs = 0;
    };
    static std::unordered_map<uintptr_t, CachedName> nameCache;
    if (nameCache.size() > 256) nameCache.clear();

    const ULONGLONG nowMs = GetTickCount64();
    auto cachedIt = nameCache.find(controller);
    if (cachedIt != nameCache.end() && nowMs - cachedIt->second.refreshMs < 2000) {
        strncpy_s(out, outSize, cachedIt->second.value.c_str(), _TRUNCATE);
        return;
    }

    SIZE_T read = 0;

    // Try direct inline name first
    char buffer[128] = {};
    ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(controller + Offsets::m_iszPlayerName),
        buffer, sizeof(buffer) - 1, &read);
    if (buffer[0] != '\0') {
        strncpy_s(out, outSize, buffer, _TRUNCATE);
        nameCache[controller] = { std::string(buffer), nowMs };
        return;
    }

    uintptr_t namePtr = mem.Read<uintptr_t>(controller + Offsets::m_sSanitizedPlayerName);
    if (!IsValidPtr(namePtr)) return;

    ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(namePtr), buffer, sizeof(buffer) - 1, &read);
    if (buffer[0] != '\0') {
        nameCache[controller] = { std::string(buffer), nowMs };
        strncpy_s(out, outSize, buffer, _TRUNCATE);
    }
}

bool ReadEntitySnapshot(uintptr_t controller, uintptr_t pawn, EntitySnapshot& out) {
    if (!IsValidPtr(pawn)) return false;
    out = EntitySnapshot();
    out.pawn = pawn;

    // Read a block from pawn to minimize ReadProcessMemory syscalls
    // Current pawn fields reach beyond 0x1C9C. Reading one contiguous block is
    // substantially cheaper than issuing separate calls for every field.
    constexpr SIZE_T blockSize = 0x1D00;
    std::array<uint8_t, blockSize> buf{};
    SIZE_T bytesRead = 0;
    BOOL ok = ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(pawn), buf.data(), blockSize, &bytesRead);
    Stats::rpmReadCount.fetch_add(1);
    if (!ok || bytesRead == 0) return false;

    auto read_u32 = [&](size_t off)->uint32_t {
        if (off + 4 > buf.size()) return 0;
        uint32_t v; memcpy(&v, buf.data() + off, 4); return v;
    };

    auto read_u8 = [&](size_t off)->uint8_t {
        if (off + 1 > buf.size()) return 0; uint8_t v; memcpy(&v, buf.data() + off, 1); return v; };
    auto read_i32 = [&](size_t off)->int32_t { if (off + 4 > buf.size()) return 0; int32_t v; memcpy(&v, buf.data() + off, 4); return v; };
    auto read_ptr = [&](size_t off)->uintptr_t { if (off + sizeof(uintptr_t) > buf.size()) return 0; uintptr_t v; memcpy(&v, buf.data() + off, sizeof(uintptr_t)); return v; };

    // Extract common fields
    out.health = read_i32(Offsets::m_iHealth);
    out.team = read_u8(Offsets::m_iTeamNum);
    out.lifeState = read_u8(Offsets::m_lifeState);
    out.armor = read_i32(Offsets::m_ArmorValue);
    out.weaponServices = read_ptr(Offsets::m_pWeaponServices);
    uintptr_t itemServices = read_ptr(Offsets::m_pItemServices);
    out.hasDefuser = IsValidPtr(controller) &&
        mem.Read<bool>(controller + Offsets::m_bPawnHasDefuser);
    out.hasHelmet = false;
    if (IsValidPtr(itemServices)) {
        SIZE_T br = 0;
        if (Offsets::m_bHasHelmet == Offsets::m_bHasDefuser + 1) {
            std::array<uint8_t, 2> equipmentFlags{};
            ReadProcessMemory(mem.hProcess,
                reinterpret_cast<LPCVOID>(itemServices + Offsets::m_bHasDefuser),
                equipmentFlags.data(), equipmentFlags.size(), &br);
            if (br == equipmentFlags.size()) {
                out.hasDefuser = out.hasDefuser || equipmentFlags[0] != 0;
                out.hasHelmet = equipmentFlags[1] != 0;
            }
        } else {
            out.hasDefuser = mem.Read<bool>(itemServices + Offsets::m_bHasDefuser);
            out.hasHelmet = mem.Read<bool>(itemServices + Offsets::m_bHasHelmet);
        }
    }

    // Try to get world pos: read scene node pointer and if valid, read its vecAbsOrigin
    out.sceneNode = read_ptr(Offsets::m_pGameSceneNode);

    if (Offsets::m_vOldOrigin + sizeof(Vector3) <= buf.size())
        memcpy(&out.worldPos, buf.data() + Offsets::m_vOldOrigin, sizeof(Vector3));

    const bool validBlockOrigin = std::isfinite(out.worldPos.x) && std::isfinite(out.worldPos.y) &&
        std::isfinite(out.worldPos.z) && (std::fabs(out.worldPos.x) > 1.0f ||
            std::fabs(out.worldPos.y) > 1.0f || std::fabs(out.worldPos.z) > 1.0f);
    if (!validBlockOrigin && IsValidPtr(out.sceneNode)) {
        Vector3 v{};
        SIZE_T br = 0;
        if (ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(out.sceneNode + Offsets::m_vecAbsOrigin), &v, sizeof(v), &br) && br == sizeof(v)) {
            if (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)) {
                out.worldPos = v;
            }
        }
    }

    // Read spotted/visibility state if present inside the block (m_entitySpottedState)
    if (Offsets::m_entitySpottedState + Offsets::m_bSpottedByMask + 8 <= buf.size()) {
        uintptr_t spottedBase = Offsets::m_entitySpottedState;
        out.spottedFlag = (read_u8(spottedBase + Offsets::m_bSpotted) != 0);
        out.spottedMask0 = read_u32(spottedBase + Offsets::m_bSpottedByMask);
        out.spottedMask1 = read_u32(spottedBase + Offsets::m_bSpottedByMask + 4);
    } else {
        // fallback: try to read via safe RPM
        uintptr_t spottedBaseAddr = pawn + Offsets::m_entitySpottedState;
        uint8_t sf = 0; SIZE_T br = 0;
        ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(spottedBaseAddr + Offsets::m_bSpotted), &sf, 1, &br);
        if (br == 1) out.spottedFlag = (sf != 0);
        uint32_t m0 = 0, m1 = 0;
        ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(spottedBaseAddr + Offsets::m_bSpottedByMask), &m0, 4, &br);
        if (br == 4) out.spottedMask0 = m0;
        ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(spottedBaseAddr + Offsets::m_bSpottedByMask + 4), &m1, 4, &br);
        if (br == 4) out.spottedMask1 = m1;
    }

    return true;
}

bool IsPawnVisibleToLocal(uintptr_t pawn, int localPlayerIndex) {
    // fallback live-read visibility (kept for external callers that don't use snapshot)
    uintptr_t spottedBase = pawn + Offsets::m_entitySpottedState;
    bool spottedFlag = mem.Read<bool>(spottedBase + Offsets::m_bSpotted);

    uintptr_t maskBase = spottedBase + Offsets::m_bSpottedByMask;
    uint32_t mask0 = mem.Read<uint32_t>(maskBase);
    uint32_t mask1 = mem.Read<uint32_t>(maskBase + 4);

    if (localPlayerIndex > 0 && localPlayerIndex <= 64) {
        bool byMask = (localPlayerIndex <= 32)
            ? ((mask0 & (1u << (localPlayerIndex - 1))) != 0)
            : ((mask1 & (1u << (localPlayerIndex - 33))) != 0);
        return byMask || (!g_Esp.enableRadarHack && spottedFlag);
    }

    return (!g_Esp.enableRadarHack && spottedFlag) || mask0 != 0 || mask1 != 0;
}

// Snapshot-based visibility query: prefer using snapshot data when available
bool IsPawnVisibleInSnapshot(const EntitySnapshot& snap, int localPlayerIndex) {
    bool spottedFlag = snap.spottedFlag;
    uint32_t mask0 = snap.spottedMask0;
    uint32_t mask1 = snap.spottedMask1;
    if (localPlayerIndex > 0 && localPlayerIndex <= 64) {
        bool byMask = (localPlayerIndex <= 32)
            ? ((mask0 & (1u << (localPlayerIndex - 1))) != 0)
            : ((mask1 & (1u << (localPlayerIndex - 33))) != 0);
        return byMask || (!g_Esp.enableRadarHack && spottedFlag);
    }
    return (!g_Esp.enableRadarHack && spottedFlag) || mask0 != 0 || mask1 != 0;
}

static int FindLocalPlayerIndex(uintptr_t entityList, uintptr_t localController, uintptr_t stride) {
    for (int i = 1; i <= 64; ++i) {
        if (GetEntityByIndex(entityList, i, stride) == localController)
            return i;
    }
    return -1;
}

static uintptr_t FindControllerForPawn(uintptr_t entityList, uintptr_t pawn, uintptr_t stride) {
    if (!IsValidPtr(pawn)) return 0;
    for (int i = 1; i <= 64; ++i) {
        uintptr_t controller = GetEntityByIndex(entityList, i, stride);
        if (!IsValidPtr(controller)) continue;
        uint32_t pawnHandle = mem.Read<uint32_t>(controller + Offsets::m_hPlayerPawn);
        if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;
        uintptr_t entryPawn = GetEntityByIndex(entityList, pawnHandle & 0x7FFF, stride);
        if (entryPawn == pawn) return controller;
    }
    return 0;
}

static uintptr_t ResolveLocalPawn(uintptr_t entityList, uintptr_t localController, uintptr_t localPawn, uintptr_t stride) {
    if (IsValidPtr(localPawn)) return localPawn;
    if (!IsValidPtr(localController)) return 0;
    uint32_t pawnHandle = mem.Read<uint32_t>(localController + Offsets::m_hPlayerPawn);
    if (!pawnHandle || pawnHandle == 0xFFFFFFFF) return 0;
    return GetEntityByIndex(entityList, pawnHandle & 0x7FFF, stride);
}

static int ResolveLocalPlayerIndex(uintptr_t localController, uintptr_t localPawn) {
    if (IsValidPtr(localController)) {
        uint32_t pawnHandle = mem.Read<uint32_t>(localController + Offsets::m_hPlayerPawn);
        if (pawnHandle && pawnHandle != 0xFFFFFFFF)
            return pawnHandle & 0x7FFF;
    }
    return -1;
}

void ResolveActiveLocal(uintptr_t entityList, uintptr_t stride,
    uintptr_t& outController, uintptr_t& outPawn, int& outTeam, int& outPlayerIndex)
{
    outController = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerController);
    outPawn = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerPawn);
    outPawn = ResolveLocalPawn(entityList, outController, outPawn, stride);

    bool controllerOwnsActivePawn = false;
    if (IsValidPtr(outController) && IsValidPtr(outPawn)) {
        const uint32_t pawnHandle = mem.Read<uint32_t>(outController + Offsets::m_hPlayerPawn);
        if (pawnHandle && pawnHandle != 0xFFFFFFFF) {
            const uintptr_t controllerPawn = GetEntityByIndex(entityList, pawnHandle & 0x7FFF, stride);
            controllerOwnsActivePawn = (controllerPawn == outPawn);
        }
    }

    // The expensive controller search is only necessary after death, while
    // spectating, or when control moves to a bot.
    if (!controllerOwnsActivePawn) {
        const uintptr_t controllerForPawn = FindControllerForPawn(entityList, outPawn, stride);
        if (IsValidPtr(controllerForPawn))
            outController = controllerForPawn;
    }

    outTeam = 0;
    if (IsValidPtr(outPawn))
        outTeam = mem.Read<uint8_t>(outPawn + Offsets::m_iTeamNum);
    else if (IsValidPtr(outController))
        outTeam = mem.Read<uint8_t>(outController + Offsets::m_iTeamNum);

    outPlayerIndex = -1;
    if (IsValidPtr(outController))
        outPlayerIndex = FindLocalPlayerIndex(entityList, outController, stride);
    if (outPlayerIndex < 0)
        outPlayerIndex = ResolveLocalPlayerIndex(outController, outPawn);
}

void GetPlayerArmorInfo(uintptr_t pawn, int& armorOut, bool& hasHelmetOut) {
    armorOut = mem.Read<int>(pawn + Offsets::m_ArmorValue);
    hasHelmetOut = false;
    uintptr_t itemServices = mem.Read<uintptr_t>(pawn + Offsets::m_pItemServices);
    if (IsValidPtr(itemServices))
        hasHelmetOut = mem.Read<bool>(itemServices + Offsets::m_bHasHelmet);
}

uintptr_t GetActiveWeaponEntity(uintptr_t pawn, uintptr_t entityList) {
    uintptr_t weaponServices = mem.Read<uintptr_t>(pawn + Offsets::m_pWeaponServices);
    if (!IsValidPtr(weaponServices)) return 0;

    uint32_t weaponHandle = mem.Read<uint32_t>(weaponServices + Offsets::m_hActiveWeapon);
    if (!weaponHandle || weaponHandle == 0xFFFFFFFF) return 0;

    return GetEntityByIndexAuto(entityList, weaponHandle & 0x7FFF);
}

int GetActiveWeaponDefIndex(uintptr_t pawn, uintptr_t entityList) {
    uintptr_t weapon = GetActiveWeaponEntity(pawn, entityList);
    if (!IsValidPtr(weapon)) return 0;

    uintptr_t itemView = weapon + Offsets::m_AttributeManager + Offsets::m_Item;
    return mem.Read<uint16_t>(itemView + Offsets::m_iItemDefinitionIndex);
}
