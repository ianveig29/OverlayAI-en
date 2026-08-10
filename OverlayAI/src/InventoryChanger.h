#pragma once

// ============================================================
// InventoryChanger.h
// Inventory changer function declarations.
// ============================================================

#include "Types.h"

#include <cstdint>

enum class InventoryRuntimeBackend {
    Disabled,
    ExternalOverlay,
    InjectedBridge
};

struct InventoryChangerStatus {
    uintptr_t localController = 0;
    uintptr_t inventoryServices = 0;
    int selectedDefinitionIndex = 0;
    int currentControllerMusicId = 0;
    int currentControllerMusicMvps = 0;
    uint16_t currentServiceMusicId = 0;
    bool selectionValid = false;
    bool externallyApplicable = false;
    bool applied = false;
    bool bridgeActive = false;
    InventoryRuntimeBackend backend = InventoryRuntimeBackend::Disabled;
};

void UpdateInventoryChanger();
void ShutdownInventoryChanger();
void RequestInventoryChangerRefresh();
void RequestMusicKitReapply();
void SetInventoryBridgeActive(bool active);
bool IsInventoryBridgeActive();
uint64_t GetMusicKitApplyRevision();
const InventoryChangerStatus& GetInventoryChangerStatus();
int AddLocalInventoryItem(int type, int definitionIndex, const char* displayName);
int AddLocalInventoryItem(const LocalInventoryItem& candidate);
bool UpdateLocalInventoryItem(int slot, const LocalInventoryItem& candidate);
void RemoveLocalInventoryItem(int slot);
void ClearLocalInventoryItems();
int CountLocalInventoryItems();
void SynchronizeLocalInventoryCatalog();
bool SelectLocalInventoryItemById(LocalItemId localId);
bool QueueLocalInventoryRevealById(LocalItemId localId);
bool EquipLocalMusicKitById(LocalItemId localId);
void UnequipLocalMusicKitSelection();
bool EquipLocalInventoryItemById(LocalItemId localId, int team);
bool UnequipLocalInventoryItemSelection(int itemType, int team);
bool IsLocalInventoryItemEquippedById(LocalItemId localId);
int GetSelectedLocalInventorySlot();
