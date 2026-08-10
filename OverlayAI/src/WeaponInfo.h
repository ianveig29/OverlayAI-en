#pragma once

#include <cstdint>
#include <string>

struct ActiveWeaponInfo {
    uintptr_t entity = 0;
    int definitionIndex = 0;
    int clipAmmo = -1;
    std::string designerName;
};

bool ReadActiveWeaponInfo(uintptr_t pawn, uintptr_t entityList,
    bool includeAmmo, ActiveWeaponInfo& out);
std::string GetWeaponDisplayName(int definitionIndex);
std::string FormatWeaponDisplayText(const ActiveWeaponInfo& weapon, bool includeAmmo);
