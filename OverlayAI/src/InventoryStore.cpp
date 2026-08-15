#include "InventoryStore.h"

#include "InventoryCatalog.h"
#include "InventoryValidator.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace {
    void RefreshPendingRevealCompatibility(
        InventoryChangerSettings& state) {
        state.pendingRevealItemId = state.pendingRevealItemCount > 0
            ? state.pendingRevealItemIds[0] : kInvalidLocalItemId;
    }

    void RemovePendingRevealId(
        InventoryChangerSettings& state, LocalItemId localId) {
        int writeIndex = 0;
        for (int index = 0; index < state.pendingRevealItemCount; ++index) {
            const LocalItemId pendingId = state.pendingRevealItemIds[index];
            if (pendingId == localId) continue;
            state.pendingRevealItemIds[writeIndex++] = pendingId;
        }
        for (int index = writeIndex; index < state.pendingRevealItemCount;
            ++index)
            state.pendingRevealItemIds[index] = kInvalidLocalItemId;
        state.pendingRevealItemCount = writeIndex;
        RefreshPendingRevealCompatibility(state);
    }

    bool IsLocalIdUsed(const InventoryChangerSettings& state, LocalItemId localId) {
        return FindLocalInventorySlotById(state, localId) >= 0;
    }

    LocalItemId AllocateLocalId(InventoryChangerSettings& state) {
        if (state.nextLocalId == kInvalidLocalItemId)
            state.nextLocalId = 1;

        const LocalItemId firstCandidate = state.nextLocalId;
        do {
            const LocalItemId candidate = state.nextLocalId++;
            if (state.nextLocalId == kInvalidLocalItemId)
                state.nextLocalId = 1;
            if (candidate != kInvalidLocalItemId && !IsLocalIdUsed(state, candidate))
                return candidate;
        } while (state.nextLocalId != firstCandidate);
        return kInvalidLocalItemId;
    }

    int64_t CurrentUnixTime() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void RefreshCompatibilitySlots(InventoryChangerSettings& state) {
        state.selectedSlot = FindLocalInventorySlotById(state, state.selectedLocalId);
        state.equippedSlot = FindLocalInventorySlotById(state, state.loadout.musicKit);
    }

    LocalItemId* GetLoadoutSlot(
        InventoryChangerSettings& state, int itemType, int team) {
        if (itemType == LocalInventoryMusicKit && team == LocalInventoryTeamBoth)
            return &state.loadout.musicKit;
        if (itemType == LocalInventoryKnife) {
            if (team == LocalInventoryTeamTerrorist)
                return &state.loadout.terroristKnife;
            if (team == LocalInventoryTeamCounterTerrorist)
                return &state.loadout.counterTerroristKnife;
        }
        if (itemType == LocalInventoryGloves) {
            if (team == LocalInventoryTeamTerrorist)
                return &state.loadout.terroristGloves;
            if (team == LocalInventoryTeamCounterTerrorist)
                return &state.loadout.counterTerroristGloves;
        }
        if (itemType == LocalInventoryAgent) {
            if (team == LocalInventoryTeamTerrorist)
                return &state.loadout.terroristAgent;
            if (team == LocalInventoryTeamCounterTerrorist)
                return &state.loadout.counterTerroristAgent;
        }
        return nullptr;
    }

    void RefreshEquippedTeams(InventoryChangerSettings& state) {
        for (LocalInventoryItem& item : state.items) {
            if (!item.occupied) continue;
            if (item.type == LocalInventoryWeaponSkin) {
                if (item.equippedTeam < LocalInventoryTeamNone ||
                    item.equippedTeam > LocalInventoryTeamBoth)
                    item.equippedTeam = LocalInventoryTeamNone;
                const InventoryCatalogItem* catalog =
                    FindInventoryCatalogItem(item.type,
                        item.definitionIndex, item.paintIndex);
                const int compatibleTeam = catalog
                    ? GetInventoryCatalogItemTeam(*catalog)
                    : LocalInventoryTeamNone;
                if (compatibleTeam != LocalInventoryTeamBoth)
                    item.equippedTeam &= compatibleTeam;
                continue;
            }
            int team = LocalInventoryTeamNone;
            if (state.loadout.musicKit == item.localId ||
                (state.loadout.terroristKnife == item.localId &&
                    state.loadout.counterTerroristKnife == item.localId) ||
                (state.loadout.terroristGloves == item.localId &&
                    state.loadout.counterTerroristGloves == item.localId) ||
                (state.loadout.terroristAgent == item.localId &&
                    state.loadout.counterTerroristAgent == item.localId)) {
                team = LocalInventoryTeamBoth;
            } else if (state.loadout.terroristKnife == item.localId ||
                state.loadout.terroristGloves == item.localId ||
                state.loadout.terroristAgent == item.localId) {
                team = LocalInventoryTeamTerrorist;
            } else if (state.loadout.counterTerroristKnife == item.localId ||
                state.loadout.counterTerroristGloves == item.localId ||
                state.loadout.counterTerroristAgent == item.localId) {
                team = LocalInventoryTeamCounterTerrorist;
            }
            item.equippedTeam = team;
        }
        RefreshCompatibilitySlots(state);
    }

    bool IsValidLoadoutReference(
        const InventoryChangerSettings& state, LocalItemId localId,
        int itemType, int team) {
        const LocalInventoryItem* item = FindLocalInventoryItemById(state, localId);
        if (!item || item->type != itemType || item->validity != LocalInventoryValid)
            return false;
        const InventoryCatalogItem* catalog = FindInventoryCatalogItem(
            item->type, item->definitionIndex, item->paintIndex);
        return catalog && CanInventoryCatalogItemEquipForTeam(*catalog, team);
    }
}

int FindLocalInventorySlotById(const InventoryChangerSettings& state, LocalItemId localId) {
    if (localId == kInvalidLocalItemId) return -1;
    for (int slot = 0; slot < kMaxLocalInventoryItems; ++slot) {
        const LocalInventoryItem& item = state.items[slot];
        if (item.occupied && item.localId == localId) return slot;
    }
    return -1;
}

LocalInventoryItem* FindLocalInventoryItemById(
    InventoryChangerSettings& state, LocalItemId localId) {
    const int slot = FindLocalInventorySlotById(state, localId);
    return slot >= 0 ? &state.items[slot] : nullptr;
}

const LocalInventoryItem* FindLocalInventoryItemById(
    const InventoryChangerSettings& state, LocalItemId localId) {
    const int slot = FindLocalInventorySlotById(state, localId);
    return slot >= 0 ? &state.items[slot] : nullptr;
}

int CountLocalInventoryItems(const InventoryChangerSettings& state) {
    int count = 0;
    for (const LocalInventoryItem& item : state.items) {
        if (item.occupied) ++count;
    }
    return count;
}

int AddLocalInventoryItemToStore(
    InventoryChangerSettings& state, const LocalInventoryItem& candidate,
    bool markPendingReveal) {
    LocalInventoryItem normalized;
    if (!NormalizeNewLocalInventoryItem(candidate, normalized)) return -1;

    int freeSlot = -1;
    for (int slot = 0; slot < kMaxLocalInventoryItems; ++slot) {
        if (!state.items[slot].occupied) {
            freeSlot = slot;
            break;
        }
    }
    if (freeSlot < 0) return -1;

    normalized.localId = AllocateLocalId(state);
    if (normalized.localId == kInvalidLocalItemId) return -1;
    normalized.acquiredAt = CurrentUnixTime();
    normalized.equippedTeam = LocalInventoryTeamNone;
    normalized.validity = LocalInventoryValid;
    state.items[freeSlot] = normalized;
    state.selectedLocalId = normalized.localId;
    if (markPendingReveal && state.queueRevealWhenUnavailable)
        (void)QueueLocalInventoryReveal(state, normalized.localId);
    RefreshCompatibilitySlots(state);
    return freeSlot;
}

bool UpdateLocalInventoryItemInStore(
    InventoryChangerSettings& state, LocalItemId localId,
    const LocalInventoryItem& candidate) {
    LocalInventoryItem* existing = FindLocalInventoryItemById(state, localId);
    if (!existing) return false;

    LocalInventoryItem normalized;
    if (!NormalizeNewLocalInventoryItem(candidate, normalized)) return false;
    normalized.localId = existing->localId;
    normalized.acquiredAt = existing->acquiredAt;
    normalized.equippedTeam = existing->equippedTeam;
    *existing = normalized;
    RefreshCompatibilitySlots(state);
    return true;
}

bool RemoveLocalInventoryItemFromStore(
    InventoryChangerSettings& state, LocalItemId localId) {
    const int slot = FindLocalInventorySlotById(state, localId);
    if (slot < 0) return false;

    if (state.selectedLocalId == localId)
        state.selectedLocalId = kInvalidLocalItemId;
    RemovePendingRevealId(state, localId);
    if (state.loadout.musicKit == localId) {
        state.loadout.musicKit = kInvalidLocalItemId;
    }
    if (state.loadout.terroristKnife == localId) state.loadout.terroristKnife = 0;
    if (state.loadout.counterTerroristKnife == localId) state.loadout.counterTerroristKnife = 0;
    if (state.loadout.terroristGloves == localId) state.loadout.terroristGloves = 0;
    if (state.loadout.counterTerroristGloves == localId) state.loadout.counterTerroristGloves = 0;
    if (state.loadout.terroristAgent == localId) state.loadout.terroristAgent = 0;
    if (state.loadout.counterTerroristAgent == localId) state.loadout.counterTerroristAgent = 0;
    state.items[slot] = {};
    RefreshEquippedTeams(state);
    return true;
}

void ClearLocalInventoryStore(InventoryChangerSettings& state) {
    const LocalItemId nextLocalId = std::max<LocalItemId>(1, state.nextLocalId);
    const bool enabled = state.enabled;
    const bool queueReveal = state.queueRevealWhenUnavailable;
    const bool applyKnivesToBots = state.applyKnivesToControlledBots;
    state = InventoryChangerSettings{};
    state.nextLocalId = nextLocalId;
    state.enabled = enabled;
    state.queueRevealWhenUnavailable = queueReveal;
    state.applyKnivesToControlledBots = applyKnivesToBots;
}

bool SelectLocalInventoryItem(InventoryChangerSettings& state, LocalItemId localId) {
    if (!FindLocalInventoryItemById(state, localId)) return false;
    state.selectedLocalId = localId;
    RefreshCompatibilitySlots(state);
    return true;
}

bool QueueLocalInventoryReveal(
    InventoryChangerSettings& state, LocalItemId localId) {
    const LocalInventoryItem* item = FindLocalInventoryItemById(state, localId);
    if (!item || item->validity != LocalInventoryValid) return false;
    if (IsLocalInventoryRevealPending(state, localId)) return true;
    if (state.pendingRevealItemCount >= kMaxLocalInventoryItems) return false;
    state.pendingRevealItemIds[state.pendingRevealItemCount++] = localId;
    RefreshPendingRevealCompatibility(state);
    return true;
}

bool IsLocalInventoryRevealPending(
    const InventoryChangerSettings& state, LocalItemId localId) {
    if (localId == kInvalidLocalItemId) return false;
    for (int index = 0; index < state.pendingRevealItemCount; ++index) {
        if (state.pendingRevealItemIds[index] == localId) return true;
    }
    return false;
}

int CountPendingLocalInventoryReveals(
    const InventoryChangerSettings& state) {
    return state.pendingRevealItemCount;
}

void ClearPendingLocalInventoryReveals(InventoryChangerSettings& state) {
    for (LocalItemId& localId : state.pendingRevealItemIds)
        localId = kInvalidLocalItemId;
    state.pendingRevealItemCount = 0;
    RefreshPendingRevealCompatibility(state);
}

bool EquipLocalMusicKit(InventoryChangerSettings& state, LocalItemId localId) {
    return EquipLocalInventoryItem(state, localId, LocalInventoryTeamBoth);
}

void UnequipLocalMusicKit(InventoryChangerSettings& state) {
    (void)UnequipLocalInventoryItem(
        state, LocalInventoryMusicKit, LocalInventoryTeamBoth);
}

bool EquipLocalInventoryItem(
    InventoryChangerSettings& state, LocalItemId localId, int team) {
    LocalInventoryItem* item = FindLocalInventoryItemById(state, localId);
    if (!item || item->validity != LocalInventoryValid ||
        !IsInventoryItemLoadoutSupported(item->type))
        return false;

    const InventoryCatalogItem* catalog = FindInventoryCatalogItem(
        item->type, item->definitionIndex, item->paintIndex);
    if (!catalog || !CanInventoryCatalogItemEquipForTeam(*catalog, team))
        return false;

    if (item->type == LocalInventoryWeaponSkin) {
        const int teamMask = team == LocalInventoryTeamBoth
            ? LocalInventoryTeamBoth : team;
        for (LocalInventoryItem& candidate : state.items) {
            if (!candidate.occupied ||
                candidate.type != LocalInventoryWeaponSkin ||
                candidate.definitionIndex != item->definitionIndex)
                continue;
            candidate.equippedTeam &= ~teamMask;
        }
        item->equippedTeam |= teamMask;
        state.enabled = true;
        RefreshCompatibilitySlots(state);
        return true;
    }

    if (team == LocalInventoryTeamBoth && item->type != LocalInventoryMusicKit) {
        LocalItemId* terroristSlot = GetLoadoutSlot(
            state, item->type, LocalInventoryTeamTerrorist);
        LocalItemId* counterTerroristSlot = GetLoadoutSlot(
            state, item->type, LocalInventoryTeamCounterTerrorist);
        if (!terroristSlot || !counterTerroristSlot) return false;
        *terroristSlot = localId;
        *counterTerroristSlot = localId;
    } else {
        LocalItemId* slot = GetLoadoutSlot(state, item->type, team);
        if (!slot) return false;
        *slot = localId;
    }

    state.enabled = true;
    RefreshEquippedTeams(state);
    return true;
}

bool UnequipLocalInventoryItem(
    InventoryChangerSettings& state, int itemType, int team) {
    bool changed = false;
    if (team == LocalInventoryTeamBoth && itemType != LocalInventoryMusicKit) {
        LocalItemId* terroristSlot = GetLoadoutSlot(
            state, itemType, LocalInventoryTeamTerrorist);
        LocalItemId* counterTerroristSlot = GetLoadoutSlot(
            state, itemType, LocalInventoryTeamCounterTerrorist);
        if (terroristSlot && *terroristSlot != kInvalidLocalItemId) {
            *terroristSlot = kInvalidLocalItemId;
            changed = true;
        }
        if (counterTerroristSlot && *counterTerroristSlot != kInvalidLocalItemId) {
            *counterTerroristSlot = kInvalidLocalItemId;
            changed = true;
        }
    } else {
        LocalItemId* slot = GetLoadoutSlot(state, itemType, team);
        if (slot && *slot != kInvalidLocalItemId) {
            *slot = kInvalidLocalItemId;
            changed = true;
        }
    }
    RefreshEquippedTeams(state);
    return changed;
}

bool UnequipLocalInventoryItemById(
    InventoryChangerSettings& state, LocalItemId localId, int team) {
    LocalInventoryItem* item = FindLocalInventoryItemById(state, localId);
    if (!item || item->type != LocalInventoryWeaponSkin ||
        (team != LocalInventoryTeamTerrorist &&
            team != LocalInventoryTeamCounterTerrorist &&
            team != LocalInventoryTeamBoth))
        return false;
    const int previousTeam = item->equippedTeam;
    item->equippedTeam &= ~team;
    RefreshCompatibilitySlots(state);
    return previousTeam != item->equippedTeam;
}

bool IsLocalInventoryItemEquipped(
    const InventoryChangerSettings& state, LocalItemId localId) {
    const LocalInventoryItem* item = FindLocalInventoryItemById(
        state, localId);
    if (item && item->type == LocalInventoryWeaponSkin)
        return item->equippedTeam != LocalInventoryTeamNone;
    return localId != kInvalidLocalItemId &&
        (state.loadout.musicKit == localId ||
            state.loadout.terroristKnife == localId ||
            state.loadout.counterTerroristKnife == localId ||
            state.loadout.terroristGloves == localId ||
            state.loadout.counterTerroristGloves == localId ||
            state.loadout.terroristAgent == localId ||
            state.loadout.counterTerroristAgent == localId);
}

InventoryMigrationResult FinalizeLoadedInventoryState(
    InventoryChangerSettings& state, int legacySelectedSlot, int legacyEquippedSlot) {
    InventoryMigrationResult result;
    LocalItemId largestId = 0;

    for (LocalInventoryItem& item : state.items) {
        if (!item.occupied) continue;
        if (item.localId == kInvalidLocalItemId || IsLocalIdUsed(state, item.localId) &&
            FindLocalInventoryItemById(state, item.localId) != &item) {
            item.localId = AllocateLocalId(state);
            ++result.assignedLocalIds;
        }
        largestId = (std::max)(largestId, item.localId);
        item.validity = ClassifyLocalInventoryItem(item);
        if (item.validity != LocalInventoryValid) ++result.invalidItems;
    }

    if (state.nextLocalId == kInvalidLocalItemId || state.nextLocalId <= largestId) {
        state.nextLocalId = largestId == (std::numeric_limits<LocalItemId>::max)()
            ? 1 : largestId + 1;
        result.repairedNextLocalId = true;
    }

    if (!FindLocalInventoryItemById(state, state.selectedLocalId) &&
        legacySelectedSlot >= 0 && legacySelectedSlot < kMaxLocalInventoryItems &&
        state.items[legacySelectedSlot].occupied)
        state.selectedLocalId = state.items[legacySelectedSlot].localId;

    if (!FindLocalInventoryItemById(state, state.loadout.musicKit) &&
        legacyEquippedSlot >= 0 && legacyEquippedSlot < kMaxLocalInventoryItems &&
        state.items[legacyEquippedSlot].occupied &&
        state.items[legacyEquippedSlot].type == LocalInventoryMusicKit)
        state.loadout.musicKit = state.items[legacyEquippedSlot].localId;

    if (state.pendingRevealItemCount == 0 &&
        FindLocalInventoryItemById(state, state.pendingRevealItemId)) {
        state.pendingRevealItemIds[0] = state.pendingRevealItemId;
        state.pendingRevealItemCount = 1;
    }
    LocalItemId validPendingIds[kMaxLocalInventoryItems]{};
    int validPendingCount = 0;
    for (int index = 0; index < state.pendingRevealItemCount; ++index) {
        const LocalItemId localId = state.pendingRevealItemIds[index];
        if (!FindLocalInventoryItemById(state, localId)) continue;
        bool duplicate = false;
        for (int previous = 0; previous < validPendingCount; ++previous) {
            if (validPendingIds[previous] == localId) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) validPendingIds[validPendingCount++] = localId;
    }
    for (int index = 0; index < kMaxLocalInventoryItems; ++index)
        state.pendingRevealItemIds[index] = index < validPendingCount
            ? validPendingIds[index] : kInvalidLocalItemId;
    state.pendingRevealItemCount = validPendingCount;
    RefreshPendingRevealCompatibility(state);
    if (!FindLocalInventoryItemById(state, state.loadout.musicKit)) {
        state.loadout.musicKit = kInvalidLocalItemId;
    }

    if (!IsValidLoadoutReference(state, state.loadout.musicKit,
        LocalInventoryMusicKit, LocalInventoryTeamBoth))
        state.loadout.musicKit = kInvalidLocalItemId;
    if (!IsValidLoadoutReference(state, state.loadout.terroristKnife,
        LocalInventoryKnife, LocalInventoryTeamTerrorist))
        state.loadout.terroristKnife = kInvalidLocalItemId;
    if (!IsValidLoadoutReference(state, state.loadout.counterTerroristKnife,
        LocalInventoryKnife, LocalInventoryTeamCounterTerrorist))
        state.loadout.counterTerroristKnife = kInvalidLocalItemId;
    if (!IsValidLoadoutReference(state, state.loadout.terroristGloves,
        LocalInventoryGloves, LocalInventoryTeamTerrorist))
        state.loadout.terroristGloves = kInvalidLocalItemId;
    if (!IsValidLoadoutReference(state, state.loadout.counterTerroristGloves,
        LocalInventoryGloves, LocalInventoryTeamCounterTerrorist))
        state.loadout.counterTerroristGloves = kInvalidLocalItemId;
    if (!IsValidLoadoutReference(state, state.loadout.terroristAgent,
        LocalInventoryAgent, LocalInventoryTeamTerrorist))
        state.loadout.terroristAgent = kInvalidLocalItemId;
    if (!IsValidLoadoutReference(state, state.loadout.counterTerroristAgent,
        LocalInventoryAgent, LocalInventoryTeamCounterTerrorist))
        state.loadout.counterTerroristAgent = kInvalidLocalItemId;
    state.storageVersion = kLocalInventoryStorageVersion;
    RefreshEquippedTeams(state);
    return result;
}

void RevalidateLocalInventoryState(InventoryChangerSettings& state) {
    for (LocalInventoryItem& item : state.items) {
        if (item.occupied) {
            item.validity = ClassifyLocalInventoryItem(item);
            const InventoryCatalogItem* catalog = FindInventoryCatalogItem(
                item.type, item.definitionIndex, item.paintIndex);
            if (catalog)
                strncpy_s(item.displayName, catalog->name, _TRUNCATE);
        }
    }
    RefreshEquippedTeams(state);
}
