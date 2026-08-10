#pragma once

#include <cstdint>

constexpr int kMaxLocalInventoryItems = 256;
constexpr uint32_t kLocalInventoryStorageVersion = 7;

using LocalItemId = uint64_t;
constexpr LocalItemId kInvalidLocalItemId = 0;

enum LocalInventoryItemType {
    LocalInventoryMusicKit = 0,
    LocalInventoryWeaponSkin = 1,
    LocalInventoryKnife = 2,
    LocalInventoryGloves = 3,
    LocalInventoryAgent = 4,
    LocalInventoryCollectible = 5,
    LocalInventoryContainer = 6,
    LocalInventoryKey = 7,
    LocalInventorySticker = 8,
    LocalInventoryItemTypeCount
};

enum LocalInventoryValidity {
    LocalInventoryValid = 0,
    LocalInventoryOutdated = 1,
    LocalInventoryIncompatible = 2,
    LocalInventoryIncomplete = 3
};

enum LocalInventoryTeam {
    LocalInventoryTeamNone = 0,
    LocalInventoryTeamTerrorist = 1,
    LocalInventoryTeamCounterTerrorist = 2,
    LocalInventoryTeamBoth = 3
};

struct LocalInventoryLoadout {
    LocalItemId musicKit = kInvalidLocalItemId;
    LocalItemId terroristKnife = kInvalidLocalItemId;
    LocalItemId counterTerroristKnife = kInvalidLocalItemId;
    LocalItemId terroristGloves = kInvalidLocalItemId;
    LocalItemId counterTerroristGloves = kInvalidLocalItemId;
    LocalItemId terroristAgent = kInvalidLocalItemId;
    LocalItemId counterTerroristAgent = kInvalidLocalItemId;
};

struct LocalInventoryItem {
    bool occupied = false;
    LocalItemId localId = kInvalidLocalItemId;
    int type = LocalInventoryMusicKit;
    int definitionIndex = 0;
    int paintIndex = 0;
    float wear = 0.15f;
    int seed = 0;
    bool statTrak = false;
    int statTrakCount = 0;
    bool souvenir = false;
    int equippedTeam = LocalInventoryTeamNone;
    int64_t acquiredAt = 0;
    int validity = LocalInventoryIncomplete;
    char customName[64]{};
    char displayName[80]{};
};

struct InventoryChangerSettings {
    uint32_t storageVersion = kLocalInventoryStorageVersion;
    LocalItemId nextLocalId = 1;
    LocalItemId selectedLocalId = kInvalidLocalItemId;
    // Compatibility alias for older configs/frontends. It always mirrors the
    // first entry in pendingRevealItemIds.
    LocalItemId pendingRevealItemId = kInvalidLocalItemId;
    LocalItemId pendingRevealItemIds[kMaxLocalInventoryItems]{};
    int pendingRevealItemCount = 0;
    bool queueRevealWhenUnavailable = true;
    bool applyKnivesToControlledBots = false;
    // Debug-only Panorama UI: orange badge, custom NEW ITEM modal and carousel.
    bool useDebugPanoramaUi = false;
    bool enabled = false;
    LocalInventoryLoadout loadout{};
    LocalInventoryItem items[kMaxLocalInventoryItems]{};

    // Temporary UI/migration caches. They are not persistent item identities.
    int selectedSlot = -1;
    int equippedSlot = -1;
};
