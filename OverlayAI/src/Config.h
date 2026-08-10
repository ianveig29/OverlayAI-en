#pragma once

// ============================================================
// Config.h
// Configuration function declarations. Defines what settings exist and how they are saved/loaded.
// ============================================================

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

// Saves ESP configuration to a file
void SaveEspConfig(const char* path);
// Loads ESP configuration from a file
void LoadEspConfig(const char* path);
void SetMenuInputMode(bool menuActive);
// Preload helpers: save/load selected slot (1-3). Returns 0 if none.
void SavePreloadSlot(int slot);
int LoadPreloadSlot();

void EnsureConfigStorage();
std::vector<std::string> ListConfigPresets();
// Saves a named configuration preset
bool SaveConfigPreset(const std::string& name, std::string* savedName = nullptr);
// Loads a saved configuration preset
bool LoadConfigPreset(const std::string& name);
bool DeleteConfigPreset(const std::string& name);
bool IsDefaultConfigSlot(const std::string& name);
// Resets all settings to factory defaults
void ResetConfigDefaults();
void SavePreloadConfig(const std::string& name);
std::string LoadPreloadConfig();

// New helpers for bomb/spectator toggles are persisted in ESP config
