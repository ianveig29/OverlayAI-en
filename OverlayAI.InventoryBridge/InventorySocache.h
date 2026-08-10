#pragma once

#include <cstdint>

struct InventorySocacheDiagnostics {
    bool managerResolved = false;
    bool inventoryResolved = false;
    bool socacheResolved = false;
    bool itemTypeCacheResolved = false;
    bool econItemFactoryResolved = false;
    bool attributeSettersResolved = false;
    bool itemViewCopyResolved = false;
    bool itemViewClearResolved = false;
    bool clientEconAccessorCandidate = false;
    bool writeReady = false;
    uintptr_t manager = 0;
    uintptr_t inventory = 0;
    uintptr_t socache = 0;
    uintptr_t itemTypeCache = 0;
    uintptr_t clientEconAccessor = 0;
    int itemCount = 0;
};

struct InventorySocacheItemSpec {
    uint64_t localId = 0;
    int team = 0;
    int definitionIndex = 0;
    int musicKitId = 0;
    int paintKit = 0;
    int seed = 0;
    float wear = 0.15f;
    bool statTrak = false;
    int statTrakCount = 0;
    int statTrakType = 0;
    int variantAttributeDefinition = 0;
    uint32_t variantAttributeValue = 0;
    uint8_t quality = 3;
    uint8_t rarity = 6;
    int loadoutSlot = 41;
    uintptr_t itemViewItemIdOffset = 0;
    bool unacknowledged = false;
    bool equip = true;
};

struct InventorySocacheItemIdentity {
    uintptr_t object = 0;
    uintptr_t loadoutItemView = 0;
    uint64_t itemId = 0;
    uint32_t accountId = 0;
};

struct InventorySocacheLoadoutSelection {
    uint64_t itemId = 0;
    uint64_t localId = 0;
    bool generated = false;
};

bool InitializeInventorySocacheDiagnostics(void* clientInterface) noexcept;
InventorySocacheDiagnostics GetInventorySocacheDiagnostics() noexcept;
bool EnsureInventorySocacheItem(
    const InventorySocacheItemSpec& spec,
    InventorySocacheItemIdentity& identity) noexcept;
bool CopyInventorySocacheItemView(
    uintptr_t destination, uintptr_t source) noexcept;
bool ClearInventorySocacheItemView(uintptr_t itemView) noexcept;
void LogInventorySocacheItemViewDiagnostics(
    uintptr_t destination, uintptr_t source) noexcept;
void RemoveInventorySocacheItem(const char* reason) noexcept;
void RemoveInventorySocacheItemsForSlot(
    int loadoutSlot, const char* reason) noexcept;
void RestoreInventorySocacheLoadoutSlot(
    int loadoutSlot, const char* reason) noexcept;
void PruneInventorySocacheCollection(
    const uint64_t* localIds, int count, int loadoutSlot,
    const char* reason) noexcept;
bool ReadInventorySocacheLoadoutSelection(
    int team, int loadoutSlot, uintptr_t itemViewItemIdOffset,
    InventorySocacheLoadoutSelection& selection) noexcept;
bool ResolveInventorySocacheGeneratedItemId(
    uint64_t localId, uint64_t& itemId) noexcept;
bool ResolveInventorySocacheGeneratedLocalId(
    uint64_t itemId, uint64_t& localId) noexcept;
bool ReadInventorySocacheItemUnacknowledged(
    uint64_t localId, bool& unacknowledged) noexcept;
void ShutdownInventorySocache() noexcept;
