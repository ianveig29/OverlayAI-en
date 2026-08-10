#pragma once

#include "Types.h"
#include <Windows.h>
#include <string>
#include <vector>

extern EspSettings g_Esp;
extern AppSettings g_App;
extern TriggerbotSettings g_Triggerbot;
extern BhopSettings g_Bhop;
extern InventoryChangerSettings g_InventoryChanger;
struct AimSettings;
extern AimSettings g_Aim;
extern HWND g_OverlayHwnd;
extern bool g_MenuOpen;

void SaveEspConfig(const char* path);
void LoadEspConfig(const char* path);
void SetMenuInputMode(bool menuActive);
// Preload helpers: save/load selected slot (1-3). Returns 0 if none.
void SavePreloadSlot(int slot);
int LoadPreloadSlot();

void EnsureConfigStorage();
std::vector<std::string> ListConfigPresets();
bool SaveConfigPreset(const std::string& name, std::string* savedName = nullptr);
bool LoadConfigPreset(const std::string& name);
bool DeleteConfigPreset(const std::string& name);
bool IsDefaultConfigSlot(const std::string& name);
void ResetConfigDefaults();
void SavePreloadConfig(const std::string& name);
std::string LoadPreloadConfig();

// New helpers for bomb/spectator toggles are persisted in ESP config
