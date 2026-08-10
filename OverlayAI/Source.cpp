#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_set>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

// --- MATH STRUCTURES ---
struct Vector3 { float x, y, z; };
struct Matrix4x4 { float m[4][4]; };

// --- SOURCE 2 SCHEMAS & OFFSETS ---
// These names match standard engine dumps (e.g., client_dll.json)
namespace Offsets {
    // Global Engine Offsets (Update these using a dumper when the game updates)
    // These are mutable so they can be overridden by an external offsets file placed next to the executable.
    uintptr_t dwEntityList = 0x24E5590;
    uintptr_t dwViewMatrix = 0x2344B30;
    uintptr_t dwViewRender = 0x2344ED8;
    uintptr_t dwLocalPlayerController = 0x231E700;
    uintptr_t dwLocalPlayerPawn = 0x233F698;

    // Client module schema (client_dll.hpp / cs2-dumper)
    uintptr_t m_hPlayerPawn = 0x90C;      // CCSPlayerController::m_hPlayerPawn
    uintptr_t m_iPawnHealth = 0x918;      // CCSPlayerController::m_iPawnHealth
    uintptr_t m_bPawnIsAlive = 0x914;     // CCSPlayerController::m_bPawnIsAlive
    uintptr_t m_iTeamNum = 0x3EB;         // C_BaseEntity::m_iTeamNum
    uintptr_t m_iHealth = 0x34C;          // C_BaseEntity::m_iHealth (on pawn)
    uintptr_t m_lifeState = 0x354;        // C_BaseEntity::m_lifeState (0 = alive)
    uintptr_t m_pGameSceneNode = 0x330;   // C_BaseEntity::m_pGameSceneNode
    uintptr_t m_vecAbsOrigin = 0xC8;      // CGameSceneNode::m_vecAbsOrigin
    uintptr_t m_vOldOrigin = 0x1390;      // C_BasePlayerPawn::m_vOldOrigin
    uintptr_t m_iConnected = 0x6EC;       // CBasePlayerController::m_iConnected
    uintptr_t m_iszPlayerName = 0x6F4;    // CBasePlayerController::m_iszPlayerName
    uintptr_t m_sSanitizedPlayerName = 0x860; // CCSPlayerController::m_sSanitizedPlayerName
    uintptr_t m_entitySpottedState = 0x1C38;  // C_CSPlayerPawn::m_entitySpottedState
    uintptr_t m_bSpotted = 0x8;             // EntitySpottedState_t::m_bSpotted
    uintptr_t m_bSpottedByMask = 0xC;         // EntitySpottedState_t::m_bSpottedByMask
    uintptr_t m_pItemServices = 0x11E8;     // C_BasePlayerPawn::m_pItemServices
    uintptr_t m_bHasHelmet = 0x49;          // CCSPlayer_ItemServices::m_bHasHelmet
    uintptr_t m_ArmorValue = 0x1C7C;        // C_CSPlayerPawn::m_ArmorValue
}

enum EspTextAnchor {
    EspTextTop = 0,
    EspTextBottom = 1,
    EspTextLeft = 2,
    EspTextRight = 3
};

struct EspSettings {
    bool showTeammates = true;
    bool showNames = true;
    bool visibilityNames = true;
    bool visibilityBoxes = true;
    bool showHpText = true;
    bool showHpBar = true;
    bool showArmorBar = true;
    bool showArmorText = true;
    bool showDebug = false;
    int nameTextAnchor = EspTextTop;

    int enemyBoxR = 240, enemyBoxG = 50, enemyBoxB = 50;
    int teamBoxR = 80, teamBoxG = 160, teamBoxB = 255;
    int hpTextR = 255, hpTextG = 255, hpTextB = 255;
    int nameTextR = 255, nameTextG = 255, nameTextB = 255;
    int nameVisibleR = 80, nameVisibleG = 255, nameVisibleB = 120;
    int nameHiddenR = 255, nameHiddenG = 70, nameHiddenB = 70;
    int boxVisibleR = 80, boxVisibleG = 255, boxVisibleB = 120;
    int boxHiddenR = 255, boxHiddenG = 70, boxHiddenB = 70;
    int armorBarR = 70, armorBarG = 140, armorBarB = 255;
};

struct AppSettings {
    char menuTitle[64] = "Ianveig29 C.H.E.A.T";
    int menuToggleVk = VK_INSERT;
    bool waitingForMenuKey = false;
};

static EspSettings g_Esp;
static AppSettings g_App;
static HWND g_OverlayHwnd = nullptr;
static bool g_MenuOpen = false;

static ImU32 EspColor(int r, int g, int b, int a = 255) {
    return IM_COL32((ImU32)r, (ImU32)g, (ImU32)b, (ImU32)a);
}

static void SaveEspConfig(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "w");
    if (!f) return;
    fprintf(f,
        "# OverlayAI ESP config\n"
        "show_teammates=%d\n"
        "show_names=%d\n"
        "visibility_names=%d\n"
        "show_hp_text=%d\n"
        "show_hp_bar=%d\n"
        "show_armor_bar=%d\n"
        "show_armor_text=%d\n"
        "show_debug=%d\n"
        "visibility_boxes=%d\n"
        "name_text_anchor=%d\n"
        "menu_title=%s\n"
        "menu_key=%d\n"
        "enemy_box=%d,%d,%d\n"
        "team_box=%d,%d,%d\n"
        "hp_text=%d,%d,%d\n"
        "name_text=%d,%d,%d\n"
        "name_visible=%d,%d,%d\n"
        "name_hidden=%d,%d,%d\n"
        "box_visible=%d,%d,%d\n"
        "box_hidden=%d,%d,%d\n"
        "armor_bar=%d,%d,%d\n",
        g_Esp.showTeammates ? 1 : 0,
        g_Esp.showNames ? 1 : 0,
        g_Esp.visibilityNames ? 1 : 0,
        g_Esp.showHpText ? 1 : 0,
        g_Esp.showHpBar ? 1 : 0,
        g_Esp.showArmorBar ? 1 : 0,
        g_Esp.showArmorText ? 1 : 0,
        g_Esp.showDebug ? 1 : 0,
        g_Esp.visibilityBoxes ? 1 : 0,
        g_Esp.nameTextAnchor,
        g_App.menuTitle,
        g_App.menuToggleVk,
        g_Esp.enemyBoxR, g_Esp.enemyBoxG, g_Esp.enemyBoxB,
        g_Esp.teamBoxR, g_Esp.teamBoxG, g_Esp.teamBoxB,
        g_Esp.hpTextR, g_Esp.hpTextG, g_Esp.hpTextB,
        g_Esp.nameTextR, g_Esp.nameTextG, g_Esp.nameTextB,
        g_Esp.nameVisibleR, g_Esp.nameVisibleG, g_Esp.nameVisibleB,
        g_Esp.nameHiddenR, g_Esp.nameHiddenG, g_Esp.nameHiddenB,
        g_Esp.boxVisibleR, g_Esp.boxVisibleG, g_Esp.boxVisibleB,
        g_Esp.boxHiddenR, g_Esp.boxHiddenG, g_Esp.boxHiddenB,
        g_Esp.armorBarR, g_Esp.armorBarG, g_Esp.armorBarB);
    fclose(f);
}

static void LoadEspConfig(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;
        int i1 = 0, i2 = 0, i3 = 0;
        if (sscanf_s(p, "show_teammates=%d", &i1) == 1) g_Esp.showTeammates = i1 != 0;
        else if (sscanf_s(p, "show_names=%d", &i1) == 1) g_Esp.showNames = i1 != 0;
        else if (sscanf_s(p, "visibility_names=%d", &i1) == 1) g_Esp.visibilityNames = i1 != 0;
        else if (sscanf_s(p, "show_hp_text=%d", &i1) == 1) g_Esp.showHpText = i1 != 0;
        else if (sscanf_s(p, "show_hp_bar=%d", &i1) == 1) g_Esp.showHpBar = i1 != 0;
        else if (sscanf_s(p, "show_armor=%d", &i1) == 1) g_Esp.showArmorBar = i1 != 0;
        else if (sscanf_s(p, "show_armor_bar=%d", &i1) == 1) g_Esp.showArmorBar = i1 != 0;
        else if (sscanf_s(p, "show_armor_text=%d", &i1) == 1) g_Esp.showArmorText = i1 != 0;
        else if (sscanf_s(p, "show_debug=%d", &i1) == 1) g_Esp.showDebug = i1 != 0;
        else if (sscanf_s(p, "visibility_boxes=%d", &i1) == 1) g_Esp.visibilityBoxes = i1 != 0;
        else if (sscanf_s(p, "text_anchor=%d", &i1) == 1) {
            if (i1 >= EspTextTop && i1 <= EspTextRight) g_Esp.nameTextAnchor = i1;
        } else if (sscanf_s(p, "name_text_anchor=%d", &i1) == 1) {
            if (i1 >= EspTextTop && i1 <= EspTextRight) g_Esp.nameTextAnchor = i1;
        } else if (strncmp(p, "menu_title=", 11) == 0) {
            strncpy_s(g_App.menuTitle, p + 11, _TRUNCATE);
            size_t len = strlen(g_App.menuTitle);
            while (len > 0 && (g_App.menuTitle[len - 1] == '\n' || g_App.menuTitle[len - 1] == '\r'))
                g_App.menuTitle[--len] = '\0';
        } else if (sscanf_s(p, "menu_key=%d", &i1) == 1) {
            if (i1 > 0 && i1 < 256) g_App.menuToggleVk = i1;
        }
        else if (sscanf_s(p, "enemy_box=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.enemyBoxR = i1; g_Esp.enemyBoxG = i2; g_Esp.enemyBoxB = i3;
        } else if (sscanf_s(p, "team_box=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.teamBoxR = i1; g_Esp.teamBoxG = i2; g_Esp.teamBoxB = i3;
        } else if (sscanf_s(p, "hp_text=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.hpTextR = i1; g_Esp.hpTextG = i2; g_Esp.hpTextB = i3;
        } else if (sscanf_s(p, "name_text=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.nameTextR = i1; g_Esp.nameTextG = i2; g_Esp.nameTextB = i3;
        } else if (sscanf_s(p, "name_visible=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.nameVisibleR = i1; g_Esp.nameVisibleG = i2; g_Esp.nameVisibleB = i3;
        } else if (sscanf_s(p, "name_hidden=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.nameHiddenR = i1; g_Esp.nameHiddenG = i2; g_Esp.nameHiddenB = i3;
        } else if (sscanf_s(p, "box_visible=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.boxVisibleR = i1; g_Esp.boxVisibleG = i2; g_Esp.boxVisibleB = i3;
        } else if (sscanf_s(p, "box_hidden=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.boxHiddenR = i1; g_Esp.boxHiddenG = i2; g_Esp.boxHiddenB = i3;
        } else if (sscanf_s(p, "armor_bar=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.armorBarR = i1; g_Esp.armorBarG = i2; g_Esp.armorBarB = i3;
        }
    }
    fclose(f);
}

static void SetMenuInputMode(bool menuActive) {
    if (!g_OverlayHwnd) return;

    LONG ex = GetWindowLongW(g_OverlayHwnd, GWL_EXSTYLE);
    if (menuActive) {
        ex &= ~WS_EX_TRANSPARENT;
        SetWindowLongW(g_OverlayHwnd, GWL_EXSTYLE, ex);
        SetForegroundWindow(g_OverlayHwnd);
        SetFocus(g_OverlayHwnd);
        SetCapture(g_OverlayHwnd);
        BlockInput(FALSE);
        while (ShowCursor(TRUE) < 0) {}
        if (ImGui::GetCurrentContext())
            ImGui::GetIO().MouseDrawCursor = true;
    } else {
        ex |= WS_EX_TRANSPARENT;
        SetWindowLongW(g_OverlayHwnd, GWL_EXSTYLE, ex);
        ReleaseCapture();
        BlockInput(FALSE);
        while (ShowCursor(FALSE) >= 0) {}
        if (ImGui::GetCurrentContext())
            ImGui::GetIO().MouseDrawCursor = false;
    }
}

// Try to load overrides from a simple offsets file. Format: key=value per line.
// Values support decimal or 0xHEX. Example:
// dwViewMatrix=0x2344B30
// m_vOldOrigin=5008
static void LoadOffsetsFromFile(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // trim
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;
        char key[128] = {};
        char val[128] = {};
        if (sscanf_s(p, "%127[^=]=%127s", key, (unsigned)_countof(key), val, (unsigned)_countof(val)) == 2) {
            uintptr_t v = 0;
            if (val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) {
                sscanf_s(val + 2, "%llx", (unsigned long long*)&v);
            } else {
                sscanf_s(val, "%llu", (unsigned long long*)&v);
            }
            std::string ks(key);
            if (ks == "dwEntityList") Offsets::dwEntityList = v;
            else if (ks == "dwViewMatrix") Offsets::dwViewMatrix = v;
            else if (ks == "dwViewRender") Offsets::dwViewRender = v;
            else if (ks == "dwGlobalVars") Offsets::dwGlobalVars = v;
            else if (ks == "dwLocalPlayerController") Offsets::dwLocalPlayerController = v;
            else if (ks == "dwLocalPlayerPawn") Offsets::dwLocalPlayerPawn = v;
            else if (ks == "dwWeaponC4") Offsets::dwWeaponC4 = v;
            else if (ks == "m_hPlayerPawn") Offsets::m_hPlayerPawn = v;
            else if (ks == "m_iPawnHealth") Offsets::m_iPawnHealth = v;
            else if (ks == "m_bPawnIsAlive") Offsets::m_bPawnIsAlive = v;
            else if (ks == "m_iTeamNum") Offsets::m_iTeamNum = v;
            else if (ks == "m_iHealth") Offsets::m_iHealth = v;
            else if (ks == "m_lifeState") Offsets::m_lifeState = v;
            else if (ks == "m_pGameSceneNode") Offsets::m_pGameSceneNode = v;
            else if (ks == "m_vecAbsOrigin") Offsets::m_vecAbsOrigin = v;
            else if (ks == "m_vOldOrigin") Offsets::m_vOldOrigin = v;
            else if (ks == "m_iConnected") Offsets::m_iConnected = v;
            else if (ks == "m_iszPlayerName") Offsets::m_iszPlayerName = v;
            else if (ks == "m_sSanitizedPlayerName") Offsets::m_sSanitizedPlayerName = v;
            else if (ks == "m_entitySpottedState") Offsets::m_entitySpottedState = v;
            else if (ks == "m_bSpotted") Offsets::m_bSpotted = v;
            else if (ks == "m_bSpottedByMask") Offsets::m_bSpottedByMask = v;
            else if (ks == "m_pItemServices") Offsets::m_pItemServices = v;
            else if (ks == "m_bHasHelmet") Offsets::m_bHasHelmet = v;
            else if (ks == "m_ArmorValue") Offsets::m_ArmorValue = v;
        }
    }
    fclose(f);
}

// Try to read a JSON-style offsets file with simple substring parsing.
static void LoadOffsetsFromJSON(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf;
    buf.resize(sz);
    fread(&buf[0], 1, sz, f);
    fclose(f);

    auto parseValue = [&](const char* key)->bool {
        std::string pattern = std::string("\"") + key + "\"";
        size_t pos = buf.find(pattern);
        if (pos == std::string::npos) return false;
        size_t colon = buf.find(':', pos + pattern.size());
        if (colon == std::string::npos) return false;
        size_t start = colon + 1;
        while (start < buf.size() && (buf[start] == ' ' || buf[start] == '\"' || buf[start] == '\t')) ++start;
        // read until non-number/hex
        size_t end = start;
        if (start < buf.size() && buf[start] == '0' && (start+1 < buf.size()) && (buf[start+1]=='x' || buf[start+1]=='X')) {
            end = start + 2;
            while (end < buf.size() && isxdigit((unsigned char)buf[end])) ++end;
        } else {
            // digits
            if (buf[start] == '"') ++start; // handle quoted decimal
            while (end < buf.size() && (buf[end] == '-' || isdigit((unsigned char)buf[end]))) ++end;
        }
        if (end <= start) return false;
        std::string token = buf.substr(start, end - start);
        uintptr_t v = 0;
        if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0) {
            sscanf_s(token.c_str()+2, "%llx", (unsigned long long*)&v);
        } else {
            sscanf_s(token.c_str(), "%llu", (unsigned long long*)&v);
        }
        std::string ks(key);
        if (ks == "dwEntityList") Offsets::dwEntityList = v;
        else if (ks == "dwViewMatrix") Offsets::dwViewMatrix = v;
        else if (ks == "dwViewRender") Offsets::dwViewRender = v;
        else if (ks == "dwLocalPlayerController") Offsets::dwLocalPlayerController = v;
        else if (ks == "dwLocalPlayerPawn") Offsets::dwLocalPlayerPawn = v;
        else if (ks == "m_hPlayerPawn") Offsets::m_hPlayerPawn = v;
        else if (ks == "m_iPawnHealth") Offsets::m_iPawnHealth = v;
        else if (ks == "m_bPawnIsAlive") Offsets::m_bPawnIsAlive = v;
        else if (ks == "m_iTeamNum") Offsets::m_iTeamNum = v;
        else if (ks == "m_iHealth") Offsets::m_iHealth = v;
        else if (ks == "m_vOldOrigin") Offsets::m_vOldOrigin = v;
        return true;
    };

    // try keys
    const char* keys[] = {
        "dwEntityList", "dwViewMatrix", "dwViewRender", "dwGlobalVars", "dwLocalPlayerController", "dwLocalPlayerPawn", "dwWeaponC4",
        "m_hPlayerPawn", "m_iPawnHealth", "m_bPawnIsAlive", "m_iTeamNum", "m_iHealth", "m_lifeState", "m_pGameSceneNode", "m_vecAbsOrigin", "m_vOldOrigin",
        "m_iConnected", "m_iszPlayerName", "m_sSanitizedPlayerName", "m_entitySpottedState", "m_bSpotted", "m_bSpottedByMask",
        "m_pItemServices", "m_bHasHelmet", "m_ArmorValue"
    };
    for (auto k : keys) parseValue(k);
}

static void PrintLoadedOffsets() {
    std::cout << "Loaded Offsets:\n";
    std::cout << " dwEntityList=0x" << std::hex << Offsets::dwEntityList << std::dec << "\n";
    std::cout << " dwViewMatrix=0x" << std::hex << Offsets::dwViewMatrix << std::dec << "\n";
    std::cout << " dwLocalPlayerController=0x" << std::hex << Offsets::dwLocalPlayerController << std::dec << "\n";
    std::cout << " m_vOldOrigin=" << Offsets::m_vOldOrigin << "\n";
}



// --- EXTERNAL PROCESS MEMORY MANAGER ---
class ProcessMemory {
public:
    DWORD pid = 0;
    HANDLE hProcess = nullptr;
    uintptr_t clientModule = 0;

    ~ProcessMemory() {
        if (hProcess) CloseHandle(hProcess);
    }

    bool Attach(const wchar_t* processName) {
        PROCESSENTRY32W entry = { sizeof(PROCESSENTRY32W) };
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;

        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, processName) == 0) {
                    pid = entry.th32ProcessID;
                    hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);

        if (!hProcess) return false;

        // Obtain base address of client.dll
        MODULEENTRY32W modEntry = { sizeof(MODULEENTRY32W) };
        HANDLE modSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (modSnapshot != INVALID_HANDLE_VALUE) {
            if (Module32FirstW(modSnapshot, &modEntry)) {
                do {
                    if (_wcsicmp(modEntry.szModule, L"client.dll") == 0) {
                        clientModule = reinterpret_cast<uintptr_t>(modEntry.modBaseAddr);
                        break;
                    }
                } while (Module32NextW(modSnapshot, &modEntry));
            }
            CloseHandle(modSnapshot);
        }
        return clientModule != 0;
    }

    template <typename T>
    T Read(uintptr_t address) {
        T buffer{};
        ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), &buffer, sizeof(T), nullptr);
        return buffer;
    }
};

// Global Memory Context Instance
ProcessMemory mem;

static bool IsValidPtr(uintptr_t p) {
    return p > 0x10000 && p < 0x7FFFFFFFFFFF;
}

static uintptr_t g_entityStride = 0x78; // auto-detected: 0x78 or 0x70

static uintptr_t GetEntityListBase() {
    uintptr_t list = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwEntityList);
    if (IsValidPtr(list)) return list;
    return mem.clientModule + Offsets::dwEntityList;
}

static uintptr_t GetEntityByIndex(uintptr_t entityList, int index, uintptr_t stride) {
    if (!entityList || index <= 0 || index >= 0x8000 || stride == 0) return 0;
    uintptr_t chunk = mem.Read<uintptr_t>(entityList + 8 * ((index & 0x7FFF) >> 9) + 16);
    if (!IsValidPtr(chunk)) return 0;
    return mem.Read<uintptr_t>(chunk + stride * (index & 0x1FF));
}

static uintptr_t GetEntityByIndexAuto(uintptr_t entityList, int index) {
    uintptr_t e = GetEntityByIndex(entityList, index, g_entityStride);
    if (IsValidPtr(e)) return e;
    uintptr_t alt = (g_entityStride == 0x78) ? 0x70 : 0x78;
    return GetEntityByIndex(entityList, index, alt);
}

static uintptr_t DetectEntityStride(uintptr_t entityList, uintptr_t localController, uintptr_t localPawn) {
    const uintptr_t strides[] = { 0x78, 0x70, 120 };
    if (!IsValidPtr(localController) || !IsValidPtr(localPawn)) return 0x78;

    uint32_t pawnHandle = mem.Read<uint32_t>(localController + Offsets::m_hPlayerPawn);
    if (!pawnHandle || pawnHandle == 0xFFFFFFFF) return 0x78;

    int pawnIndex = pawnHandle & 0x7FFF;
    for (uintptr_t stride : strides) {
        uintptr_t pawn = GetEntityByIndex(entityList, pawnIndex, stride);
        if (pawn == localPawn) return stride;
    }
    return 0x78;
}

static Vector3 GetPawnWorldPos(uintptr_t pawn) {
    uintptr_t sceneNode = mem.Read<uintptr_t>(pawn + Offsets::m_pGameSceneNode);
    if (IsValidPtr(sceneNode)) {
        Vector3 pos = mem.Read<Vector3>(sceneNode + Offsets::m_vecAbsOrigin);
        if (std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z)) {
            if (fabs(pos.x) > 1.0f || fabs(pos.y) > 1.0f || fabs(pos.z) > 1.0f)
                return pos;
        }
    }
    return mem.Read<Vector3>(pawn + Offsets::m_vOldOrigin);
}

static bool IsPawnAlive(uintptr_t pawn) {
    return mem.Read<uint8_t>(pawn + Offsets::m_lifeState) == 0;
}

static void ReadPlayerName(uintptr_t controller, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!IsValidPtr(controller)) return;

    char buffer[128] = {};
    SIZE_T read = 0;
    ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(controller + Offsets::m_iszPlayerName),
        buffer, sizeof(buffer) - 1, &read);
    if (buffer[0] != '\0') {
        strncpy_s(out, outSize, buffer, _TRUNCATE);
        return;
    }

    uintptr_t namePtr = mem.Read<uintptr_t>(controller + Offsets::m_sSanitizedPlayerName);
    if (!IsValidPtr(namePtr)) return;
    ReadProcessMemory(mem.hProcess, reinterpret_cast<LPCVOID>(namePtr), buffer, sizeof(buffer) - 1, &read);
    if (buffer[0] != '\0')
        strncpy_s(out, outSize, buffer, _TRUNCATE);
}

static int FindEntityIndexByAddress(uintptr_t entityList, uintptr_t address, uintptr_t stride) {
    if (!IsValidPtr(address)) return -1;
    for (int i = 1; i <= 8192; ++i) {
        if (GetEntityByIndex(entityList, i, stride) == address)
            return i;
    }
    return -1;
}

static bool IsPawnVisibleToLocal(uintptr_t pawn, int localPlayerIndex) {
    uintptr_t spottedBase = pawn + Offsets::m_entitySpottedState;
    bool spottedFlag = mem.Read<bool>(spottedBase + Offsets::m_bSpotted);

    uintptr_t maskBase = spottedBase + Offsets::m_bSpottedByMask;
    uint32_t mask0 = mem.Read<uint32_t>(maskBase);
    uint32_t mask1 = mem.Read<uint32_t>(maskBase + 4);

    if (localPlayerIndex > 0 && localPlayerIndex <= 64) {
        bool byMask = (localPlayerIndex <= 32)
            ? ((mask0 & (1u << (localPlayerIndex - 1))) != 0)
            : ((mask1 & (1u << (localPlayerIndex - 33))) != 0);
        return byMask || spottedFlag;
    }

    return spottedFlag || mask0 != 0 || mask1 != 0;
}

static int FindLocalPlayerIndex(uintptr_t entityList, uintptr_t localController, uintptr_t stride) {
    for (int i = 1; i <= 64; ++i) {
        if (GetEntityByIndex(entityList, i, stride) == localController)
            return i;
    }
    return -1;
}

static uintptr_t FindControllerForPawn(uintptr_t entityList, uintptr_t pawn, uintptr_t stride) {
    if (!IsValidPtr(pawn)) return 0;
    for (int i = 1; i <= 64; ++i) {
        uintptr_t controller = GetEntityByIndex(entityList, i, stride);
        if (!IsValidPtr(controller)) continue;
        uint32_t pawnHandle = mem.Read<uint32_t>(controller + Offsets::m_hPlayerPawn);
        if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;
        uintptr_t entryPawn = GetEntityByIndex(entityList, pawnHandle & 0x7FFF, stride);
        if (entryPawn == pawn) return controller;
    }
    return 0;
}

static bool IsConnectedPlayerController(uintptr_t controller) {
    uint32_t state = mem.Read<uint32_t>(controller + Offsets::m_iConnected);
    return state == 0;
}

static uintptr_t ResolveLocalPawn(uintptr_t entityList, uintptr_t localController, uintptr_t localPawn, uintptr_t stride) {
    if (IsValidPtr(localPawn)) return localPawn;
    if (!IsValidPtr(localController)) return 0;
    uint32_t pawnHandle = mem.Read<uint32_t>(localController + Offsets::m_hPlayerPawn);
    if (!pawnHandle || pawnHandle == 0xFFFFFFFF) return 0;
    return GetEntityByIndex(entityList, pawnHandle & 0x7FFF, stride);
}

static int ResolveLocalPlayerIndex(uintptr_t localController, uintptr_t localPawn) {
    if (IsValidPtr(localController)) {
        uint32_t pawnHandle = mem.Read<uint32_t>(localController + Offsets::m_hPlayerPawn);
        if (pawnHandle && pawnHandle != 0xFFFFFFFF)
            return pawnHandle & 0x7FFF;
    }
    return -1;
}

static void ResolveActiveLocal(uintptr_t entityList, uintptr_t stride,
    uintptr_t& outController, uintptr_t& outPawn, int& outTeam, int& outPlayerIndex)
{
    outController = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerController);
    outPawn = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerPawn);
    outPawn = ResolveLocalPawn(entityList, outController, outPawn, stride);

    uintptr_t controllerForPawn = FindControllerForPawn(entityList, outPawn, stride);
    if (IsValidPtr(controllerForPawn))
        outController = controllerForPawn;

    outTeam = 0;
    if (IsValidPtr(outPawn))
        outTeam = mem.Read<uint8_t>(outPawn + Offsets::m_iTeamNum);
    else if (IsValidPtr(outController))
        outTeam = mem.Read<uint8_t>(outController + Offsets::m_iTeamNum);

    outPlayerIndex = -1;
    if (IsValidPtr(outPawn))
        outPlayerIndex = FindEntityIndexByAddress(entityList, outPawn, stride);
    if (outPlayerIndex < 0)
        outPlayerIndex = ResolveLocalPlayerIndex(outController, outPawn);
    if (outPlayerIndex < 0 && IsValidPtr(outController))
        outPlayerIndex = FindLocalPlayerIndex(entityList, outController, stride);
}

static void GetPlayerArmorInfo(uintptr_t pawn, int& armorOut, bool& hasHelmetOut) {
    armorOut = mem.Read<int>(pawn + Offsets::m_ArmorValue);
    hasHelmetOut = false;
    uintptr_t itemServices = mem.Read<uintptr_t>(pawn + Offsets::m_pItemServices);
    if (IsValidPtr(itemServices))
        hasHelmetOut = mem.Read<bool>(itemServices + Offsets::m_bHasHelmet);
}

static void DrawOutlinedText(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text) {
    ImVec2 sz = ImGui::CalcTextSize(text);
    drawList->AddRectFilled(
        ImVec2(pos.x - 2.0f, pos.y - 1.0f),
        ImVec2(pos.x + sz.x + 2.0f, pos.y + sz.y + 1.0f),
        IM_COL32(0, 0, 0, 170));
    drawList->AddText(pos, color, text);
}

static ImU32 GetVisibilityColor(bool visible, bool useVisibility,
    int visR, int visG, int visB, int hidR, int hidG, int hidB,
    int baseR, int baseG, int baseB)
{
    if (useVisibility)
        return visible ? EspColor(visR, visG, visB) : EspColor(hidR, hidG, hidB);
    return EspColor(baseR, baseG, baseB);
}

static ImVec2 CalcAnchoredTextPos(int anchor, float boxX, float boxY, float boxW, float boxH, const ImVec2& textSize) {
    ImVec2 pos{};
    switch (anchor) {
    case EspTextBottom:
        pos.x = boxX + boxW * 0.5f - textSize.x * 0.5f;
        pos.y = boxY + boxH + 4.0f;
        break;
    case EspTextLeft:
        pos.x = boxX - textSize.x - 8.0f;
        pos.y = boxY + boxH * 0.5f - textSize.y * 0.5f;
        break;
    case EspTextRight:
        pos.x = boxX + boxW + 8.0f;
        pos.y = boxY + boxH * 0.5f - textSize.y * 0.5f;
        break;
    case EspTextTop:
    default:
        pos.x = boxX + boxW * 0.5f - textSize.x * 0.5f;
        pos.y = boxY - textSize.y - 4.0f;
        break;
    }
    return pos;
}

static void DrawAnchoredOutlinedText(ImDrawList* drawList, int anchor, float boxX, float boxY, float boxW, float boxH,
    ImU32 color, const char* text)
{
    ImVec2 sz = ImGui::CalcTextSize(text);
    ImVec2 pos = CalcAnchoredTextPos(anchor, boxX, boxY, boxW, boxH, sz);
    DrawOutlinedText(drawList, pos, color, text);
}

// Debug logging removed per user request.

static const char* VkToString(int vk) {
    static char buf[32];
    switch (vk) {
    case VK_INSERT: return "INSERT";
    case VK_DELETE: return "DELETE";
    case VK_HOME: return "HOME";
    case VK_END: return "END";
    case VK_PRIOR: return "PAGE UP";
    case VK_NEXT: return "PAGE DOWN";
    case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3"; case VK_F4: return "F4";
    case VK_F5: return "F5"; case VK_F6: return "F6"; case VK_F7: return "F7"; case VK_F8: return "F8";
    case VK_F9: return "F9"; case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
    default:
        if (vk >= 'A' && vk <= 'Z') { snprintf(buf, sizeof(buf), "%c", vk); return buf; }
        if (vk >= '0' && vk <= '9') { snprintf(buf, sizeof(buf), "%c", vk); return buf; }
        snprintf(buf, sizeof(buf), "VK_%d", vk);
        return buf;
    }
}

static void PollMenuKeyBind() {
    if (!g_App.waitingForMenuKey) return;
    for (int vk = 1; vk < 256; ++vk) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
        if (GetAsyncKeyState(vk) & 0x8000) {
            g_App.menuToggleVk = vk;
            g_App.waitingForMenuKey = false;
            break;
        }
    }
}

static void DrawColorEdit(const char* label, int* r, int* g, int* b) {
    float col[3] = { *r / 255.0f, *g / 255.0f, *b / 255.0f };
    if (ImGui::ColorEdit3(label, col, ImGuiColorEditFlags_NoInputs)) {
        *r = (int)(col[0] * 255.0f);
        *g = (int)(col[1] * 255.0f);
        *b = (int)(col[2] * 255.0f);
    }
}

static void ApplyOverlayUiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.15f, 0.96f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.15f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.19f, 0.23f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.35f, 0.65f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.42f, 0.75f, 1.0f);
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.0f);
}

static void RenderEspMenu() {
    ImGui::SetNextWindowSize(ImVec2(460, 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.96f);
    if (ImGui::Begin(g_App.menuTitle, &g_MenuOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::InputText("Titulo ventana", g_App.menuTitle, sizeof(g_App.menuTitle));

        if (g_App.waitingForMenuKey)
            ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Pulsa la tecla para el menu...");
        if (ImGui::Button(g_App.waitingForMenuKey ? "Esperando tecla..." : "Cambiar tecla menu"))
            g_App.waitingForMenuKey = true;
        ImGui::SameLine();
        ImGui::Text("Actual: %s", VkToString(g_App.menuToggleVk));

        ImGui::Separator();

        ImGui::Checkbox("Mostrar companeros", &g_Esp.showTeammates);
        ImGui::Checkbox("Nombres", &g_Esp.showNames);
        ImGui::Checkbox("Color nombre por visibilidad", &g_Esp.visibilityNames);
        ImGui::Checkbox("Color caja por visibilidad", &g_Esp.visibilityBoxes);
        ImGui::Checkbox("Texto HP (junto a barra vida)", &g_Esp.showHpText);
        ImGui::Checkbox("Barra HP", &g_Esp.showHpBar);
        ImGui::Checkbox("Barra chaleco (azul)", &g_Esp.showArmorBar);
        ImGui::Checkbox("Debug (info en pantalla)", &g_Esp.showDebug);

        const char* textPositions[] = { "Arriba", "Abajo", "Izquierda", "Derecha" };
        ImGui::Combo("Posicion del nombre", &g_Esp.nameTextAnchor, textPositions, 4);

        ImGui::Separator();
        ImGui::Text("Colores caja (sin visibilidad)");
        DrawColorEdit("Enemigo", &g_Esp.enemyBoxR, &g_Esp.enemyBoxG, &g_Esp.enemyBoxB);
        DrawColorEdit("Companero", &g_Esp.teamBoxR, &g_Esp.teamBoxG, &g_Esp.teamBoxB);
        if (g_Esp.visibilityBoxes) {
            DrawColorEdit("Caja visible", &g_Esp.boxVisibleR, &g_Esp.boxVisibleG, &g_Esp.boxVisibleB);
            DrawColorEdit("Caja oculta", &g_Esp.boxHiddenR, &g_Esp.boxHiddenG, &g_Esp.boxHiddenB);
        }

        ImGui::Text("Colores texto");
        DrawColorEdit("HP", &g_Esp.hpTextR, &g_Esp.hpTextG, &g_Esp.hpTextB);
        DrawColorEdit("Nombre (fijo)", &g_Esp.nameTextR, &g_Esp.nameTextG, &g_Esp.nameTextB);
        if (g_Esp.visibilityNames) {
            DrawColorEdit("Nombre visible", &g_Esp.nameVisibleR, &g_Esp.nameVisibleG, &g_Esp.nameVisibleB);
            DrawColorEdit("Nombre oculto", &g_Esp.nameHiddenR, &g_Esp.nameHiddenG, &g_Esp.nameHiddenB);
        }
        DrawColorEdit("Barra chaleco", &g_Esp.armorBarR, &g_Esp.armorBarG, &g_Esp.armorBarB);

        if (ImGui::Button("Guardar esp_config.ini"))
            SaveEspConfig("esp_config.ini");

        ImGui::End();
    }
}

// --- CAMERA TRANSFORMATION (WORLD TO SCREEN) ---
bool WorldToScreen(const Vector3& world, Vector3& screen, const Matrix4x4& matrix, int width, int height,
    int* modeOut = nullptr, float* clipOut = nullptr, float* ndcXOut = nullptr, float* ndcYOut = nullptr)
{
    // Try several projection interpretations and return diagnostic values when requested.
    struct Candidate { int mode; float clipX, clipY, clipW; };
    Candidate cand;
    bool ok = false;

    // Mode 0: row-major typical (w = row3 dot world + m33)
    cand.mode = 0;
    cand.clipW = world.x * matrix.m[3][0] + world.y * matrix.m[3][1] + world.z * matrix.m[3][2] + matrix.m[3][3];
    cand.clipX = world.x * matrix.m[0][0] + world.y * matrix.m[0][1] + world.z * matrix.m[0][2] + matrix.m[0][3];
    cand.clipY = world.x * matrix.m[1][0] + world.y * matrix.m[1][1] + world.z * matrix.m[1][2] + matrix.m[1][3];
    if (cand.clipW > 0.001f) ok = true;

    // Mode 1: column-major / transposed
    Candidate cand1;
    cand1.mode = 1;
    cand1.clipW = world.x * matrix.m[0][3] + world.y * matrix.m[1][3] + world.z * matrix.m[2][3] + matrix.m[3][3];
    cand1.clipX = world.x * matrix.m[0][0] + world.y * matrix.m[1][0] + world.z * matrix.m[2][0] + matrix.m[3][0];
    cand1.clipY = world.x * matrix.m[0][1] + world.y * matrix.m[1][1] + world.z * matrix.m[2][1] + matrix.m[3][1];

    Candidate* use = nullptr;
    if (ok && std::isfinite(cand.clipX) && std::isfinite(cand.clipY)) use = &cand;
    else if (cand1.clipW > 0.001f && std::isfinite(cand1.clipX) && std::isfinite(cand1.clipY)) use = &cand1;

    if (!use) return false;

    float ndcX = use->clipX / use->clipW;
    float ndcY = use->clipY / use->clipW;
    screen.x = (width / 2.0f) + (ndcX * (width / 2.0f));
    screen.y = (height / 2.0f) - (ndcY * (height / 2.0f));

    if (modeOut) *modeOut = use->mode;
    if (clipOut) *clipOut = use->clipW;
    if (ndcXOut) *ndcXOut = ndcX;
    if (ndcYOut) *ndcYOut = ndcY;

    // On-screen validation
    if (screen.x < -2000 || screen.x > width + 2000 || screen.y < -2000 || screen.y > height + 2000) return false;
    return true;
}

static bool ReadViewMatrix(Matrix4x4& out) {
    Matrix4x4 m = mem.Read<Matrix4x4>(mem.clientModule + Offsets::dwViewMatrix);
    if (std::abs(m.m[0][0]) > 1e-5f && std::abs(m.m[3][3]) > 1e-5f) {
        out = m;
        return true;
    }
    m = mem.Read<Matrix4x4>(mem.clientModule + Offsets::dwViewRender);
    if (std::abs(m.m[0][0]) > 1e-5f && std::abs(m.m[3][3]) > 1e-5f) {
        out = m;
        return true;
    }
    return false;
}

// --- RENDER GEOMETRY STAGE ---
void RenderESP(int screenWidth, int screenHeight) {
    if (!mem.clientModule) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    int drawn = 0;
    int scanned = 0;

    Matrix4x4 viewMatrix{};
    bool matrixOk = ReadViewMatrix(viewMatrix);

    uintptr_t entityList = GetEntityListBase();
    uintptr_t localController = 0;
    uintptr_t localPawn = 0;
    int localTeam = 0;
    int localPlayerIndex = -1;

    if (IsValidPtr(entityList)) {
        ResolveActiveLocal(entityList, g_entityStride, localController, localPawn, localTeam, localPlayerIndex);
        if (IsValidPtr(localController) && IsValidPtr(localPawn))
            g_entityStride = DetectEntityStride(entityList, localController, localPawn);
        ResolveActiveLocal(entityList, g_entityStride, localController, localPawn, localTeam, localPlayerIndex);
    }

    if (!matrixOk || !IsValidPtr(entityList)) return;

    std::unordered_set<uintptr_t> seenPawns;
    std::unordered_set<uintptr_t> seenControllers;

    constexpr int kMaxPlayers = 64;
    for (int i = 1; i <= kMaxPlayers; ++i) {
        uintptr_t controller = GetEntityByIndexAuto(entityList, i);
        if (!IsValidPtr(controller)) continue;
        if (seenControllers.find(controller) != seenControllers.end()) continue;
        seenControllers.insert(controller);
        ++scanned;

        uint32_t pawnHandle = mem.Read<uint32_t>(controller + Offsets::m_hPlayerPawn);
        if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;

        uintptr_t pawn = GetEntityByIndexAuto(entityList, pawnHandle & 0x7FFF);
        if (!IsValidPtr(pawn) || pawn == localPawn) continue;
        if (seenPawns.find(pawn) != seenPawns.end()) continue;
        seenPawns.insert(pawn);

        bool aliveOnPawn = IsPawnAlive(pawn);
        bool aliveOnCtrl = mem.Read<bool>(controller + Offsets::m_bPawnIsAlive);
        if (!aliveOnPawn && !aliveOnCtrl) continue;

        int team = mem.Read<uint8_t>(pawn + Offsets::m_iTeamNum);
        if (team == 0) team = mem.Read<uint8_t>(controller + Offsets::m_iTeamNum);
        bool isTeammate = (localTeam != 0 && team == localTeam);
        if (isTeammate && !g_Esp.showTeammates) continue;

        int health = mem.Read<int>(pawn + Offsets::m_iHealth);
        if (health <= 0 || health > 100)
            health = mem.Read<int>(controller + Offsets::m_iPawnHealth);
        if (health <= 0 || health > 100) continue;

        bool visible = IsPawnVisibleToLocal(pawn, localPlayerIndex);

        Vector3 feetWorld = GetPawnWorldPos(pawn);
        if (!std::isfinite(feetWorld.x) || !std::isfinite(feetWorld.y) || !std::isfinite(feetWorld.z)) continue;
        if (fabs(feetWorld.x) < 1.0f && fabs(feetWorld.y) < 1.0f && fabs(feetWorld.z) < 1.0f) continue;

        Vector3 headWorld = { feetWorld.x, feetWorld.y, feetWorld.z + 72.0f };
        Vector3 feetScreen, headScreen;
        if (!WorldToScreen(feetWorld, feetScreen, viewMatrix, screenWidth, screenHeight)) continue;
        if (!WorldToScreen(headWorld, headScreen, viewMatrix, screenWidth, screenHeight)) continue;

        float boxHeight = feetScreen.y - headScreen.y;
        if (boxHeight <= 2.0f || boxHeight > (float)screenHeight * 1.5f) continue;

        float boxWidth = boxHeight / 1.8f;
        float topLeftX = headScreen.x - (boxWidth / 2.0f);
        float topLeftY = headScreen.y;

        ImU32 boxColor = GetVisibilityColor(visible, g_Esp.visibilityBoxes,
            g_Esp.boxVisibleR, g_Esp.boxVisibleG, g_Esp.boxVisibleB,
            g_Esp.boxHiddenR, g_Esp.boxHiddenG, g_Esp.boxHiddenB,
            isTeammate ? g_Esp.teamBoxR : g_Esp.enemyBoxR,
            isTeammate ? g_Esp.teamBoxG : g_Esp.enemyBoxG,
            isTeammate ? g_Esp.teamBoxB : g_Esp.enemyBoxB);

        const float barThickness = 3.0f;
        const float barPadding = 5.0f;
        float hpBarX = topLeftX - barPadding - barThickness;
        float armorBarX = topLeftX + boxWidth + barPadding;

        // read armor once per-player so we can show numeric value and bar
        int armor = 0;
        bool hasHelmet = false;
        GetPlayerArmorInfo(pawn, armor, hasHelmet);
        // (armor debug/testing removed)

        if (g_Esp.showHpBar) {
            drawList->AddRectFilled({ hpBarX, topLeftY }, { hpBarX + barThickness, topLeftY + boxHeight }, IM_COL32(21, 21, 21, 200));
            float hpFactor = (std::min)(1.0f, (std::max)(0.0f, static_cast<float>(health) / 100.0f));
            float hpBarHeight = boxHeight * hpFactor;
            float hpBarTopY = topLeftY + boxHeight - hpBarHeight;
            ImU32 hpFill = IM_COL32(
                (ImU32)((1.0f - hpFactor) * 255.0f),
                (ImU32)(hpFactor * 255.0f),
                0, 255);
            drawList->AddRectFilled({ hpBarX, hpBarTopY }, { hpBarX + barThickness, topLeftY + boxHeight }, hpFill);
            drawList->AddRect({ hpBarX - 1, topLeftY - 1 }, { hpBarX + barThickness + 1, topLeftY + boxHeight + 1 }, IM_COL32(0, 0, 0, 180));
        }

        if (g_Esp.showArmorBar) {
            drawList->AddRectFilled({ armorBarX, topLeftY }, { armorBarX + barThickness, topLeftY + boxHeight }, IM_COL32(21, 21, 21, 200));
            float armorFactor = (std::min)(1.0f, (std::max)(0.0f, static_cast<float>(armor) / 100.0f));
            float armorBarHeight = boxHeight * armorFactor;
            float armorBarTopY = topLeftY + boxHeight - armorBarHeight;
            ImU32 armorFill = EspColor(g_Esp.armorBarR, g_Esp.armorBarG, g_Esp.armorBarB);
            if (hasHelmet)
                armorFill = EspColor(
                    (std::min)(255, g_Esp.armorBarR + 40),
                    (std::min)(255, g_Esp.armorBarG + 40),
                    255);
            drawList->AddRectFilled({ armorBarX, armorBarTopY }, { armorBarX + barThickness, topLeftY + boxHeight }, armorFill);
            drawList->AddRect({ armorBarX - 1, topLeftY - 1 }, { armorBarX + barThickness + 1, topLeftY + boxHeight + 1 }, IM_COL32(0, 0, 0, 180));
        }

        drawList->AddRect({ topLeftX - 1, topLeftY - 1 }, { topLeftX + boxWidth + 1, topLeftY + boxHeight + 1 }, IM_COL32(0, 0, 0, 160));
        drawList->AddRect({ topLeftX, topLeftY }, { topLeftX + boxWidth, topLeftY + boxHeight }, boxColor, 0.0f, 0, 1.5f);

        if (g_Esp.showNames) {
            char playerName[128] = {};
            ReadPlayerName(controller, playerName, sizeof(playerName));
            if (playerName[0] == '\0')
                snprintf(playerName, sizeof(playerName), "Player %d", i);

            ImU32 nameColor = GetVisibilityColor(visible, g_Esp.visibilityNames,
                g_Esp.nameVisibleR, g_Esp.nameVisibleG, g_Esp.nameVisibleB,
                g_Esp.nameHiddenR, g_Esp.nameHiddenG, g_Esp.nameHiddenB,
                g_Esp.nameTextR, g_Esp.nameTextG, g_Esp.nameTextB);
            DrawAnchoredOutlinedText(drawList, g_Esp.nameTextAnchor, topLeftX, topLeftY, boxWidth, boxHeight, nameColor, playerName);
        }

        if (g_Esp.showHpText) {
            char infoBuf[32];
            snprintf(infoBuf, sizeof(infoBuf), "%d HP", health);
            ImVec2 hpSz = ImGui::CalcTextSize(infoBuf);
            float hpTextX = hpBarX - hpSz.x - 4.0f;
            float hpTextY = topLeftY + boxHeight * 0.5f - hpSz.y * 0.5f;
            DrawOutlinedText(drawList, ImVec2(hpTextX, hpTextY), EspColor(g_Esp.hpTextR, g_Esp.hpTextG, g_Esp.hpTextB), infoBuf);
        }

        if (g_Esp.showArmorText) {
            // Draw numeric armor using the same placement and style as HP text
            // Format matches HP text ("<N> HP") but with "AR" label
            char aBuf[32];
            snprintf(aBuf, sizeof(aBuf), "%d AR", armor);
            ImVec2 aSz = ImGui::CalcTextSize(aBuf);
            // place using same logic as HP text: left of the armor bar
            float aTextX = armorBarX - aSz.x - 4.0f;
            float aTextY = topLeftY + boxHeight * 0.5f - aSz.y * 0.5f;
            DrawOutlinedText(drawList, ImVec2(aTextX, aTextY), EspColor(g_Esp.hpTextR, g_Esp.hpTextG, g_Esp.hpTextB), aBuf);
        }

        // per-player armor debug removed

        ++drawn;
    }

    if (g_Esp.showDebug) {
        char line1[192];
        char line2[192];
        snprintf(line1, sizeof(line1),
            "ESP mat:%s drawn:%d scanned:%d stride:0x%llX team:%d idx:%d",
            matrixOk ? "OK" : "NO", drawn, scanned,
            (unsigned long long)g_entityStride, localTeam, localPlayerIndex);
        snprintf(line2, sizeof(line2),
            "pawn:0x%llX ctrl:0x%llX list:0x%llX",
            (unsigned long long)localPawn,
            (unsigned long long)localController,
            (unsigned long long)entityList);
        DrawOutlinedText(drawList, ImVec2(10.0f, (float)screenHeight - 42.0f), IM_COL32(255, 255, 0, 255), line1);
        DrawOutlinedText(drawList, ImVec2(10.0f, (float)screenHeight - 22.0f), IM_COL32(255, 255, 0, 255), line2);
        if (g_Esp.showDebug) {
            int localArmor = -1;
            if (IsValidPtr(localPawn)) {
                // attempt to read armor directly from the pawn to help validate offsets at runtime
                localArmor = mem.Read<int>(localPawn + Offsets::m_ArmorValue);
            }
            char line3[256];
            snprintf(line3, sizeof(line3), "offsets: m_ArmorValue=0x%llX showArmorText=%d showArmorBar=%d localArmor=%d",
                (unsigned long long)Offsets::m_ArmorValue,
                g_Esp.showArmorText ? 1 : 0,
                g_Esp.showArmorBar ? 1 : 0,
                localArmor);
            DrawOutlinedText(drawList, ImVec2(10.0f, (float)screenHeight - 62.0f), IM_COL32(255, 200, 50, 255), line3);
        }
    }
}

// --- WIN32 AND DIRECTX INFRASTRUCTURE BOILERPLATE ---
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}
// Declaracion del handler de ImGui para Win32
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Dejar que ImGui procese primero los mensajes; si los consume, devolver 1
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int main() {
    // Attempt tracking attachment hook setup
    std::cout << "[+] Looking for CS2.exe process...\n";
    while (!mem.Attach(L"cs2.exe")) {
        Sleep(1000);
    }
    std::cout << "[+] Process attached. Client.dll address captured: 0x" << std::hex << mem.clientModule << "\n";

    // Attempt to load offsets overrides from a file in the executable folder.
    // Look for "offsets.ini" or "offsets.txt" in the current working directory.
    LoadOffsetsFromFile("offsets.ini");
    LoadOffsetsFromFile("offsets.txt");
    LoadOffsetsFromJSON("offsets.json");
    LoadEspConfig("esp_config.ini");
    PrintLoadedOffsets();

    // Obtain current system monitor resolution bounds setup
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Register win32 transparency window properties wrapper
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"OverlayClass", nullptr };
    RegisterClassExW(&wc);

    // Setup Extended Layered Transparent Engine Styling Rules
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        L"OverlayClass", L"External Overlay Canvas",
        WS_POPUP,
        0, 0, screenWidth, screenHeight,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    g_OverlayHwnd = hwnd;

    // Alpha compositing (COLORKEY makes ImGui black panels invisible)
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = screenWidth;
    sd.BufferDesc.Height = screenHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    CreateRenderTarget();

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ApplyOverlayUiStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    ImGui_ImplWin32_EnableAlphaCompositing(hwnd);

    bool running = true;
    bool insertWasDown = false;
    MSG msg;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        PollMenuKeyBind();
        bool menuKeyDown = (GetAsyncKeyState(g_App.menuToggleVk) & 0x8000) != 0;
        if (menuKeyDown && !insertWasDown)
            g_MenuOpen = !g_MenuOpen;
        insertWasDown = menuKeyDown;

        // Start the ImGui Frame loop
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        static bool menuInputActive = false;
        if (g_MenuOpen)
            RenderEspMenu();
        if (g_MenuOpen != menuInputActive) {
            SetMenuInputMode(g_MenuOpen);
            menuInputActive = g_MenuOpen;
        }

        // Execution of the processing stage loops
        RenderESP(screenWidth, screenHeight);

        // Frame clean rendering presentation pass
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // Cleared alpha keeps transparent backing visible
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync enabled lock synchronization
    }

    SetMenuInputMode(false);

    // Pipeline Destruction Stage Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupRenderTarget();
    g_pSwapChain->Release();
    g_pd3dDeviceContext->Release();
    g_pd3dDevice->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}