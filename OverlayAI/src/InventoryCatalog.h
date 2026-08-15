#pragma once

#include "Types.h"

#include <cstddef>
#include <cstdint>

struct InventoryCatalogItem {
    int type = LocalInventoryMusicKit;
    int definitionIndex = 0;
    int paintIndex = 0;
    float minWear = 0.0f;
    float maxWear = 1.0f;
    bool statTrakAllowed = false;
    bool souvenirAllowed = false;
    uint8_t quality = 4;
    uint32_t rarityColor = 0xB0B0B0;
    const char* name = "";
    const char* group = "";
    const char* rarity = "";
    const char* imageUrl = "";
    bool legacyModel = false;
};

const InventoryCatalogItem* GetInventoryCatalog();
std::size_t GetInventoryCatalogSize();
const InventoryCatalogItem* GetInventoryCatalogItem(std::size_t index);
const InventoryCatalogItem* FindInventoryCatalogItem(
    int type, int definitionIndex, int paintIndex = 0);
bool IsInventoryCatalogItemStatTrakAllowed(
    const InventoryCatalogItem& item);
const char* GetLocalInventoryItemTypeName(int type);
bool IsInventoryItemExternallyApplicable(int type);
bool IsInventoryItemLoadoutSupported(int type);
bool IsInventoryItemNativeCollectionSupported(int type);
bool IsInventoryCatalogItemWearCustomizable(
    const InventoryCatalogItem& item);
const char* GetInventoryCatalogItemVariantName(
    const InventoryCatalogItem& item);
int GetInventoryCatalogItemTeam(const InventoryCatalogItem& item);
bool CanInventoryCatalogItemEquipForTeam(
    const InventoryCatalogItem& item, int team);
bool InventoryCatalogTextMatches(const InventoryCatalogItem& item, const char* query);
int GetInventoryRarityRank(const char* rarity);
