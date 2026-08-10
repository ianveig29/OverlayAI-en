#pragma once

#include "InventoryTypes.h"

#include <cstddef>

int ClassifyLocalInventoryItem(
    const LocalInventoryItem& item, char* reason = nullptr, std::size_t reasonSize = 0);
bool ValidateLocalInventoryItem(
    const LocalInventoryItem& item, char* reason = nullptr, std::size_t reasonSize = 0);
bool NormalizeNewLocalInventoryItem(
    const LocalInventoryItem& candidate, LocalInventoryItem& normalized,
    char* reason = nullptr, std::size_t reasonSize = 0);
const char* GetLocalInventoryValidityName(int validity);
