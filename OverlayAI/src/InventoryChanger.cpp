#include "InventoryChanger.h"

#include "Config.h"
#include "InventoryCatalog.h"
#include "InventoryStore.h"
#include "InventoryValidator.h"
#include "Memory.h"
#include "Offsets.h"
#include "Stats.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
    constexpr ULONGLONG kUpdateIntervalMs = 250;
    constexpr unsigned kRequiredStableContextSamples = 4;

    InventoryChangerStatus g_status;
    ULONGLONG g_lastUpdateMs = 0;
    uintptr_t g_originalController = 0;
    uintptr_t g_originalInventoryServices = 0;
    uint16_t g_originalServiceMusicId = 0;
    int g_originalControllerMusicId = 0;
    int g_originalControllerMusicMvps = 0;
    bool g_hasOriginalState = false;
    uintptr_t g_candidateController = 0;
    uintptr_t g_candidateInventoryServices = 0;
    unsigned g_stableContextSamples = 0;
    uintptr_t g_lastAppliedController = 0;
    uintptr_t g_lastAppliedInventoryServices = 0;
    int g_lastAppliedDefinitionIndex = 0;
    int g_lastAppliedMusicMvps = 0;
    bool g_applyRequested = true;
    bool g_bridgeActive = false;
    uint64_t g_musicKitApplyRevision = 1;

    template <typename T>
    bool WriteIfDifferent(uintptr_t address, const T& value) {
        if (!IsValidPtr(address)) return false;
        if (mem.Read<T>(address) == value) return true;
        if (!mem.Write<T>(address, value)) return false;
        Stats::rpmWriteCount.fetch_add(1);
        return true;
    }

    const LocalInventoryItem* GetSelectedItem() {
        const LocalInventoryItem* item = FindLocalInventoryItemById(
            g_InventoryChanger, g_InventoryChanger.loadout.musicKit);
        return item && ValidateLocalInventoryItem(*item) ? item : nullptr;
    }

    uintptr_t GetAccountLocalController() {
        if (!mem.hProcess || !IsValidPtr(mem.clientModule) ||
            Offsets::dwLocalPlayerController == 0)
            return 0;
        return mem.Read<uintptr_t>(
            mem.clientModule + Offsets::dwLocalPlayerController);
    }

    void ClearOriginalState() {
        g_originalController = 0;
        g_originalInventoryServices = 0;
        g_originalServiceMusicId = 0;
        g_originalControllerMusicId = 0;
        g_originalControllerMusicMvps = 0;
        g_hasOriginalState = false;
    }

    void ResetContextTracking() {
        g_candidateController = 0;
        g_candidateInventoryServices = 0;
        g_stableContextSamples = 0;
        g_lastAppliedController = 0;
        g_lastAppliedInventoryServices = 0;
        g_lastAppliedDefinitionIndex = 0;
        g_lastAppliedMusicMvps = 0;
    }

    void RestoreOriginalState() {
        if (!g_hasOriginalState || !mem.hProcess) {
            ClearOriginalState();
            return;
        }

        const uintptr_t currentController = GetAccountLocalController();
        const uintptr_t currentInventoryServices = IsValidPtr(currentController)
            ? mem.Read<uintptr_t>(
                currentController + Offsets::m_pInventoryServices)
            : 0;
        const bool sameLiveContext = currentController == g_originalController &&
            currentInventoryServices == g_originalInventoryServices &&
            IsValidPtr(currentController) && IsValidPtr(currentInventoryServices);
        if (sameLiveContext) {
            (void)WriteIfDifferent<int>(
                g_originalController + Offsets::m_iMusicKitID,
                g_originalControllerMusicId);
            (void)WriteIfDifferent<int>(
                g_originalController + Offsets::m_iMusicKitMVPs,
                g_originalControllerMusicMvps);
            (void)WriteIfDifferent<uint16_t>(
                g_originalInventoryServices + Offsets::m_unMusicID,
                g_originalServiceMusicId);
        }
        ClearOriginalState();
    }
}

void UpdateInventoryChanger() {
    const LocalInventoryItem* selected = GetSelectedItem();
    if (g_bridgeActive) {
        g_status = {};
        g_status.selectionValid = selected != nullptr;
        g_status.externallyApplicable = selected &&
            IsInventoryItemExternallyApplicable(selected->type);
        g_status.selectedDefinitionIndex = selected
            ? selected->definitionIndex : 0;
        g_status.bridgeActive = true;
        g_status.backend = g_InventoryChanger.enabled
            ? InventoryRuntimeBackend::InjectedBridge
            : InventoryRuntimeBackend::Disabled;
        return;
    }

    if (!g_InventoryChanger.enabled || !selected ||
        selected->type != LocalInventoryMusicKit ||
        selected->definitionIndex <= 0 || selected->definitionIndex > 65535 ||
        !mem.hProcess) {
        g_status = {};
        g_status.selectionValid = selected != nullptr;
        g_status.externallyApplicable = selected &&
            IsInventoryItemExternallyApplicable(selected->type);
        g_status.backend = InventoryRuntimeBackend::Disabled;
        RestoreOriginalState();
        ResetContextTracking();
        g_applyRequested = true;
        return;
    }

    const ULONGLONG nowMs = GetTickCount64();
    if (g_lastUpdateMs != 0 && nowMs - g_lastUpdateMs < kUpdateIntervalMs &&
        g_status.selectedDefinitionIndex == selected->definitionIndex)
        return;
    g_lastUpdateMs = nowMs;

    g_status = {};
    g_status.selectionValid = true;
    g_status.externallyApplicable = true;
    g_status.backend = InventoryRuntimeBackend::ExternalOverlay;
    const uintptr_t localController = GetAccountLocalController();
    g_status.localController = localController;
    g_status.selectedDefinitionIndex = selected->definitionIndex;
    if (!IsValidPtr(localController)) return;

    const uintptr_t inventoryServices = mem.Read<uintptr_t>(
        localController + Offsets::m_pInventoryServices);
    g_status.inventoryServices = inventoryServices;
    if (!IsValidPtr(inventoryServices)) return;

    if (g_candidateController != localController ||
        g_candidateInventoryServices != inventoryServices) {
        g_candidateController = localController;
        g_candidateInventoryServices = inventoryServices;
        g_stableContextSamples = 1;
        g_applyRequested = true;
        return;
    }
    if (g_stableContextSamples < kRequiredStableContextSamples) {
        ++g_stableContextSamples;
        return;
    }

    if (!g_hasOriginalState || g_originalController != localController ||
        g_originalInventoryServices != inventoryServices) {
        // A changed entity context makes the old addresses unsafe to restore.
        ClearOriginalState();
        g_originalController = localController;
        g_originalInventoryServices = inventoryServices;
        g_originalControllerMusicId = mem.Read<int>(
            localController + Offsets::m_iMusicKitID);
        g_originalControllerMusicMvps = mem.Read<int>(
            localController + Offsets::m_iMusicKitMVPs);
        g_originalServiceMusicId = mem.Read<uint16_t>(
            inventoryServices + Offsets::m_unMusicID);
        g_hasOriginalState = true;
    }

    const uint16_t musicId = static_cast<uint16_t>(selected->definitionIndex);
    int musicMvps = selected->statTrak
        ? (std::max)(0, selected->statTrakCount) : 0;
    const int observedMusicMvps = mem.Read<int>(
        localController + Offsets::m_iMusicKitMVPs);
    const bool sameAppliedSelection = selected->statTrak &&
        g_lastAppliedController == localController &&
        g_lastAppliedInventoryServices == inventoryServices &&
        g_lastAppliedDefinitionIndex == selected->definitionIndex;
    if (sameAppliedSelection && observedMusicMvps > musicMvps) {
        const int slot = FindLocalInventorySlotById(
            g_InventoryChanger, selected->localId);
        if (slot >= 0) {
            LocalInventoryItem updated = *selected;
            updated.statTrakCount = observedMusicMvps;
            if (UpdateLocalInventoryItem(slot, updated))
                musicMvps = observedMusicMvps;
        }
    }
    const bool newApplication = g_lastAppliedController != localController ||
        g_lastAppliedInventoryServices != inventoryServices ||
        g_lastAppliedDefinitionIndex != selected->definitionIndex ||
        g_lastAppliedMusicMvps != musicMvps;
    const bool runtimeDrift =
        mem.Read<uint16_t>(inventoryServices + Offsets::m_unMusicID) != musicId ||
        mem.Read<int>(localController + Offsets::m_iMusicKitID) !=
            selected->definitionIndex ||
        mem.Read<int>(localController + Offsets::m_iMusicKitMVPs) != musicMvps;
    if (g_applyRequested || newApplication || runtimeDrift) {
        (void)WriteIfDifferent<uint16_t>(
            inventoryServices + Offsets::m_unMusicID, musicId);
        (void)WriteIfDifferent<int>(
            localController + Offsets::m_iMusicKitID,
            selected->definitionIndex);
        (void)WriteIfDifferent<int>(
            localController + Offsets::m_iMusicKitMVPs,
            musicMvps);
        g_lastAppliedController = localController;
        g_lastAppliedInventoryServices = inventoryServices;
        g_lastAppliedDefinitionIndex = selected->definitionIndex;
        g_lastAppliedMusicMvps = musicMvps;
        g_applyRequested = false;
    }

    g_status.currentServiceMusicId = mem.Read<uint16_t>(
        inventoryServices + Offsets::m_unMusicID);
    g_status.currentControllerMusicId = mem.Read<int>(
        localController + Offsets::m_iMusicKitID);
    g_status.currentControllerMusicMvps = mem.Read<int>(
        localController + Offsets::m_iMusicKitMVPs);
    g_status.applied = g_status.currentServiceMusicId == musicId &&
        g_status.currentControllerMusicId == selected->definitionIndex &&
        g_status.currentControllerMusicMvps == musicMvps;
}

void ShutdownInventoryChanger() {
    RestoreOriginalState();
    g_status = {};
    g_lastUpdateMs = 0;
    ResetContextTracking();
    g_applyRequested = true;
    g_bridgeActive = false;
}

void RequestInventoryChangerRefresh() {
    g_lastUpdateMs = 0;
    g_applyRequested = true;
}

void RequestMusicKitReapply() {
    ++g_musicKitApplyRevision;
    if (g_musicKitApplyRevision == 0) g_musicKitApplyRevision = 1;
    RequestInventoryChangerRefresh();
}

void SetInventoryBridgeActive(bool active) {
    if (g_bridgeActive == active) return;

    if (active) {
        // Complete the external backend handoff before the Bridge receives
        // its first authoritative snapshot.
        RestoreOriginalState();
        ResetContextTracking();
    }
    g_bridgeActive = active;
    g_lastUpdateMs = 0;
    g_applyRequested = true;
    g_status = {};
    g_status.bridgeActive = active;
    g_status.backend = active && g_InventoryChanger.enabled
        ? InventoryRuntimeBackend::InjectedBridge
        : InventoryRuntimeBackend::Disabled;
}

bool IsInventoryBridgeActive() {
    return g_bridgeActive;
}

uint64_t GetMusicKitApplyRevision() {
    return g_musicKitApplyRevision;
}

const InventoryChangerStatus& GetInventoryChangerStatus() {
    return g_status;
}

int AddLocalInventoryItem(int type, int definitionIndex, const char* displayName) {
    LocalInventoryItem item;
    item.occupied = true;
    item.type = type;
    item.definitionIndex = definitionIndex;
    const InventoryCatalogItem* catalogItem = FindInventoryCatalogItem(type, definitionIndex);
    if (catalogItem) {
        item.paintIndex = catalogItem->paintIndex;
        item.wear = std::clamp(0.15f, catalogItem->minWear, catalogItem->maxWear);
    }
    if (displayName && *displayName)
        strncpy_s(item.displayName, displayName, _TRUNCATE);
    return AddLocalInventoryItem(item);
}

int AddLocalInventoryItem(const LocalInventoryItem& candidate) {
    return AddLocalInventoryItemToStore(g_InventoryChanger, candidate, true);
}

bool UpdateLocalInventoryItem(int slot, const LocalInventoryItem& candidate) {
    if (slot < 0 || slot >= kMaxLocalInventoryItems ||
        !g_InventoryChanger.items[slot].occupied)
        return false;

    const LocalItemId localId = g_InventoryChanger.items[slot].localId;
    if (!UpdateLocalInventoryItemInStore(g_InventoryChanger, localId, candidate))
        return false;
    if (g_InventoryChanger.loadout.musicKit == localId)
        RequestInventoryChangerRefresh();
    return true;
}

void RemoveLocalInventoryItem(int slot) {
    if (slot < 0 || slot >= kMaxLocalInventoryItems) return;
    const LocalItemId localId = g_InventoryChanger.items[slot].localId;
    const bool wasEquipped = IsLocalInventoryItemEquipped(
        g_InventoryChanger, localId);
    if (!RemoveLocalInventoryItemFromStore(g_InventoryChanger, localId)) return;
    if (wasEquipped)
        RestoreOriginalState();
    RequestInventoryChangerRefresh();
}

void ClearLocalInventoryItems() {
    ClearLocalInventoryStore(g_InventoryChanger);
    RestoreOriginalState();
    RequestInventoryChangerRefresh();
}

int CountLocalInventoryItems() {
    return CountLocalInventoryItems(g_InventoryChanger);
}

void SynchronizeLocalInventoryCatalog() {
    RevalidateLocalInventoryState(g_InventoryChanger);
}

bool SelectLocalInventoryItemById(LocalItemId localId) {
    return SelectLocalInventoryItem(g_InventoryChanger, localId);
}

bool QueueLocalInventoryRevealById(LocalItemId localId) {
    return QueueLocalInventoryReveal(g_InventoryChanger, localId);
}

bool EquipLocalMusicKitById(LocalItemId localId) {
    if (!EquipLocalMusicKit(g_InventoryChanger, localId)) return false;
    RequestInventoryChangerRefresh();
    return true;
}

void UnequipLocalMusicKitSelection() {
    UnequipLocalMusicKit(g_InventoryChanger);
    RestoreOriginalState();
    RequestInventoryChangerRefresh();
}

bool EquipLocalInventoryItemById(LocalItemId localId, int team) {
    if (!EquipLocalInventoryItem(g_InventoryChanger, localId, team)) return false;
    RequestInventoryChangerRefresh();
    return true;
}

bool UnequipLocalInventoryItemSelection(int itemType, int team) {
    const bool changed = UnequipLocalInventoryItem(
        g_InventoryChanger, itemType, team);
    if (itemType == LocalInventoryMusicKit)
        RestoreOriginalState();
    RequestInventoryChangerRefresh();
    return changed;
}

bool UnequipLocalInventoryItemById(LocalItemId localId, int team) {
    const bool changed = ::UnequipLocalInventoryItemById(
        g_InventoryChanger, localId, team);
    RequestInventoryChangerRefresh();
    return changed;
}

bool IsLocalInventoryItemEquippedById(LocalItemId localId) {
    return IsLocalInventoryItemEquipped(g_InventoryChanger, localId);
}

int GetSelectedLocalInventorySlot() {
    return FindLocalInventorySlotById(
        g_InventoryChanger, g_InventoryChanger.selectedLocalId);
}
