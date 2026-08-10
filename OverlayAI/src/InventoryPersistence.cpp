// ============================================================
// InventoryPersistence.cpp
// Saves and loads modified inventory items to a file so they persist between sessions.
// ============================================================

#include "InventoryPersistence.h"

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
    std::vector<std::string> Split(const char* text, char delimiter) {
        std::vector<std::string> fields;
        if (!text) return fields;
        const char* fieldStart = text;
        for (const char* cursor = text;; ++cursor) {
            if (*cursor == delimiter || *cursor == '\0' || *cursor == '\r' || *cursor == '\n') {
                fields.emplace_back(fieldStart, cursor);
                if (*cursor != delimiter) break;
                fieldStart = cursor + 1;
            }
        }
        return fields;
    }

    bool ParseInt(const std::string& text, int& value) {
        if (text.empty()) return false;
        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(text.c_str(), &end, 10);
        if (errno != 0 || !end || *end != '\0' ||
            parsed < INT_MIN || parsed > INT_MAX)
            return false;
        value = static_cast<int>(parsed);
        return true;
    }

    bool ParseInt64(const std::string& text, int64_t& value) {
        if (text.empty()) return false;
        char* end = nullptr;
        errno = 0;
        const long long parsed = std::strtoll(text.c_str(), &end, 10);
        if (errno != 0 || !end || *end != '\0') return false;
        value = static_cast<int64_t>(parsed);
        return true;
    }

    bool ParseLocalId(const std::string& text, LocalItemId& value) {
        if (text.empty() || text.front() == '-') return false;
        char* end = nullptr;
        errno = 0;
        const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
        if (errno != 0 || !end || *end != '\0') return false;
        value = static_cast<LocalItemId>(parsed);
        return true;
    }

    bool ParseFloat(const std::string& text, float& value) {
        if (text.empty()) return false;
        char* end = nullptr;
        errno = 0;
        const float parsed = std::strtof(text.c_str(), &end);
        if (errno != 0 || !end || *end != '\0' || !std::isfinite(parsed))
            return false;
        value = parsed;
        return true;
    }

    int HexDigit(char value) {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    std::string HexEncode(const char* text) {
        static constexpr char kDigits[] = "0123456789ABCDEF";
        std::string result;
        if (!text) return result;
        const std::size_t length = std::strlen(text);
        result.reserve(length * 2);
        for (std::size_t index = 0; index < length; ++index) {
            const unsigned char value = static_cast<unsigned char>(text[index]);
            result.push_back(kDigits[value >> 4]);
            result.push_back(kDigits[value & 0x0F]);
        }
        return result;
    }

    bool HexDecode(const std::string& text, char* destination, std::size_t size) {
        if (!destination || size == 0 || text.size() % 2 != 0 ||
            text.size() / 2 >= size)
            return false;
        for (std::size_t index = 0; index < text.size(); index += 2) {
            const int high = HexDigit(text[index]);
            const int low = HexDigit(text[index + 1]);
            if (high < 0 || low < 0) return false;
            destination[index / 2] = static_cast<char>((high << 4) | low);
        }
        destination[text.size() / 2] = '\0';
        return true;
    }

    bool LoadVersion3Item(InventoryChangerSettings& state, const char* payload) {
        const std::vector<std::string> fields = Split(payload, '|');
        if (fields.size() != 14) return false;

        int slot = -1;
        LocalInventoryItem item;
        int statTrak = 0;
        int souvenir = 0;
        if (!ParseInt(fields[0], slot) || slot < 0 || slot >= kMaxLocalInventoryItems ||
            !ParseLocalId(fields[1], item.localId) ||
            !ParseInt(fields[2], item.type) ||
            !ParseInt(fields[3], item.definitionIndex) ||
            !ParseInt(fields[4], item.paintIndex) ||
            !ParseFloat(fields[5], item.wear) ||
            !ParseInt(fields[6], item.seed) ||
            !ParseInt(fields[7], statTrak) ||
            !ParseInt(fields[8], item.statTrakCount) ||
            !ParseInt(fields[9], souvenir) ||
            !ParseInt(fields[10], item.equippedTeam) ||
            !ParseInt64(fields[11], item.acquiredAt) ||
            !HexDecode(fields[12], item.customName, sizeof(item.customName)) ||
            !HexDecode(fields[13], item.displayName, sizeof(item.displayName)))
            return false;

        item.occupied = true;
        item.statTrak = statTrak != 0;
        item.souvenir = souvenir != 0;
        state.items[slot] = item;
        return true;
    }

    bool LoadVersion2Item(InventoryChangerSettings& state, const char* line) {
        int slot = -1;
        int type = 0;
        int definitionIndex = 0;
        int paintIndex = 0;
        float wear = 0.15f;
        int seed = 0;
        int statTrak = 0;
        int souvenir = 0;
        char displayName[80]{};
        if (sscanf_s(line, "inventory_item_v2=%d,%d,%d,%d,%f,%d,%d,%d,%79[^\r\n]",
            &slot, &type, &definitionIndex, &paintIndex, &wear, &seed,
            &statTrak, &souvenir, displayName,
            static_cast<unsigned>(_countof(displayName))) != 9)
            return false;
        if (slot < 0 || slot >= kMaxLocalInventoryItems) return true;

        LocalInventoryItem& item = state.items[slot];
        item = {};
        item.occupied = true;
        item.type = type;
        item.definitionIndex = definitionIndex;
        item.paintIndex = paintIndex;
        item.wear = wear;
        item.seed = seed;
        item.statTrak = statTrak != 0;
        item.souvenir = souvenir != 0;
        strncpy_s(item.displayName, displayName, _TRUNCATE);
        return true;
    }

    bool LoadLegacyMusicKit(InventoryChangerSettings& state, const char* line) {
        int slot = -1;
        int type = 0;
        int definitionIndex = 0;
        char displayName[48]{};
        if (sscanf_s(line, "inventory_item=%d,%d,%d,%47[^\r\n]",
            &slot, &type, &definitionIndex, displayName,
            static_cast<unsigned>(_countof(displayName))) != 4)
            return false;
        if (slot < 0 || slot >= kMaxLocalInventoryItems) return true;

        LocalInventoryItem& item = state.items[slot];
        item = {};
        item.occupied = true;
        item.type = type;
        item.definitionIndex = definitionIndex;
        strncpy_s(item.displayName, displayName, _TRUNCATE);
        return true;
    }
}

bool TryLoadInventoryConfigLine(
    InventoryChangerSettings& state, InventoryConfigLoadContext& context,
    const char* line) {
    if (!line) return false;
    int value = 0;
    unsigned long long localId = 0;

    if (sscanf_s(line, "inventory_version=%u", &context.sourceVersion) == 1)
        return true;
    if (sscanf_s(line, "inventory_enabled=%d", &value) == 1) {
        state.enabled = value != 0;
        return true;
    }
    if (sscanf_s(line, "inventory_queue_reveal=%d", &value) == 1) {
        state.queueRevealWhenUnavailable = value != 0;
        return true;
    }
    if (sscanf_s(line, "inventory_knives_on_controlled_bots=%d", &value) == 1) {
        state.applyKnivesToControlledBots = value != 0;
        return true;
    }
    if (sscanf_s(line, "inventory_debug_panorama_ui=%d", &value) == 1) {
        state.useDebugPanoramaUi = value != 0;
        return true;
    }
    if (sscanf_s(line, "inventory_next_local_id=%llu", &localId) == 1) {
        state.nextLocalId = static_cast<LocalItemId>(localId);
        return true;
    }
    if (sscanf_s(line, "inventory_selected_local_id=%llu", &localId) == 1) {
        state.selectedLocalId = static_cast<LocalItemId>(localId);
        return true;
    }
    if (sscanf_s(line, "inventory_pending_reveal_item_id=%llu", &localId) == 1) {
        state.pendingRevealItemId = static_cast<LocalItemId>(localId);
        return true;
    }
    if (sscanf_s(line, "inventory_pending_reveal_item=%llu", &localId) == 1) {
        const LocalItemId pendingId = static_cast<LocalItemId>(localId);
        if (pendingId != kInvalidLocalItemId &&
            state.pendingRevealItemCount < kMaxLocalInventoryItems) {
            bool duplicate = false;
            for (int index = 0; index < state.pendingRevealItemCount; ++index) {
                if (state.pendingRevealItemIds[index] == pendingId) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                state.pendingRevealItemIds[
                    state.pendingRevealItemCount++] = pendingId;
        }
        return true;
    }
    if (sscanf_s(line, "inventory_loadout_music_kit=%llu", &localId) == 1) {
        state.loadout.musicKit = static_cast<LocalItemId>(localId);
        return true;
    }
    if (sscanf_s(line, "inventory_loadout_terrorist_knife=%llu", &localId) == 1) {
        state.loadout.terroristKnife = static_cast<LocalItemId>(localId);
        return true;
    }
    if (sscanf_s(line, "inventory_loadout_counter_terrorist_knife=%llu", &localId) == 1) {
        state.loadout.counterTerroristKnife = static_cast<LocalItemId>(localId);
        return true;
    }
    if (sscanf_s(line, "inventory_loadout_terrorist_gloves=%llu", &localId) == 1) {
        state.loadout.terroristGloves = static_cast<LocalItemId>(localId);
        return true;
    }
    if (sscanf_s(line, "inventory_loadout_counter_terrorist_gloves=%llu", &localId) == 1) {
        state.loadout.counterTerroristGloves = static_cast<LocalItemId>(localId);
        return true;
    }
    if (sscanf_s(line, "inventory_loadout_terrorist_agent=%llu", &localId) == 1) {
        state.loadout.terroristAgent = static_cast<LocalItemId>(localId);
        return true;
    }
    if (sscanf_s(line, "inventory_loadout_counter_terrorist_agent=%llu", &localId) == 1) {
        state.loadout.counterTerroristAgent = static_cast<LocalItemId>(localId);
        return true;
    }
    if (sscanf_s(line, "inventory_selected_slot=%d", &value) == 1) {
        context.legacySelectedSlot = value;
        return true;
    }
    if (sscanf_s(line, "inventory_equipped_slot=%d", &value) == 1) {
        context.legacyEquippedSlot = value;
        return true;
    }
    constexpr const char* kVersion3Prefix = "inventory_item_v3=";
    if (std::strncmp(line, kVersion3Prefix, std::strlen(kVersion3Prefix)) == 0) {
        (void)LoadVersion3Item(state, line + std::strlen(kVersion3Prefix));
        return true;
    }
    if (std::strncmp(line, "inventory_item_v2=", 18) == 0)
        return LoadVersion2Item(state, line);
    if (std::strncmp(line, "inventory_item=", 15) == 0)
        return LoadLegacyMusicKit(state, line);
    return false;
}

InventoryMigrationResult FinalizeInventoryConfigLoad(
    InventoryChangerSettings& state, const InventoryConfigLoadContext& context) {
    return FinalizeLoadedInventoryState(
        state, context.legacySelectedSlot, context.legacyEquippedSlot);
}

bool SaveInventoryConfig(FILE* file, const InventoryChangerSettings& state) {
    if (!file) return false;
    if (fprintf(file,
        "inventory_version=%u\n"
        "inventory_enabled=%d\n"
        "inventory_queue_reveal=%d\n"
        "inventory_knives_on_controlled_bots=%d\n"
        "inventory_debug_panorama_ui=%d\n"
        "inventory_next_local_id=%llu\n"
        "inventory_selected_local_id=%llu\n"
        "inventory_pending_reveal_item_id=%llu\n"
        "inventory_loadout_music_kit=%llu\n"
        "inventory_loadout_terrorist_knife=%llu\n"
        "inventory_loadout_counter_terrorist_knife=%llu\n"
        "inventory_loadout_terrorist_gloves=%llu\n"
        "inventory_loadout_counter_terrorist_gloves=%llu\n"
        "inventory_loadout_terrorist_agent=%llu\n"
        "inventory_loadout_counter_terrorist_agent=%llu\n",
        kLocalInventoryStorageVersion, state.enabled ? 1 : 0,
        state.queueRevealWhenUnavailable ? 1 : 0,
        state.applyKnivesToControlledBots ? 1 : 0,
        state.useDebugPanoramaUi ? 1 : 0,
        static_cast<unsigned long long>(state.nextLocalId),
        static_cast<unsigned long long>(state.selectedLocalId),
        static_cast<unsigned long long>(state.pendingRevealItemId),
        static_cast<unsigned long long>(state.loadout.musicKit),
        static_cast<unsigned long long>(state.loadout.terroristKnife),
        static_cast<unsigned long long>(state.loadout.counterTerroristKnife),
        static_cast<unsigned long long>(state.loadout.terroristGloves),
        static_cast<unsigned long long>(state.loadout.counterTerroristGloves),
        static_cast<unsigned long long>(state.loadout.terroristAgent),
        static_cast<unsigned long long>(state.loadout.counterTerroristAgent)) < 0)
        return false;

    for (int index = 0; index < state.pendingRevealItemCount; ++index) {
        if (fprintf(file, "inventory_pending_reveal_item=%llu\n",
                static_cast<unsigned long long>(
                    state.pendingRevealItemIds[index])) < 0)
            return false;
    }

    for (int slot = 0; slot < kMaxLocalInventoryItems; ++slot) {
        const LocalInventoryItem& item = state.items[slot];
        if (!item.occupied) continue;
        const std::string customName = HexEncode(item.customName);
        const std::string displayName = HexEncode(item.displayName);
        if (fprintf(file,
            "inventory_item_v3=%d|%llu|%d|%d|%d|%.6f|%d|%d|%d|%d|%d|%lld|%s|%s\n",
            slot, static_cast<unsigned long long>(item.localId), item.type,
            item.definitionIndex, item.paintIndex, item.wear, item.seed,
            item.statTrak ? 1 : 0, item.statTrakCount, item.souvenir ? 1 : 0,
            item.equippedTeam, static_cast<long long>(item.acquiredAt),
            customName.c_str(), displayName.c_str()) < 0)
            return false;
    }
    return true;
}
