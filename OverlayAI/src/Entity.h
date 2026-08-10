#pragma once

#include "Types.h"
#include <cstdint>
#include <cstdarg>
#include <vector>

extern uintptr_t g_entityStride;

uintptr_t GetEntityListBase();
uintptr_t GetEntityByIndexAuto(uintptr_t entityList, int index);
uintptr_t DetectEntityStride(uintptr_t entityList, uintptr_t localController, uintptr_t localPawn);
Vector3 GetPawnWorldPos(uintptr_t pawn);
bool IsPawnAlive(uintptr_t pawn);
void ReadPlayerName(uintptr_t controller, char* out, size_t outSize);
bool IsPawnVisibleToLocal(uintptr_t pawn, int localPlayerIndex);
void ResolveActiveLocal(uintptr_t entityList, uintptr_t stride,
    uintptr_t& outController, uintptr_t& outPawn, int& outTeam, int& outPlayerIndex);
void GetPlayerArmorInfo(uintptr_t pawn, int& armorOut, bool& hasHelmetOut);
int GetActiveWeaponDefIndex(uintptr_t pawn, uintptr_t entityList);
uintptr_t GetActiveWeaponEntity(uintptr_t pawn, uintptr_t entityList);

struct EntitySnapshot {
    uintptr_t pawn = 0;
    uint32_t pawnHandle = 0;
    uintptr_t controller = 0;
    uintptr_t sceneNode = 0;
    int health = 0;
    uint8_t team = 0;
    uint8_t lifeState = 0;
    Vector3 worldPos{};
    int armor = 0;
    bool hasHelmet = false;
    bool hasDefuser = false;
    uintptr_t weaponServices = 0;
    int index = -1; // entity list index
    // visibility snapshot
    bool spottedFlag = false;
    uint32_t spottedMask0 = 0;
    uint32_t spottedMask1 = 0;
};

// Frame snapshot holds per-entity data for a single frame.
struct FrameSnapshot {
    // compact vector of snapshots
    std::vector<EntitySnapshot> entities;
    Matrix4x4 viewMatrix;
    uintptr_t localPawn = 0;
    uintptr_t localController = 0;
    int localTeam = 0;
    int localPlayerIndex = -1;
    int localScopeLevel = 0;
};

// Produce a full frame snapshot: read view matrix, resolve local, then read entity snapshots for scanned players.
bool BuildFrameSnapshot(FrameSnapshot& out, int maxPlayers = 64);

// Refresh global double-buffered snapshot (atomic swap). Returns true on success.
bool RefreshGlobalSnapshot();

// Get reference to current active snapshot (thread-safe read).
const FrameSnapshot& GetCurrentFrameSnapshot();

bool ReadEntitySnapshot(uintptr_t controller, uintptr_t pawn, EntitySnapshot& out);

// Snapshot-based visibility query
bool IsPawnVisibleInSnapshot(const EntitySnapshot& snap, int localPlayerIndex);
