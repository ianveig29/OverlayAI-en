#include "InventoryValidator.h"

#include "InventoryCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
    void SetReason(char* destination, std::size_t size, const char* text) {
        if (!destination || size == 0) return;
        strncpy_s(destination, size, text ? text : "", _TRUNCATE);
    }

    bool DefinitionExistsInAnotherCategory(const LocalInventoryItem& item) {
        const InventoryCatalogItem* catalog = GetInventoryCatalog();
        const std::size_t count = GetInventoryCatalogSize();
        for (std::size_t index = 0; index < count; ++index) {
            if (catalog[index].definitionIndex == item.definitionIndex &&
                catalog[index].paintIndex == item.paintIndex)
                return true;
        }
        return false;
    }
}

int ClassifyLocalInventoryItem(
    const LocalInventoryItem& item, char* reason, std::size_t reasonSize) {
    if (!item.occupied || item.type < 0 || item.type >= LocalInventoryItemTypeCount ||
        item.definitionIndex <= 0 || !std::isfinite(item.wear)) {
        SetReason(reason, reasonSize, "El articulo esta incompleto.");
        return LocalInventoryIncomplete;
    }

    const InventoryCatalogItem* catalogItem = FindInventoryCatalogItem(
        item.type, item.definitionIndex, item.paintIndex);
    if (!catalogItem) {
        if (DefinitionExistsInAnotherCategory(item)) {
            SetReason(reason, reasonSize, "La categoria no coincide con el articulo.");
            return LocalInventoryIncompatible;
        }
        SetReason(reason, reasonSize, "El articulo no existe en el catalogo actual.");
        return LocalInventoryOutdated;
    }

    if (item.seed < 0 || item.seed > 1000 ||
        item.wear + 0.0001f < catalogItem->minWear ||
        item.wear - 0.0001f > catalogItem->maxWear ||
        (item.statTrak &&
            !IsInventoryCatalogItemStatTrakAllowed(*catalogItem)) ||
        (item.souvenir && !catalogItem->souvenirAllowed) ||
        (item.statTrak && item.souvenir) || item.statTrakCount < 0 ||
        (!item.statTrak && item.statTrakCount != 0)) {
        SetReason(reason, reasonSize, "Los atributos no son compatibles con el catalogo.");
        return LocalInventoryIncompatible;
    }

    SetReason(reason, reasonSize, "Configuracion valida.");
    return LocalInventoryValid;
}

bool ValidateLocalInventoryItem(
    const LocalInventoryItem& item, char* reason, std::size_t reasonSize) {
    return ClassifyLocalInventoryItem(item, reason, reasonSize) == LocalInventoryValid;
}

bool NormalizeNewLocalInventoryItem(
    const LocalInventoryItem& candidate, LocalInventoryItem& normalized,
    char* reason, std::size_t reasonSize) {
    normalized = candidate;
    normalized.occupied = true;

    const InventoryCatalogItem* catalogItem = FindInventoryCatalogItem(
        normalized.type, normalized.definitionIndex, normalized.paintIndex);
    if (!catalogItem) {
        SetReason(reason, reasonSize, "La combinacion no existe en el catalogo local.");
        return false;
    }

    normalized.wear = std::clamp(normalized.wear, catalogItem->minWear, catalogItem->maxWear);
    normalized.seed = std::clamp(normalized.seed, 0, 1000);
    if (!IsInventoryCatalogItemStatTrakAllowed(*catalogItem))
        normalized.statTrak = false;
    if (!normalized.statTrak) normalized.statTrakCount = 0;
    if (!catalogItem->souvenirAllowed) normalized.souvenir = false;
    if (normalized.statTrak) normalized.souvenir = false;
    strncpy_s(normalized.displayName, catalogItem->name, _TRUNCATE);
    normalized.validity = ClassifyLocalInventoryItem(normalized, reason, reasonSize);
    return normalized.validity == LocalInventoryValid;
}

const char* GetLocalInventoryValidityName(int validity) {
    switch (validity) {
    case LocalInventoryValid: return "Valido";
    case LocalInventoryOutdated: return "Desactualizado";
    case LocalInventoryIncompatible: return "Incompatible";
    case LocalInventoryIncomplete: return "Incompleto";
    default: return "Desconocido";
    }
}
