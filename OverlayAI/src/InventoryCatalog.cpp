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

    // ---- Permissive search helpers ---------------------------------------
    //
    // BuildSearchableText: flattens a string for search purposes.
    //   1) Lowercases every letter.
    //   2) Strips accents (a-acute -> a, e-acute -> e, n-tilde -> n, etc.).
    //   3) Drops decorative symbols (TM sign, star, sparkle) and turns every
    //      other non letter/number character into a space (|, -, (, ), ', ...).
    //   4) Collapses repeated spaces and trims both ends.
    // Example: "ST StatTrak AK-47 | Redline" becomes "stattrak ak 47 redline".
    void BuildSearchableText(const char* text, char* out, std::size_t outSize) {
        std::size_t outPos = 0;
        bool lastWasSpace = true; // also trims leading spaces for free
        for (const unsigned char* p =
                reinterpret_cast<const unsigned char*>(text ? text : ""); *p; ) {
            const unsigned char c = *p;
            char decoded = 0;       // ASCII letter/digit after translation
            std::size_t consume = 1; // bytes consumed from the source

            if (c < 0x80) {
                if (std::isalnum(c)) decoded = static_cast<char>(std::tolower(c));
            } else if (c == 0xC3 && p[1] != 0) {
                // Two-byte Latin accents: a e i o u n (lower and upper case).
                const unsigned char low = p[1];
                if (low == 0xA1 || low == 0x81) decoded = 'a';
                else if (low == 0xA9 || low == 0x89) decoded = 'e';
                else if (low == 0xAD || low == 0x8D) decoded = 'i';
                else if (low == 0xB3 || low == 0x93) decoded = 'o';
                else if (low == 0xBA || low == 0x9A) decoded = 'u';
                else if (low == 0xB1 || low == 0x91) decoded = 'n';
                consume = 2;
            } else if (c == 0xE2 && p[1] == 0x84 && p[2] == 0xA2) {
                consume = 3; // (TM) -> nothing
            } else if (c == 0xE2 && p[1] == 0x98 && p[2] == 0x85) {
                consume = 3; // black star -> nothing
            } else if (c == 0xF0 && p[1] == 0x9F && p[2] == 0x8C && p[3] == 0x9F) {
                consume = 4; // sparkle/star emoji -> nothing
            } else {
                // Any other multi-byte sequence: treat as a separator.
                if ((c & 0xE0) == 0xC0) consume = 2;
                else if ((c & 0xF0) == 0xE0) consume = 3;
                else if ((c & 0xF8) == 0xF0) consume = 4;
            }

            if (decoded) {
                if (outPos + 1 < outSize) {
                    out[outPos++] = decoded;
                    lastWasSpace = false;
                }
            } else if (!lastWasSpace) {
                if (outPos + 1 < outSize) {
                    out[outPos++] = ' ';
                    lastWasSpace = true;
                }
            }
            p += consume;
        }
        while (outPos > 0 && out[outPos - 1] == ' ') --outPos; // trim trailing
        out[outPos] = '\0';
    }

    // CopyWithoutSpaces: compact copy used to match queries like "ak47"
    // against text written as "AK-47" (the separator became a space).
    void CopyWithoutSpaces(const char* src, char* dst, std::size_t dstSize) {
        std::size_t pos = 0;
        for (const char* p = src ? src : ""; *p && pos + 1 < dstSize; ++p)
            if (*p != ' ') dst[pos++] = *p;
        dst[pos] = '\0';
    }

    // SearchTextHasAllTokens: splits the query into words and requires that
    // EVERY word appears somewhere in the haystack (order does not matter).
    // "redline ak" finds "AK-47 | Redline", "stattrak doppler" finds the
    // StatTrak Doppler knives, and "ak47" still works because each word is
    // also tried against a spaces-removed copy of the haystack.
    bool SearchTextHasAllTokens(const char* haystack, const char* query) {
        if (!haystack) return false;
        if (!query || !*query) return true;

        char normHay[512]{};
        BuildSearchableText(haystack, normHay, sizeof(normHay));
        char normHayNoSpace[512]{};
        CopyWithoutSpaces(normHay, normHayNoSpace, sizeof(normHayNoSpace));

        char normQuery[160]{};
        BuildSearchableText(query, normQuery, sizeof(normQuery));

        // Walk the normalized query word by word.
        char* cursor = normQuery;
        while (*cursor) {
            while (*cursor == ' ') ++cursor; // skip spaces between words
            char* word = cursor;
            while (*cursor && *cursor != ' ') ++cursor;
            if (*cursor) *cursor++ = '\0'; // terminate the word in place
            if (!*word) continue;

            char wordNoSpace[80]{};
            CopyWithoutSpaces(word, wordNoSpace, sizeof(wordNoSpace));
            if (!ContainsNoCase(normHay, word) &&
                !ContainsNoCase(normHayNoSpace, wordNoSpace))
                return false; // one missing word is enough to reject the item
        }
        return true;
    }


    int GetWeaponDefinitionTeam(int definitionIndex) {
        switch (definitionIndex) {
        case 4:  // Glock-18
        case 7:  // AK-47
        case 11: // G3SG1
        case 13: // Galil AR
        case 17: // MAC-10
        case 29: // Sawed-Off
        case 30: // Tec-9
        case 39: // SG 553
            return LocalInventoryTeamTerrorist;
        case 3:  // Five-SeveN
        case 8:  // AUG
        case 10: // FAMAS
        case 16: // M4A4
        case 27: // MAG-7
        case 32: // P2000
        case 34: // MP9
        case 38: // SCAR-20
        case 60: // M4A1-S
        case 61: // USP-S
            return LocalInventoryTeamCounterTerrorist;
        default:
            return LocalInventoryTeamBoth;
        }
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
    case LocalInventoryCharm: return Localized("Llavero", "Charm");
    default: return Localized("Desconocido", "Unknown");
    }
}

bool IsInventoryItemExternallyApplicable(int type) {
    return type == LocalInventoryMusicKit;
}

bool IsInventoryItemLoadoutSupported(int type) {
    return type == LocalInventoryMusicKit ||
        type == LocalInventoryWeaponSkin || type == LocalInventoryKnife ||
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
    if (item.type == LocalInventoryWeaponSkin)
        return GetWeaponDefinitionTeam(item.definitionIndex);
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

    // The haystack joins every searchable field into one string, so one
    // word can come from the name and another from the rarity ("ak covert").
    char numeric[48]{};
    sprintf_s(numeric, "%d %d", item.definitionIndex, item.paintIndex);

    char haystack[512]{};
    sprintf_s(haystack, "%s %s %s %s %s",
        item.name ? item.name : "", item.group ? item.group : "",
        item.rarity ? item.rarity : "",
        GetLocalInventoryItemTypeName(item.type), numeric);
    return SearchTextHasAllTokens(haystack, query);
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
