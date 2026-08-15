#pragma once

#include <string>

struct ImFont;

bool InitializeWeaponIconFont();
bool IsWeaponIconFontAvailable();
bool HasWeaponIcon(int definitionIndex);
std::string GetWeaponIconUtf8(int definitionIndex);
std::string GetEquipmentIconUtf8(unsigned int codepoint);
ImFont* GetWeaponIconFont();
