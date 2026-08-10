#pragma once

#include "InventoryStore.h"

#include <cstdio>

struct InventoryConfigLoadContext {
    uint32_t sourceVersion = 0;
    int legacySelectedSlot = -1;
    int legacyEquippedSlot = -1;
};

bool TryLoadInventoryConfigLine(
    InventoryChangerSettings& state, InventoryConfigLoadContext& context,
    const char* line);
InventoryMigrationResult FinalizeInventoryConfigLoad(
    InventoryChangerSettings& state, const InventoryConfigLoadContext& context);
bool SaveInventoryConfig(FILE* file, const InventoryChangerSettings& state);
