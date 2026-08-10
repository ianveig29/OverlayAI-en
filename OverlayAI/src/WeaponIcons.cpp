// ============================================================
// WeaponIcons.cpp
// Draws weapon icons on screen, showing which weapon each player has.
// ============================================================

#include "WeaponIcons.h"

#include "../imgui.h"
#include "../resource.h"
#include <windows.h>

namespace {
    ImFont* g_weaponIconFont = nullptr;

    std::string EncodePrivateUseCodepoint(int definitionIndex) {
        const unsigned int codepoint = 0xE000u + static_cast<unsigned int>(definitionIndex);
        if (codepoint > 0xFFFFu) return {};

        std::string utf8(3, '\0');
        utf8[0] = static_cast<char>(0xE0u | (codepoint >> 12));
        utf8[1] = static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
        utf8[2] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
        return utf8;
    }
}

bool InitializeWeaponIconFont() {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_WEAPON_ICON_FONT), RT_RCDATA);
    if (!resource) return false;

    HGLOBAL loadedResource = LoadResource(module, resource);
    void* fontData = loadedResource ? LockResource(loadedResource) : nullptr;
    const DWORD fontSize = SizeofResource(module, resource);
    if (!fontData || fontSize < 100) return false;

    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();

    static const ImWchar glyphRanges[] = { 0xE000, 0xE204, 0 };
    ImFontConfig config{};
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.RasterizerMultiply = 1.1f;
    strncpy_s(config.Name, "CS2 Weapon Icons", _TRUNCATE);
    g_weaponIconFont = io.Fonts->AddFontFromMemoryTTF(
        fontData, static_cast<int>(fontSize), 20.0f, &config, glyphRanges);
    return g_weaponIconFont != nullptr;
}

bool IsWeaponIconFontAvailable() {
    return g_weaponIconFont != nullptr;
}

bool HasWeaponIcon(int definitionIndex) {
    if (!g_weaponIconFont) return false;
    switch (definitionIndex) {
    case 1: case 2: case 3: case 4: case 7: case 8: case 9: case 10: case 11:
    case 13: case 14: case 16: case 17: case 19: case 24: case 25: case 26:
    case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34:
    case 35: case 36: case 38: case 39: case 40: case 42: case 43: case 44:
    case 45: case 46: case 47: case 48: case 49: case 59: case 60: case 61:
    case 63: case 64: case 500: case 505: case 506: case 507: case 508:
    case 509: case 512: case 514: case 515: case 516:
        return true;
    default:
        return false;
    }
}

std::string GetWeaponIconUtf8(int definitionIndex) {
    return HasWeaponIcon(definitionIndex) ? EncodePrivateUseCodepoint(definitionIndex) : std::string{};
}

std::string GetEquipmentIconUtf8(unsigned int codepoint) {
    if (!g_weaponIconFont || codepoint < 0xE000u || codepoint > 0xE204u) return {};
    return EncodePrivateUseCodepoint(static_cast<int>(codepoint - 0xE000u));
}

ImFont* GetWeaponIconFont() {
    return g_weaponIconFont;
}
