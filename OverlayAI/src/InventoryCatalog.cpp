// ============================================================
// InventoryCatalog.cpp
// Game item catalog. Contains the list of all skins, knives and items available.
// ============================================================

#include "InventoryCatalog.h"
#include "Localization.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
    constexpr InventoryCatalogItem kCatalog[] = {
#include "InventoryCatalogData.inc"
    };

    bool ContainsNoCase(const char* text, const char* query) {
        if (!query || !*query) return true;
        if (!text) return false;

        for (const char* start = text; *start; ++start) {
            const char* left = start;
            const char* right = query;
            while (*left && *right &&
                std::tolower(static_cast<unsigned char>(*left)) ==
                std::tolower(static_cast<unsigned char>(*right))) {
                ++left;
                ++right;
            }
            if (!*right) return true;
        }
        return false;
    }
}

const InventoryCatalogItem* GetInventoryCatalog() {
    return kCatalog;
}

std::size_t GetInventoryCatalogSize() {
    return sizeof(kCatalog) / sizeof(kCatalog[0]);
}

const InventoryCatalogItem* GetInventoryCatalogItem(std::size_t index) {
    return index < GetInventoryCatalogSize() ? &kCatalog[index] : nullptr;
}

const InventoryCatalogItem* FindInventoryCatalogItem(
    int type, int definitionIndex, int paintIndex) {
    for (const InventoryCatalogItem& item : kCatalog) {
        if (item.type == type && item.definitionIndex == definitionIndex &&
            item.paintIndex == paintIndex)
            return &item;
    }
    return nullptr;
}

bool IsInventoryCatalogItemStatTrakAllowed(
    const InventoryCatalogItem& item) {
    // Definitions 1 and 2 are Valve's base kits. Purchasable Music Kits have
    // a StatTrak variant that records competitive MVP awards.
    return item.statTrakAllowed ||
        (item.type == LocalInventoryMusicKit && item.definitionIndex > 2);
}

const char* GetLocalInventoryItemTypeName(int type) {
    switch (type) {
    case LocalInventoryMusicKit: return "Music Kit";
    case LocalInventoryWeaponSkin: return Localized("Arma", "Weapon");
    case LocalInventoryKnife: return Localized("Cuchillo", "Knife");
    case LocalInventoryGloves: return Localized("Guantes", "Gloves");
    case LocalInventoryAgent: return Localized("Agente", "Agent");
    case LocalInventoryCollectible:
        return Localized("Coleccionable", "Collectible");
    case LocalInventoryContainer:
        return Localized("Caja/Contenedor", "Case/Container");
    case LocalInventoryKey: return Localized("Llave", "Key");
    case LocalInventorySticker: return Localized("Sticker", "Sticker");
    default: return Localized("Desconocido", "Unknown");
    }
}

bool IsInventoryItemExternallyApplicable(int type) {
    return type == LocalInventoryMusicKit;
}

bool IsInventoryItemLoadoutSupported(int type) {
    return type == LocalInventoryMusicKit || type == LocalInventoryKnife ||
        type == LocalInventoryGloves || type == LocalInventoryAgent;
}

bool IsInventoryItemNativeCollectionSupported(int type) {
    return type == LocalInventoryMusicKit ||
        type == LocalInventoryWeaponSkin || type == LocalInventoryKnife ||
        type == LocalInventoryGloves || type == LocalInventoryCollectible ||
        type == LocalInventoryContainer || type == LocalInventoryKey ||
        type == LocalInventorySticker;
}

bool IsInventoryCatalogItemWearCustomizable(
    const InventoryCatalogItem& item) {
    return item.paintIndex > 0 &&
        (item.type == LocalInventoryWeaponSkin ||
            item.type == LocalInventoryKnife ||
            item.type == LocalInventoryGloves);
}

const char* GetInventoryCatalogItemVariantName(
    const InventoryCatalogItem& item) {
    if (item.paintIndex <= 0) return "";
    if (item.type == LocalInventorySticker)
        return Localized("Kit de sticker", "Sticker kit");
    return Localized("Paint kit", "Paint kit");
}

int GetInventoryCatalogItemTeam(const InventoryCatalogItem& item) {
    if (item.type != LocalInventoryAgent) return LocalInventoryTeamBoth;
    if (strcmp(item.group, "Terrorist") == 0)
        return LocalInventoryTeamTerrorist;
    if (strcmp(item.group, "Counter-Terrorist") == 0)
        return LocalInventoryTeamCounterTerrorist;
    return LocalInventoryTeamNone;
}

bool CanInventoryCatalogItemEquipForTeam(
    const InventoryCatalogItem& item, int team) {
    if (!IsInventoryItemLoadoutSupported(item.type)) return false;
    if (item.type == LocalInventoryMusicKit)
        return team == LocalInventoryTeamBoth;
    if (team != LocalInventoryTeamTerrorist &&
        team != LocalInventoryTeamCounterTerrorist &&
        team != LocalInventoryTeamBoth)
        return false;

    const int compatibleTeam = GetInventoryCatalogItemTeam(item);
    return compatibleTeam == LocalInventoryTeamBoth || compatibleTeam == team;
}

bool InventoryCatalogTextMatches(const InventoryCatalogItem& item, const char* query) {
    if (!query || !*query) return true;
    if (ContainsNoCase(item.name, query) || ContainsNoCase(item.group, query) ||
        ContainsNoCase(item.rarity, query) ||
        ContainsNoCase(GetLocalInventoryItemTypeName(item.type), query))
        return true;

    char numeric[48]{};
    sprintf_s(numeric, "%d %d", item.definitionIndex, item.paintIndex);
    return ContainsNoCase(numeric, query);
}

int GetInventoryRarityRank(const char* rarity) {
    if (!rarity) return 0;
    if (strcmp(rarity, "Default") == 0) return 0;
    if (strcmp(rarity, "Base Grade") == 0) return 0;
    if (strcmp(rarity, "Consumer Grade") == 0) return 1;
    if (strcmp(rarity, "Industrial Grade") == 0) return 2;
    if (strcmp(rarity, "Mil-Spec Grade") == 0) return 3;
    if (strcmp(rarity, "High Grade") == 0) return 3;
    if (strcmp(rarity, "Distinguished") == 0) return 3;
    if (strcmp(rarity, "Restricted") == 0) return 4;
    if (strcmp(rarity, "Remarkable") == 0) return 4;
    if (strcmp(rarity, "Exceptional") == 0) return 4;
    if (strcmp(rarity, "Classified") == 0) return 5;
    if (strcmp(rarity, "Exotic") == 0) return 5;
    if (strcmp(rarity, "Superior") == 0) return 5;
    if (strcmp(rarity, "Covert") == 0) return 6;
    if (strcmp(rarity, "Master") == 0) return 6;
    if (strcmp(rarity, "Extraordinary") == 0) return 6;
    if (strcmp(rarity, "Contraband") == 0) return 7;
    return 0;
}
