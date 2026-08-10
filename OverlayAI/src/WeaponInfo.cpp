#include "WeaponInfo.h"

#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include "Stats.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>

namespace {
    std::string ReadDesignerName(uintptr_t weapon) {
        const uintptr_t identity = mem.Read<uintptr_t>(weapon + Offsets::m_pEntityIdentity);
        if (!IsValidPtr(identity)) return {};
        const uintptr_t nameAddress = mem.Read<uintptr_t>(identity + Offsets::m_designerName);
        if (!IsValidPtr(nameAddress)) return {};

        char raw[96] = {};
        SIZE_T bytesRead = 0;
        const bool read = ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(nameAddress),
            raw, sizeof(raw) - 1, &bytesRead) && bytesRead > 0;
        Stats::rpmReadCount.fetch_add(1);
        if (!read) return {};

        std::string result;
        for (size_t i = 0; i < bytesRead && raw[i] != '\0'; ++i) {
            const unsigned char value = static_cast<unsigned char>(raw[i]);
            if (value < 32 || value > 126) return {};
            result.push_back(static_cast<char>(value));
        }
        return result;
    }

    std::string HumanizeDesignerName(std::string name) {
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        const struct { const char* internal; const char* display; } aliases[] = {
            { "weapon_deagle", "Desert Eagle" }, { "weapon_elite", "Dual Berettas" },
            { "weapon_fiveseven", "Five-SeveN" }, { "weapon_glock", "Glock-18" },
            { "weapon_ak47", "AK-47" }, { "weapon_aug", "AUG" }, { "weapon_awp", "AWP" },
            { "weapon_famas", "FAMAS" }, { "weapon_g3sg1", "G3SG1" },
            { "weapon_galilar", "Galil AR" }, { "weapon_m249", "M249" },
            { "weapon_m4a1", "M4A4" }, { "weapon_m4a1_silencer", "M4A1-S" },
            { "weapon_mac10", "MAC-10" }, { "weapon_mp5sd", "MP5-SD" },
            { "weapon_mp7", "MP7" }, { "weapon_mp9", "MP9" }, { "weapon_p90", "P90" },
            { "weapon_ump45", "UMP-45" }, { "weapon_bizon", "PP-Bizon" },
            { "weapon_xm1014", "XM1014" }, { "weapon_mag7", "MAG-7" },
            { "weapon_negev", "Negev" }, { "weapon_sawedoff", "Sawed-Off" },
            { "weapon_nova", "Nova" }, { "weapon_tec9", "Tec-9" },
            { "weapon_taser", "Zeus x27" }, { "weapon_hkp2000", "P2000" },
            { "weapon_p250", "P250" }, { "weapon_scar20", "SCAR-20" },
            { "weapon_sg556", "SG 553" }, { "weapon_ssg08", "SSG 08" },
            { "weapon_usp_silencer", "USP-S" }, { "weapon_cz75a", "CZ75-Auto" },
            { "weapon_revolver", "R8 Revolver" }, { "weapon_flashbang", "Flashbang" },
            { "weapon_hegrenade", "HE Grenade" }, { "weapon_smokegrenade", "Smoke Grenade" },
            { "weapon_molotov", "Molotov" }, { "weapon_incgrenade", "Incendiary" },
            { "weapon_decoy", "Decoy" }, { "weapon_c4", "C4" }
        };
        for (const auto& alias : aliases) {
            if (name == alias.internal) return alias.display;
        }

        constexpr const char* prefixes[] = { "weapon_", "item_" };
        for (const char* prefix : prefixes) {
            const size_t length = strlen(prefix);
            if (name.rfind(prefix, 0) == 0) {
                name.erase(0, length);
                break;
            }
        }
        std::replace(name.begin(), name.end(), '_', ' ');
        bool capitalize = true;
        for (char& ch : name) {
            if (capitalize && std::isalpha(static_cast<unsigned char>(ch)))
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            capitalize = (ch == ' ');
        }
        return name;
    }

    int ResolveDefinitionIndexFromDesignerName(std::string name) {
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const struct { const char* internal; int definitionIndex; } mappings[] = {
            { "weapon_deagle", 1 }, { "weapon_elite", 2 }, { "weapon_fiveseven", 3 },
            { "weapon_glock", 4 }, { "weapon_ak47", 7 }, { "weapon_aug", 8 },
            { "weapon_awp", 9 }, { "weapon_famas", 10 }, { "weapon_g3sg1", 11 },
            { "weapon_galilar", 13 }, { "weapon_m249", 14 }, { "weapon_m4a1", 16 },
            { "weapon_mac10", 17 }, { "weapon_p90", 19 }, { "weapon_mp5sd", 23 },
            { "weapon_ump45", 24 }, { "weapon_xm1014", 25 }, { "weapon_bizon", 26 },
            { "weapon_mag7", 27 }, { "weapon_negev", 28 }, { "weapon_sawedoff", 29 },
            { "weapon_tec9", 30 }, { "weapon_taser", 31 }, { "weapon_hkp2000", 32 },
            { "weapon_mp7", 33 }, { "weapon_mp9", 34 }, { "weapon_nova", 35 },
            { "weapon_p250", 36 }, { "weapon_scar20", 38 }, { "weapon_sg556", 39 },
            { "weapon_ssg08", 40 }, { "weapon_knife", 42 }, { "weapon_flashbang", 43 },
            { "weapon_hegrenade", 44 }, { "weapon_smokegrenade", 45 },
            { "weapon_molotov", 46 }, { "weapon_decoy", 47 }, { "weapon_incgrenade", 48 },
            { "weapon_c4", 49 }, { "weapon_knife_t", 59 },
            { "weapon_m4a1_silencer", 60 }, { "weapon_usp_silencer", 61 },
            { "weapon_cz75a", 63 }, { "weapon_revolver", 64 },
            { "weapon_bayonet", 500 }, { "weapon_knife_bayonet", 500 },
            { "weapon_knife_css", 503 }, { "weapon_knife_flip", 505 },
            { "weapon_knife_gut", 506 }, { "weapon_knife_karambit", 507 },
            { "weapon_knife_m9_bayonet", 508 }, { "weapon_knife_tactical", 509 },
            { "weapon_knife_falchion", 512 }, { "weapon_knife_survival_bowie", 514 },
            { "weapon_knife_butterfly", 515 }, { "weapon_knife_push", 516 },
            { "weapon_knife_cord", 517 }, { "weapon_knife_canis", 518 },
            { "weapon_knife_ursus", 519 }, { "weapon_knife_gypsy_jackknife", 520 },
            { "weapon_knife_outdoor", 521 }, { "weapon_knife_stiletto", 522 },
            { "weapon_knife_widowmaker", 523 }, { "weapon_knife_skeleton", 525 },
            { "weapon_knife_kukri", 526 }
        };
        for (const auto& mapping : mappings) {
            if (name == mapping.internal) return mapping.definitionIndex;
        }
        return 0;
    }
}

bool ReadActiveWeaponInfo(uintptr_t pawn, uintptr_t entityList,
    bool includeAmmo, ActiveWeaponInfo& out) {
    out = {};
    out.clipAmmo = -1;

    const uintptr_t weapon = GetActiveWeaponEntity(pawn, entityList);
    if (!IsValidPtr(weapon)) return false;

    out.entity = weapon;
    out.designerName = ReadDesignerName(weapon);
    const uintptr_t itemView = weapon + Offsets::m_AttributeManager + Offsets::m_Item;
    const int definitionIndex = mem.Read<uint16_t>(itemView + Offsets::m_iItemDefinitionIndex);
    if (definitionIndex > 0 && definitionIndex <= 4096)
        out.definitionIndex = definitionIndex;
    if (out.definitionIndex == 0 && !out.designerName.empty())
        out.definitionIndex = ResolveDefinitionIndexFromDesignerName(out.designerName);
    if (out.definitionIndex == 0 && out.designerName.empty()) return false;

    if (includeAmmo) {
        const int clipAmmo = mem.Read<int>(weapon + Offsets::m_iClip1);
        if (clipAmmo >= 0 && clipAmmo <= 1000) out.clipAmmo = clipAmmo;
    }
    return true;
}

std::string GetWeaponDisplayName(int definitionIndex) {
    switch (definitionIndex) {
    case 1: return "Desert Eagle";
    case 2: return "Dual Berettas";
    case 3: return "Five-SeveN";
    case 4: return "Glock-18";
    case 7: return "AK-47";
    case 8: return "AUG";
    case 9: return "AWP";
    case 10: return "FAMAS";
    case 11: return "G3SG1";
    case 13: return "Galil AR";
    case 14: return "M249";
    case 16: return "M4A4";
    case 17: return "MAC-10";
    case 19: return "P90";
    case 23: return "MP5-SD";
    case 24: return "UMP-45";
    case 25: return "XM1014";
    case 26: return "PP-Bizon";
    case 27: return "MAG-7";
    case 28: return "Negev";
    case 29: return "Sawed-Off";
    case 30: return "Tec-9";
    case 31: return "Zeus x27";
    case 32: return "P2000";
    case 33: return "MP7";
    case 34: return "MP9";
    case 35: return "Nova";
    case 36: return "P250";
    case 38: return "SCAR-20";
    case 39: return "SG 553";
    case 40: return "SSG 08";
    case 42: return "Knife";
    case 43: return "Flashbang";
    case 44: return "HE Grenade";
    case 45: return "Smoke Grenade";
    case 46: return "Molotov";
    case 47: return "Decoy";
    case 48: return "Incendiary";
    case 49: return "C4";
    case 59: return "Knife T";
    case 60: return "M4A1-S";
    case 61: return "USP-S";
    case 63: return "CZ75-Auto";
    case 64: return "R8 Revolver";
    case 500: return "Bayonet";
    case 503: return "Classic Knife";
    case 505: return "Flip Knife";
    case 506: return "Gut Knife";
    case 507: return "Karambit";
    case 508: return "M9 Bayonet";
    case 509: return "Huntsman Knife";
    case 512: return "Falchion Knife";
    case 514: return "Bowie Knife";
    case 515: return "Butterfly Knife";
    case 516: return "Shadow Daggers";
    case 517: return "Paracord Knife";
    case 518: return "Survival Knife";
    case 519: return "Ursus Knife";
    case 520: return "Navaja Knife";
    case 521: return "Nomad Knife";
    case 522: return "Stiletto Knife";
    case 523: return "Talon Knife";
    case 525: return "Skeleton Knife";
    case 526: return "Kukri Knife";
    default: {
        char fallback[32] = {};
        snprintf(fallback, sizeof(fallback), "Weapon #%d", definitionIndex);
        return fallback;
    }
    }
}

std::string FormatWeaponDisplayText(const ActiveWeaponInfo& weapon, bool includeAmmo) {
    std::string text;
    if (weapon.definitionIndex > 0)
        text = GetWeaponDisplayName(weapon.definitionIndex);
    if ((weapon.definitionIndex <= 0 || text.rfind("Weapon #", 0) == 0) && !weapon.designerName.empty())
        text = HumanizeDesignerName(weapon.designerName);
    if (text.empty()) text = "Weapon";
    if (includeAmmo && weapon.clipAmmo >= 0) {
        text += " | ";
        text += std::to_string(weapon.clipAmmo);
    }
    return text;
}
