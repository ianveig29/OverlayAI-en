#pragma once

// ============================================================
// InventoryStore.h
// Inventory store function declarations.
// ============================================================

#include "InventoryTypes.h"

struct InventoryMigrationResult {
    int assignedLocalIds = 0;
    int invalidItems = 0;
    bool repairedNextLocalId = false;
};

int FindLocalInventorySlotById(const InventoryChangerSettings& state, LocalItemId localId);
LocalInventoryItem* FindLocalInventoryItemById(
    InventoryChangerSettings& state, LocalItemId localId);
const LocalInventoryItem* FindLocalInventoryItemById(
    const InventoryChangerSettings& state, LocalItemId localId);
int CountLocalInventoryItems(const InventoryChangerSettings& state);

int AddLocalInventoryItemToStore(
    InventoryChangerSettings& state, const LocalInventoryItem& candidate,
    bool markPendingReveal = true);
bool UpdateLocalInventoryItemInStore(
    InventoryChangerSettings& state, LocalItemId localId,
    const LocalInventoryItem& candidate);
bool RemoveLocalInventoryItemFromStore(
    InventoryChangerSettings& state, LocalItemId localId);
void ClearLocalInventoryStore(InventoryChangerSettings& state);

bool SelectLocalInventoryItem(InventoryChangerSettings& state, LocalItemId localId);
bool QueueLocalInventoryReveal(
    InventoryChangerSettings& state, LocalItemId localId);
bool IsLocalInventoryRevealPending(
    const InventoryChangerSettings& state, LocalItemId localId);
int CountPendingLocalInventoryReveals(
    const InventoryChangerSettings& state);
void ClearPendingLocalInventoryReveals(InventoryChangerSettings& state);
bool EquipLocalMusicKit(InventoryChangerSettings& state, LocalItemId localId);
void UnequipLocalMusicKit(InventoryChangerSettings& state);
bool EquipLocalInventoryItem(
    InventoryChangerSettings& state, LocalItemId localId, int team);
bool UnequipLocalInventoryItem(
    InventoryChangerSettings& state, int itemType, int team);
bool IsLocalInventoryItemEquipped(
    const InventoryChangerSettings& state, LocalItemId localId);

InventoryMigrationResult FinalizeLoadedInventoryState(
    InventoryChangerSettings& state, int legacySelectedSlot = -1,
    int legacyEquippedSlot = -1);
void RevalidateLocalInventoryState(InventoryChangerSettings& state);
