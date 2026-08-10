// ============================================================
// Offsets.cpp
// Contains memory offsets for the CS2 game. These addresses change when the game updates and are used to read game data from memory.
// ============================================================

#include "Offsets.h"
#include <cstdio>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdarg>
#include <cctype>
#include <vector>
#include <windows.h>

namespace Offsets {
    uintptr_t dwEntityList = 0x254DE50;
    uintptr_t dwGameEntitySystemHighestEntityIndex = 0x2090;
    uintptr_t dwViewMatrix = 0x23A8340;
    uintptr_t dwViewAngles = 0x23B8C68;
    uintptr_t dwViewRender = 0x23A8398;
    uintptr_t dwWeaponC4 = 0x0;
    uintptr_t dwGlobalVars = 0x208ED60;
    uintptr_t dwLocalPlayerController = 0x237DBA0;
    uintptr_t dwLocalPlayerPawn = 0x23A3238;
    uintptr_t m_iHealth = 0x34C;
    uintptr_t m_hPlayerPawn = 0x914;
    uintptr_t m_bControllingBot = 0x908;
    uintptr_t m_bHasFemaleVoice = 0x14D0;
    uintptr_t m_iPawnHealth = 0x920;
    uintptr_t m_bPawnIsAlive = 0x91C;
    uintptr_t m_iTeamNum = 0x3E7;
    uintptr_t m_lifeState = 0x354;
    uintptr_t m_pGameSceneNode = 0x330;
    uintptr_t m_hOwnerEntity = 0x520;
    uintptr_t m_hHudModelArms = 0x1B7C;
    uintptr_t m_hMyWearables = 0x1180;
    uintptr_t m_pOwner = 0x30;
    uintptr_t m_pChild = 0x40;
    uintptr_t m_pNextSibling = 0x48;
    uintptr_t m_vecAbsOrigin = 0xC8;
    uintptr_t m_vOldOrigin = 0x13B8;
    uintptr_t m_pBoneArray = 0x1C0;
    uintptr_t m_boneStride = 0x20;
    uintptr_t m_modelState = 0x140;
    uintptr_t m_hModel = 0xA0;
    uintptr_t m_ModelName = 0xA8;
    uintptr_t m_nHitboxSet = 0x3FC;
    uintptr_t m_iConnected = 0x6EC;
    uintptr_t m_iszPlayerName = 0x6F4;
    uintptr_t m_sSanitizedPlayerName = 0x860;
    uintptr_t m_bSpotted = 0x8;
    uintptr_t m_bSpottedByMask = 0xC;
    uintptr_t m_pItemServices = 0x1210;
    uintptr_t m_bHasDefuser = 0x48;
    uintptr_t m_bHasHelmet = 0x49;
    uintptr_t m_ArmorValue = 0x1C9C;
    uintptr_t m_iIDEntIndex = 0x341C;
    uintptr_t m_fFlags = 0x3F4;
    uintptr_t m_vecAbsVelocity = 0x3F8;
    uintptr_t m_MoveType = 0x525;
    uintptr_t m_nActualMoveType = 0x526;
    uintptr_t m_flWaterLevel = 0x528;
    uintptr_t m_pWeaponServices = 0x1208;
    uintptr_t m_hActiveWeapon = 0x60;
    uintptr_t m_nSubclassID = 0x380;
    uintptr_t m_hViewmodelAttachment = 0x16B0;
    uintptr_t m_pObserverServices = 0x1220;
    uintptr_t m_iObserverMode = 0x48;
    uintptr_t m_hObserverTarget = 0x4C;
    uintptr_t m_hObserverPawn = 0x918;
    uintptr_t m_AttributeManager = 0x11A8;
    uintptr_t m_Item = 0x50;
    uintptr_t m_iItemDefinitionIndex = 0x1BA;
    uintptr_t m_iItemIDHigh = 0x1D0;
    uintptr_t m_iItemIDLow = 0x1D4;
    uintptr_t m_iItemID = 0x1C8;
    uintptr_t m_iAccountID = 0x1D8;
    uintptr_t m_iInventoryPosition = 0x1DC;
    uintptr_t m_iEntityQuality = 0x1BC;
    uintptr_t m_iEntityLevel = 0x1C0;
    uintptr_t m_iEntityQuantity = 0x1EC;
    uintptr_t m_iRarityOverride = 0x1F0;
    uintptr_t m_iQualityOverride = 0x1F4;
    uintptr_t m_bInitialized = 0x1E8;
    uintptr_t m_bDisallowSOC = 0x1E9;
    uintptr_t m_AttributeList = 0x208;
    uintptr_t m_NetworkedDynamicAttributes = 0x280;
    uintptr_t m_szCustomName = 0x2F8;
    uintptr_t m_pInventoryServices = 0x818;
    uintptr_t m_vecNetworkableLoadout = 0x40;
    uintptr_t m_unMusicID = 0x58;
    uintptr_t m_iMusicKitID = 0x950;
    uintptr_t m_iMusicKitMVPs = 0x954;
    uintptr_t m_nFallbackPaintKit = 0x1680;
    uintptr_t m_nFallbackSeed = 0x1684;
    uintptr_t m_flFallbackWear = 0x1688;
    uintptr_t m_nFallbackStatTrak = 0x168C;
    uintptr_t m_bNeedToReApplyGloves = 0x1685;
    uintptr_t m_EconGloves = 0x1688;
    uintptr_t m_nEconGlovesChanged = 0x1AF8;
    uintptr_t m_iClip1 = 0x1700;
    uintptr_t m_bPawnHasDefuser = 0x928;
    uintptr_t m_bIsScoped = 0x1C70;
    uintptr_t m_zoomLevel = 0x1CE0;
    uintptr_t m_vecViewOffset = 0xE78;
    uintptr_t m_bThrowAnimating = 0x1CE5;
    uintptr_t m_fThrowTime = 0x1CE8;
    uintptr_t m_flThrowStrength = 0x1CF0;
    uintptr_t m_entitySpottedState = 0x11B0;

    uintptr_t m_Glow = 0xDE0;
    uintptr_t m_flGlowBackfaceMult = 0xE38;
    uintptr_t glow_m_fGlowColor = 0x8;
    uintptr_t glow_m_iGlowType = 0x30;
    uintptr_t glow_m_iGlowTeam = 0x34;
    uintptr_t glow_m_nGlowRange = 0x38;
    uintptr_t glow_m_nGlowRangeMin = 0x3C;
    uintptr_t glow_m_glowColorOverride = 0x40;
    uintptr_t glow_m_bFlashing = 0x44;
    uintptr_t glow_m_flGlowTime = 0x48;
    uintptr_t glow_m_flGlowStartTime = 0x4C;
    uintptr_t glow_m_bGlowing = 0x51;
    uintptr_t glow_m_bEligibleForScreenHighlight = 0x50;
    // Flash / Smoke overlays (per-player pawn fields)
    uintptr_t m_flFlashOverlayAlpha = 0x141C;
    uintptr_t m_flFlashMaxAlpha = 0x1424;
    uintptr_t m_flFlashDuration = 0x1428;
    uintptr_t m_flLastSmokeOverlayAlpha = 0x1448;
    uintptr_t m_flFlashedAmount = 0x480;
    uintptr_t m_bFlashing = 0x44;
    uintptr_t m_flFlashBangTime = 0x1414;
    uintptr_t m_flFlashScreenshotAlpha = 0x1418;
    uintptr_t m_flLastSpawnTimeIndex = 0x1404;
    uintptr_t m_bFlashBuildUp = 0x1420;
    uintptr_t m_bFlashDspHasBeenCleared = 0x1421;
    uintptr_t m_bFlashScreenshotHasBeenGrabbed = 0x1422;
    uintptr_t m_pEntityIdentity = 0x10;
    uintptr_t m_designerName = 0x20;
    uintptr_t m_vSmokeColor = 0x1284;
    // Bomb / C4 pointers and members
    uintptr_t dwPlantedC4 = 0x0;
    uintptr_t m_bBombTicking = 0x11A0;
    uintptr_t m_nBombSite = 0x11A4;
    uintptr_t m_bBombDefused = 0x11F4;
    uintptr_t m_hBombDefuser = 0x11F8;
    uintptr_t m_pBombDefuser = 0x16DC;
    uintptr_t m_bBombPlanted = 0x8C7;
    uintptr_t m_bBombDropped = 0x9A8;
    uintptr_t m_bombsiteCenterA = 0x648;
    uintptr_t m_bombsiteCenterB = 0x654;
    uintptr_t m_flBombRadius = 0x604;
}

static void ApplyOffsetKey(const std::string& ks, uintptr_t v) {
    if (ks == "dwEntityList") Offsets::dwEntityList = v;
    else if (ks == "dwGameEntitySystem_highestEntityIndex") Offsets::dwGameEntitySystemHighestEntityIndex = v;
    else if (ks == "dwViewMatrix") Offsets::dwViewMatrix = v;
    else if (ks == "dwViewAngles") Offsets::dwViewAngles = v;
    else if (ks == "dwViewRender") Offsets::dwViewRender = v;
    else if (ks == "dwGlobalVars") Offsets::dwGlobalVars = v;
    else if (ks == "dwLocalPlayerController") Offsets::dwLocalPlayerController = v;
    else if (ks == "dwLocalPlayerPawn") Offsets::dwLocalPlayerPawn = v;
    else if (ks == "dwWeaponC4") Offsets::dwWeaponC4 = v;
    else if (ks == "m_iHealth") Offsets::m_iHealth = v;
    else if (ks == "m_hPlayerPawn") Offsets::m_hPlayerPawn = v;
    else if (ks == "m_bControllingBot") Offsets::m_bControllingBot = v;
    else if (ks == "m_bHasFemaleVoice") Offsets::m_bHasFemaleVoice = v;
    else if (ks == "m_iPawnHealth") Offsets::m_iPawnHealth = v;
    else if (ks == "m_bPawnIsAlive") Offsets::m_bPawnIsAlive = v;
    else if (ks == "m_iTeamNum") Offsets::m_iTeamNum = v;
    else if (ks == "m_lifeState") Offsets::m_lifeState = v;
    else if (ks == "m_pGameSceneNode") Offsets::m_pGameSceneNode = v;
    else if (ks == "m_hOwnerEntity") Offsets::m_hOwnerEntity = v;
    else if (ks == "m_hHudModelArms") Offsets::m_hHudModelArms = v;
    else if (ks == "m_hMyWearables") Offsets::m_hMyWearables = v;
    else if (ks == "m_pOwner") Offsets::m_pOwner = v;
    else if (ks == "m_pChild") Offsets::m_pChild = v;
    else if (ks == "m_pNextSibling") Offsets::m_pNextSibling = v;
    else if (ks == "m_vecAbsOrigin") Offsets::m_vecAbsOrigin = v;
    else if (ks == "m_vOldOrigin") Offsets::m_vOldOrigin = v;
    else if (ks == "m_pBoneArray") Offsets::m_pBoneArray = v;
    else if (ks == "m_boneStride") Offsets::m_boneStride = v;
    else if (ks == "m_modelState") Offsets::m_modelState = v;
    else if (ks == "m_hModel") Offsets::m_hModel = v;
    else if (ks == "m_ModelName") Offsets::m_ModelName = v;
    else if (ks == "m_nHitboxSet") Offsets::m_nHitboxSet = v;
    else if (ks == "m_iConnected") Offsets::m_iConnected = v;
    else if (ks == "m_iszPlayerName") Offsets::m_iszPlayerName = v;
    else if (ks == "m_sSanitizedPlayerName") Offsets::m_sSanitizedPlayerName = v;
    else if (ks == "m_bSpotted") Offsets::m_bSpotted = v;
    else if (ks == "m_bSpottedByMask") Offsets::m_bSpottedByMask = v;
    else if (ks == "m_pItemServices") Offsets::m_pItemServices = v;
    else if (ks == "m_bHasDefuser") Offsets::m_bHasDefuser = v;
    else if (ks == "m_bHasHelmet") Offsets::m_bHasHelmet = v;
    else if (ks == "m_ArmorValue") Offsets::m_ArmorValue = v;
    else if (ks == "m_iIDEntIndex") Offsets::m_iIDEntIndex = v;
    else if (ks == "m_fFlags") Offsets::m_fFlags = v;
    else if (ks == "m_pWeaponServices") Offsets::m_pWeaponServices = v;
    else if (ks == "m_hActiveWeapon") Offsets::m_hActiveWeapon = v;
    else if (ks == "m_nSubclassID") Offsets::m_nSubclassID = v;
    else if (ks == "m_hViewmodelAttachment") Offsets::m_hViewmodelAttachment = v;
    else if (ks == "m_pObserverServices") Offsets::m_pObserverServices = v;
    else if (ks == "m_iObserverMode") Offsets::m_iObserverMode = v;
    else if (ks == "m_hObserverTarget") Offsets::m_hObserverTarget = v;
    else if (ks == "m_hObserverPawn") Offsets::m_hObserverPawn = v;
    else if (ks == "m_AttributeManager") Offsets::m_AttributeManager = v;
    else if (ks == "m_Item") Offsets::m_Item = v;
    else if (ks == "m_iItemDefinitionIndex") Offsets::m_iItemDefinitionIndex = v;
    else if (ks == "m_iItemIDHigh") Offsets::m_iItemIDHigh = v;
    else if (ks == "m_iItemIDLow") Offsets::m_iItemIDLow = v;
    else if (ks == "m_iItemID") Offsets::m_iItemID = v;
    else if (ks == "m_iAccountID") Offsets::m_iAccountID = v;
    else if (ks == "m_iInventoryPosition") Offsets::m_iInventoryPosition = v;
    else if (ks == "m_iEntityQuality") Offsets::m_iEntityQuality = v;
    else if (ks == "m_iEntityLevel") Offsets::m_iEntityLevel = v;
    else if (ks == "m_iEntityQuantity") Offsets::m_iEntityQuantity = v;
    else if (ks == "m_iRarityOverride") Offsets::m_iRarityOverride = v;
    else if (ks == "m_iQualityOverride") Offsets::m_iQualityOverride = v;
    else if (ks == "m_bInitialized") Offsets::m_bInitialized = v;
    else if (ks == "m_bDisallowSOC") Offsets::m_bDisallowSOC = v;
    else if (ks == "m_AttributeList") Offsets::m_AttributeList = v;
    else if (ks == "m_NetworkedDynamicAttributes") Offsets::m_NetworkedDynamicAttributes = v;
    else if (ks == "m_szCustomName") Offsets::m_szCustomName = v;
    else if (ks == "m_pInventoryServices") Offsets::m_pInventoryServices = v;
    else if (ks == "m_vecNetworkableLoadout") Offsets::m_vecNetworkableLoadout = v;
    else if (ks == "m_unMusicID") Offsets::m_unMusicID = v;
    else if (ks == "m_iMusicKitID") Offsets::m_iMusicKitID = v;
    else if (ks == "m_iMusicKitMVPs") Offsets::m_iMusicKitMVPs = v;
    else if (ks == "m_nFallbackPaintKit") Offsets::m_nFallbackPaintKit = v;
    else if (ks == "m_nFallbackSeed") Offsets::m_nFallbackSeed = v;
    else if (ks == "m_flFallbackWear") Offsets::m_flFallbackWear = v;
    else if (ks == "m_nFallbackStatTrak") Offsets::m_nFallbackStatTrak = v;
    else if (ks == "m_bNeedToReApplyGloves") Offsets::m_bNeedToReApplyGloves = v;
    else if (ks == "m_EconGloves") Offsets::m_EconGloves = v;
    else if (ks == "m_nEconGlovesChanged") Offsets::m_nEconGlovesChanged = v;
    else if (ks == "m_iClip1") Offsets::m_iClip1 = v;
    else if (ks == "m_bPawnHasDefuser") Offsets::m_bPawnHasDefuser = v;
    else if (ks == "m_bIsScoped") Offsets::m_bIsScoped = v;
    else if (ks == "m_zoomLevel") Offsets::m_zoomLevel = v;
    else if (ks == "m_vecViewOffset") Offsets::m_vecViewOffset = v;
    else if (ks == "m_bThrowAnimating") Offsets::m_bThrowAnimating = v;
    else if (ks == "m_fThrowTime") Offsets::m_fThrowTime = v;
    else if (ks == "m_flThrowStrength") Offsets::m_flThrowStrength = v;
    else if (ks == "m_entitySpottedState") Offsets::m_entitySpottedState = v;
    else if (ks == "m_Glow") Offsets::m_Glow = v;
    else if (ks == "m_flGlowBackfaceMult") Offsets::m_flGlowBackfaceMult = v;
    else if (ks == "glow_m_fGlowColor") Offsets::glow_m_fGlowColor = v;
    else if (ks == "glow_m_iGlowType") Offsets::glow_m_iGlowType = v;
    else if (ks == "glow_m_iGlowTeam") Offsets::glow_m_iGlowTeam = v;
    else if (ks == "glow_m_nGlowRange") Offsets::glow_m_nGlowRange = v;
    else if (ks == "glow_m_nGlowRangeMin") Offsets::glow_m_nGlowRangeMin = v;
    else if (ks == "glow_m_glowColorOverride") Offsets::glow_m_glowColorOverride = v;
    else if (ks == "glow_m_bFlashing") Offsets::glow_m_bFlashing = v;
    else if (ks == "glow_m_flGlowTime") Offsets::glow_m_flGlowTime = v;
    else if (ks == "glow_m_flGlowStartTime") Offsets::glow_m_flGlowStartTime = v;
    else if (ks == "glow_m_bGlowing") Offsets::glow_m_bGlowing = v;
    else if (ks == "glow_m_bEligibleForScreenHighlight") Offsets::glow_m_bEligibleForScreenHighlight = v;
    else if (ks == "m_flFlashOverlayAlpha") Offsets::m_flFlashOverlayAlpha = v;
    else if (ks == "m_flFlashMaxAlpha") Offsets::m_flFlashMaxAlpha = v;
    else if (ks == "m_flFlashDuration") Offsets::m_flFlashDuration = v;
    else if (ks == "m_flLastSmokeOverlayAlpha") Offsets::m_flLastSmokeOverlayAlpha = v;
    else if (ks == "m_flFlashedAmount") Offsets::m_flFlashedAmount = v;
    else if (ks == "m_bFlashing") Offsets::m_bFlashing = v;
    else if (ks == "dwPlantedC4") Offsets::dwPlantedC4 = v;
    else if (ks == "m_bBombTicking") Offsets::m_bBombTicking = v;
    else if (ks == "m_nBombSite") Offsets::m_nBombSite = v;
    else if (ks == "m_bBombDefused") Offsets::m_bBombDefused = v;
    else if (ks == "m_hBombDefuser") Offsets::m_hBombDefuser = v;
    else if (ks == "m_pBombDefuser") Offsets::m_pBombDefuser = v;
    else if (ks == "m_bBombPlanted") Offsets::m_bBombPlanted = v;
    else if (ks == "m_bBombDropped") Offsets::m_bBombDropped = v;
    else if (ks == "m_bombsiteCenterA") Offsets::m_bombsiteCenterA = v;
    else if (ks == "m_bombsiteCenterB") Offsets::m_bombsiteCenterB = v;
    else if (ks == "m_flBombRadius") Offsets::m_flBombRadius = v;
}

void LoadOffsetsFromFile(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
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
            ApplyOffsetKey(key, v);
        }
    }
    fclose(f);
}

void LoadOffsetsFromJSON(const char* path) {
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

    auto parseValue = [&](const char* key) -> bool {
        std::string pattern = std::string("\"") + key + "\"";
        size_t pos = buf.find(pattern);
        if (pos == std::string::npos) return false;
        size_t colon = buf.find(':', pos + pattern.size());
        if (colon == std::string::npos) return false;
        size_t start = colon + 1;
        while (start < buf.size() && (buf[start] == ' ' || buf[start] == '\"' || buf[start] == '\t')) ++start;
        size_t end = start;
        if (start < buf.size() && buf[start] == '0' && (start + 1 < buf.size()) && (buf[start + 1] == 'x' || buf[start + 1] == 'X')) {
            end = start + 2;
            while (end < buf.size() && isxdigit((unsigned char)buf[end])) ++end;
        } else {
            if (buf[start] == '"') ++start;
            while (end < buf.size() && (buf[end] == '-' || isdigit((unsigned char)buf[end]))) ++end;
        }
        if (end <= start) return false;
        std::string token = buf.substr(start, end - start);
        uintptr_t v = 0;
        if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0) {
            sscanf_s(token.c_str() + 2, "%llx", (unsigned long long*)&v);
        } else {
            sscanf_s(token.c_str(), "%llu", (unsigned long long*)&v);
        }
        ApplyOffsetKey(key, v);
        return true;
    };

    const char* keys[] = {
        "dwEntityList", "dwGameEntitySystem_highestEntityIndex", "dwViewMatrix", "dwViewAngles", "dwViewRender", "dwGlobalVars", "dwLocalPlayerController", "dwLocalPlayerPawn", "dwWeaponC4",
        "m_hPlayerPawn", "m_bControllingBot", "m_bHasFemaleVoice", "m_iPawnHealth", "m_bPawnIsAlive", "m_iTeamNum", "m_iHealth", "m_lifeState", "m_pGameSceneNode", "m_hOwnerEntity", "m_hHudModelArms", "m_hMyWearables", "m_pOwner", "m_pChild", "m_pNextSibling", "m_vecAbsOrigin", "m_vOldOrigin", "m_pBoneArray", "m_boneStride", "m_modelState", "m_hModel", "m_ModelName", "m_nHitboxSet",
        "m_iConnected", "m_iszPlayerName", "m_sSanitizedPlayerName", "m_entitySpottedState", "m_bSpotted", "m_bSpottedByMask",
        "m_pItemServices", "m_bHasDefuser", "m_bHasHelmet", "m_ArmorValue", "m_iIDEntIndex", "m_fFlags", "m_pWeaponServices", "m_hActiveWeapon", "m_nSubclassID", "m_hViewmodelAttachment",
        "m_pObserverServices", "m_iObserverMode", "m_hObserverTarget", "m_hObserverPawn",
        "m_AttributeManager", "m_Item", "m_iItemDefinitionIndex", "m_iItemIDHigh",
        "m_nFallbackPaintKit", "m_nFallbackSeed", "m_flFallbackWear", "m_nFallbackStatTrak", "m_bNeedToReApplyGloves", "m_EconGloves", "m_nEconGlovesChanged", "m_iClip1",
        "m_bPawnHasDefuser", "m_bIsScoped", "m_zoomLevel", "m_vecViewOffset", "m_bThrowAnimating", "m_fThrowTime", "m_flThrowStrength",
        "m_Glow", "m_flGlowBackfaceMult", "glow_m_fGlowColor", "glow_m_iGlowType", "glow_m_iGlowTeam", "glow_m_nGlowRange",
        "glow_m_nGlowRangeMin", "glow_m_glowColorOverride", "glow_m_bFlashing", "glow_m_flGlowTime", "glow_m_flGlowStartTime", "glow_m_bGlowing", "glow_m_bEligibleForScreenHighlight",
        "m_flFlashOverlayAlpha", "m_flFlashMaxAlpha", "m_flFlashDuration", "m_flLastSmokeOverlayAlpha", "m_flFlashedAmount", "m_bFlashing",
        "dwPlantedC4", "m_bBombTicking", "m_nBombSite", "m_bBombDefused", "m_hBombDefuser", "m_pBombDefuser",
        "m_bBombPlanted", "m_bBombDropped", "m_bombsiteCenterA", "m_bombsiteCenterB", "m_flBombRadius"
    };
    for (auto k : keys) parseValue(k);
}

void LoadClientSchemaOffsetsFromJSON(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return;
    }

    std::string json(static_cast<size_t>(size), '\0');
    fread(json.data(), 1, json.size(), f);
    fclose(f);

    auto parseClassField = [&](const char* className, const char* fieldName, uintptr_t& destination) -> bool {
        const std::string classToken = std::string("\n      \"") + className + "\": {";
        const size_t classPos = json.find(classToken);
        if (classPos == std::string::npos) return false;
        const size_t nextClass = json.find("\n      \"", classPos + classToken.size());

        const std::string fieldToken = std::string("\"") + fieldName + "\"";
        const size_t fieldPos = json.find(fieldToken, classPos + classToken.size());
        if (fieldPos == std::string::npos || (nextClass != std::string::npos && fieldPos >= nextClass)) return false;
        const size_t colon = json.find(':', fieldPos + fieldToken.size());
        if (colon == std::string::npos) return false;

        size_t start = colon + 1;
        while (start < json.size() && (json[start] == ' ' || json[start] == '\t' || json[start] == '"')) ++start;
        size_t end = start;
        while (end < json.size() && isdigit(static_cast<unsigned char>(json[end]))) ++end;
        if (end <= start) return false;

        unsigned long long value = 0;
        if (sscanf_s(json.substr(start, end - start).c_str(), "%llu", &value) == 1) {
            destination = static_cast<uintptr_t>(value);
            return true;
        }
        return false;
    };

    parseClassField("CCSPlayerController", "m_hPlayerPawn", Offsets::m_hPlayerPawn);
    parseClassField("CCSPlayerController", "m_bControllingBot", Offsets::m_bControllingBot);
    parseClassField("CCSPlayerController", "m_iPawnHealth", Offsets::m_iPawnHealth);
    parseClassField("CCSPlayerController", "m_bPawnIsAlive", Offsets::m_bPawnIsAlive);
    parseClassField("CCSPlayerController", "m_sSanitizedPlayerName", Offsets::m_sSanitizedPlayerName);
    parseClassField("CBasePlayerController", "m_iConnected", Offsets::m_iConnected);
    parseClassField("CBasePlayerController", "m_iszPlayerName", Offsets::m_iszPlayerName);
    parseClassField("C_BaseEntity", "m_iTeamNum", Offsets::m_iTeamNum);
    parseClassField("C_BaseEntity", "m_iHealth", Offsets::m_iHealth);
    parseClassField("C_BaseEntity", "m_lifeState", Offsets::m_lifeState);
    parseClassField("C_BaseEntity", "m_pGameSceneNode", Offsets::m_pGameSceneNode);
    parseClassField("CGameSceneNode", "m_vecAbsOrigin", Offsets::m_vecAbsOrigin);
    parseClassField("CSkeletonInstance", "m_modelState", Offsets::m_modelState);
    parseClassField("CSkeletonInstance", "m_nHitboxSet", Offsets::m_nHitboxSet);
    parseClassField("CModelState", "m_hModel", Offsets::m_hModel);
    parseClassField("CModelState", "m_ModelName", Offsets::m_ModelName);
    parseClassField("C_BasePlayerPawn", "m_vOldOrigin", Offsets::m_vOldOrigin);
    parseClassField("C_BasePlayerPawn", "m_pItemServices", Offsets::m_pItemServices);
    parseClassField("C_BasePlayerPawn", "m_pWeaponServices", Offsets::m_pWeaponServices);
    parseClassField("C_BasePlayerPawn", "m_pObserverServices", Offsets::m_pObserverServices);
    parseClassField("CPlayer_ObserverServices", "m_iObserverMode", Offsets::m_iObserverMode);
    parseClassField("CPlayer_ObserverServices", "m_hObserverTarget", Offsets::m_hObserverTarget);
    parseClassField("CCSPlayerController", "m_hObserverPawn", Offsets::m_hObserverPawn);
    parseClassField("C_CSPlayerPawn", "m_ArmorValue", Offsets::m_ArmorValue);
    parseClassField("C_CSPlayerPawn", "m_iIDEntIndex", Offsets::m_iIDEntIndex);
    parseClassField("C_CSPlayerPawn", "m_entitySpottedState", Offsets::m_entitySpottedState);
    parseClassField("C_CSPlayerPawn", "m_bIsScoped", Offsets::m_bIsScoped);
    parseClassField("C_CSPlayerPawn", "m_bHasFemaleVoice", Offsets::m_bHasFemaleVoice);
    parseClassField("C_CSPlayerPawn", "m_bNeedToReApplyGloves", Offsets::m_bNeedToReApplyGloves);
    parseClassField("C_CSPlayerPawn", "m_EconGloves", Offsets::m_EconGloves);
    parseClassField("C_CSPlayerPawn", "m_nEconGlovesChanged", Offsets::m_nEconGlovesChanged);
    parseClassField("EntitySpottedState_t", "m_bSpotted", Offsets::m_bSpotted);
    parseClassField("EntitySpottedState_t", "m_bSpottedByMask", Offsets::m_bSpottedByMask);
    parseClassField("C_BaseEntity", "m_fFlags", Offsets::m_fFlags);
    parseClassField("CEntityInstance", "m_pEntity", Offsets::m_pEntityIdentity);
    parseClassField("CEntityIdentity", "m_designerName", Offsets::m_designerName);
    parseClassField("C_SmokeGrenadeProjectile", "m_vSmokeColor", Offsets::m_vSmokeColor);
    parseClassField("C_BaseEntity", "m_vecAbsVelocity", Offsets::m_vecAbsVelocity);
    parseClassField("C_BaseModelEntity", "m_vecViewOffset", Offsets::m_vecViewOffset);
    parseClassField("C_BaseEntity", "m_MoveType", Offsets::m_MoveType);
    parseClassField("C_BaseEntity", "m_nActualMoveType", Offsets::m_nActualMoveType);
    parseClassField("C_BaseEntity", "m_flWaterLevel", Offsets::m_flWaterLevel);
    parseClassField("C_BaseEntity", "m_hOwnerEntity", Offsets::m_hOwnerEntity);
    parseClassField("C_CSPlayerPawn", "m_hHudModelArms", Offsets::m_hHudModelArms);
    parseClassField("C_BaseCombatCharacter", "m_hMyWearables", Offsets::m_hMyWearables);
    parseClassField("CGameSceneNode", "m_pOwner", Offsets::m_pOwner);
    parseClassField("CGameSceneNode", "m_pChild", Offsets::m_pChild);
    parseClassField("CGameSceneNode", "m_pNextSibling", Offsets::m_pNextSibling);
    parseClassField("C_BaseEntity", "m_nSubclassID", Offsets::m_nSubclassID);
    parseClassField("C_EconEntity", "m_AttributeManager", Offsets::m_AttributeManager);
    parseClassField("C_EconEntity", "m_hViewmodelAttachment", Offsets::m_hViewmodelAttachment);
    parseClassField("C_AttributeContainer", "m_Item", Offsets::m_Item);
    parseClassField("C_EconItemView", "m_iItemDefinitionIndex", Offsets::m_iItemDefinitionIndex);
    parseClassField("C_EconItemView", "m_iEntityQuality", Offsets::m_iEntityQuality);
    parseClassField("C_EconItemView", "m_iEntityLevel", Offsets::m_iEntityLevel);
    parseClassField("C_EconItemView", "m_iItemID", Offsets::m_iItemID);
    parseClassField("C_EconItemView", "m_iItemIDHigh", Offsets::m_iItemIDHigh);
    parseClassField("C_EconItemView", "m_iItemIDLow", Offsets::m_iItemIDLow);
    parseClassField("C_EconItemView", "m_iAccountID", Offsets::m_iAccountID);
    parseClassField("C_EconItemView", "m_iInventoryPosition", Offsets::m_iInventoryPosition);
    parseClassField("C_EconItemView", "m_bInitialized", Offsets::m_bInitialized);
    parseClassField("C_EconItemView", "m_bDisallowSOC", Offsets::m_bDisallowSOC);
    parseClassField("C_EconItemView", "m_iEntityQuantity", Offsets::m_iEntityQuantity);
    parseClassField("C_EconItemView", "m_iRarityOverride", Offsets::m_iRarityOverride);
    parseClassField("C_EconItemView", "m_iQualityOverride", Offsets::m_iQualityOverride);
    parseClassField("C_EconItemView", "m_AttributeList", Offsets::m_AttributeList);
    parseClassField("C_EconItemView", "m_NetworkedDynamicAttributes", Offsets::m_NetworkedDynamicAttributes);
    parseClassField("C_EconItemView", "m_szCustomName", Offsets::m_szCustomName);
    parseClassField("CCSPlayerController", "m_pInventoryServices", Offsets::m_pInventoryServices);
    parseClassField("CCSPlayerController", "m_iMusicKitID", Offsets::m_iMusicKitID);
    parseClassField("CCSPlayerController", "m_iMusicKitMVPs", Offsets::m_iMusicKitMVPs);
    parseClassField("CCSPlayerController_InventoryServices", "m_vecNetworkableLoadout", Offsets::m_vecNetworkableLoadout);
    parseClassField("CCSPlayerController_InventoryServices", "m_unMusicID", Offsets::m_unMusicID);
    parseClassField("C_EconEntity", "m_nFallbackPaintKit", Offsets::m_nFallbackPaintKit);
    parseClassField("C_EconEntity", "m_nFallbackSeed", Offsets::m_nFallbackSeed);
    parseClassField("C_EconEntity", "m_flFallbackWear", Offsets::m_flFallbackWear);
    parseClassField("C_EconEntity", "m_nFallbackStatTrak", Offsets::m_nFallbackStatTrak);
    parseClassField("C_BasePlayerWeapon", "m_iClip1", Offsets::m_iClip1);
    parseClassField("CCSPlayer_ItemServices", "m_bHasDefuser", Offsets::m_bHasDefuser);
    parseClassField("CCSPlayer_ItemServices", "m_bHasHelmet", Offsets::m_bHasHelmet);
    parseClassField("CCSPlayerController", "m_bPawnHasDefuser", Offsets::m_bPawnHasDefuser);
    parseClassField("CPlayer_WeaponServices", "m_hActiveWeapon", Offsets::m_hActiveWeapon);
    parseClassField("C_CSWeaponBaseGun", "m_zoomLevel", Offsets::m_zoomLevel);
    parseClassField("C_BaseCSGrenade", "m_bThrowAnimating", Offsets::m_bThrowAnimating);
    parseClassField("C_BaseCSGrenade", "m_fThrowTime", Offsets::m_fThrowTime);
    parseClassField("C_BaseCSGrenade", "m_flThrowStrength", Offsets::m_flThrowStrength);
    parseClassField("C_BaseModelEntity", "m_Glow", Offsets::m_Glow);
    parseClassField("C_BaseModelEntity", "m_flGlowBackfaceMult", Offsets::m_flGlowBackfaceMult);
    parseClassField("CGlowProperty", "m_fGlowColor", Offsets::glow_m_fGlowColor);
    parseClassField("CGlowProperty", "m_iGlowType", Offsets::glow_m_iGlowType);
    parseClassField("CGlowProperty", "m_iGlowTeam", Offsets::glow_m_iGlowTeam);
    parseClassField("CGlowProperty", "m_nGlowRange", Offsets::glow_m_nGlowRange);
    parseClassField("CGlowProperty", "m_nGlowRangeMin", Offsets::glow_m_nGlowRangeMin);
    parseClassField("CGlowProperty", "m_glowColorOverride", Offsets::glow_m_glowColorOverride);
    parseClassField("CGlowProperty", "m_bFlashing", Offsets::glow_m_bFlashing);
    parseClassField("CGlowProperty", "m_flGlowTime", Offsets::glow_m_flGlowTime);
    parseClassField("CGlowProperty", "m_flGlowStartTime", Offsets::glow_m_flGlowStartTime);
    parseClassField("CGlowProperty", "m_bGlowing", Offsets::glow_m_bGlowing);
    parseClassField("CGlowProperty", "m_bEligibleForScreenHighlight", Offsets::glow_m_bEligibleForScreenHighlight);
    parseClassField("C_CSPlayerPawnBase", "m_flFlashOverlayAlpha", Offsets::m_flFlashOverlayAlpha);
    parseClassField("C_CSPlayerPawnBase", "m_flFlashMaxAlpha", Offsets::m_flFlashMaxAlpha);
    parseClassField("C_CSPlayerPawnBase", "m_flFlashDuration", Offsets::m_flFlashDuration);
    parseClassField("C_CSPlayerPawnBase", "m_flLastSmokeOverlayAlpha", Offsets::m_flLastSmokeOverlayAlpha);
    parseClassField("CCS2PawnGraphController", "m_flFlashedAmount", Offsets::m_flFlashedAmount);
    parseClassField("C_CSPlayerPawnBase", "m_flFlashBangTime", Offsets::m_flFlashBangTime);
    parseClassField("C_CSPlayerPawnBase", "m_flFlashScreenshotAlpha", Offsets::m_flFlashScreenshotAlpha);
    parseClassField("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex", Offsets::m_flLastSpawnTimeIndex);
    parseClassField("C_CSPlayerPawnBase", "m_bFlashBuildUp", Offsets::m_bFlashBuildUp);
    parseClassField("C_CSPlayerPawnBase", "m_bFlashDspHasBeenCleared", Offsets::m_bFlashDspHasBeenCleared);
    parseClassField("C_CSPlayerPawnBase", "m_bFlashScreenshotHasBeenGrabbed", Offsets::m_bFlashScreenshotHasBeenGrabbed);
    parseClassField("C_PlantedC4", "m_bBombTicking", Offsets::m_bBombTicking);
    parseClassField("C_PlantedC4", "m_nBombSite", Offsets::m_nBombSite);
    parseClassField("C_PlantedC4", "m_bBombDefused", Offsets::m_bBombDefused);
    parseClassField("C_PlantedC4", "m_hBombDefuser", Offsets::m_hBombDefuser);
    parseClassField("C_PlantedC4", "m_pBombDefuser", Offsets::m_pBombDefuser);
    parseClassField("C_CSGameRules", "m_bBombPlanted", Offsets::m_bBombPlanted);
    parseClassField("C_CSGameRules", "m_bBombDropped", Offsets::m_bBombDropped);
    parseClassField("C_CSPlayerResource", "m_bombsiteCenterA", Offsets::m_bombsiteCenterA);
    parseClassField("C_CSPlayerResource", "m_bombsiteCenterB", Offsets::m_bombsiteCenterB);
    parseClassField("CMapInfo", "m_flBombRadius", Offsets::m_flBombRadius);
}

std::vector<LoadedOffsetEntry> GetLoadedOffsetsSnapshot() {
    using Group = OffsetGroup;
    return {
        { "dwEntityList", Offsets::dwEntityList, Group::Core },
        { "dwViewMatrix", Offsets::dwViewMatrix, Group::Core },
        { "dwViewAngles", Offsets::dwViewAngles, Group::Core },
        { "dwViewRender", Offsets::dwViewRender, Group::Core },
        { "dwGlobalVars", Offsets::dwGlobalVars, Group::Core },
        { "dwLocalPlayerController", Offsets::dwLocalPlayerController, Group::Core },
        { "dwLocalPlayerPawn", Offsets::dwLocalPlayerPawn, Group::Core },
        { "dwWeaponC4", Offsets::dwWeaponC4, Group::Core },
        { "m_hPlayerPawn", Offsets::m_hPlayerPawn, Group::Core },
        { "m_bControllingBot", Offsets::m_bControllingBot, Group::Core },
        { "m_iPawnHealth", Offsets::m_iPawnHealth, Group::Core },
        { "m_bPawnIsAlive", Offsets::m_bPawnIsAlive, Group::Core },
        { "m_iTeamNum", Offsets::m_iTeamNum, Group::Core },
        { "m_iHealth", Offsets::m_iHealth, Group::Core },
        { "m_lifeState", Offsets::m_lifeState, Group::Core },
        { "m_pGameSceneNode", Offsets::m_pGameSceneNode, Group::Core },
        { "m_hOwnerEntity", Offsets::m_hOwnerEntity, Group::Core },
        { "m_vecAbsOrigin", Offsets::m_vecAbsOrigin, Group::Core },
        { "m_vOldOrigin", Offsets::m_vOldOrigin, Group::Core },
        { "m_pBoneArray", Offsets::m_pBoneArray, Group::Core },
        { "m_boneStride", Offsets::m_boneStride, Group::Core },
        { "m_modelState", Offsets::m_modelState, Group::Core },
        { "m_hModel", Offsets::m_hModel, Group::Core },
        { "m_nHitboxSet", Offsets::m_nHitboxSet, Group::Core },
        { "m_AttributeManager", Offsets::m_AttributeManager, Group::Inventory },
        { "m_Item", Offsets::m_Item, Group::Inventory },
        { "m_iItemDefinitionIndex", Offsets::m_iItemDefinitionIndex, Group::Inventory },
        { "m_iItemIDHigh", Offsets::m_iItemIDHigh, Group::Inventory },
        { "m_iAccountID", Offsets::m_iAccountID, Group::Inventory },
        { "m_iEntityQuality", Offsets::m_iEntityQuality, Group::Inventory },
        { "m_pInventoryServices", Offsets::m_pInventoryServices, Group::Inventory },
        { "m_vecNetworkableLoadout", Offsets::m_vecNetworkableLoadout, Group::Inventory },
        { "m_unMusicID", Offsets::m_unMusicID, Group::Inventory },
        { "m_iMusicKitID", Offsets::m_iMusicKitID, Group::Inventory },
        { "m_iMusicKitMVPs", Offsets::m_iMusicKitMVPs, Group::Inventory },
        { "m_nFallbackPaintKit", Offsets::m_nFallbackPaintKit, Group::Inventory },
        { "m_nFallbackSeed", Offsets::m_nFallbackSeed, Group::Inventory },
        { "m_flFallbackWear", Offsets::m_flFallbackWear, Group::Inventory },
        { "m_nFallbackStatTrak", Offsets::m_nFallbackStatTrak, Group::Inventory },
        { "m_Glow", Offsets::m_Glow, Group::Visuals },
        { "glow_m_bGlowing", Offsets::glow_m_bGlowing, Group::Visuals },
        { "m_flFlashOverlayAlpha", Offsets::m_flFlashOverlayAlpha, Group::Visuals },
        { "m_flFlashDuration", Offsets::m_flFlashDuration, Group::Visuals },
        { "m_flLastSmokeOverlayAlpha", Offsets::m_flLastSmokeOverlayAlpha, Group::Visuals },
        { "m_vSmokeColor", Offsets::m_vSmokeColor, Group::Visuals },
        { "m_bIsScoped", Offsets::m_bIsScoped, Group::Visuals },
        { "m_zoomLevel", Offsets::m_zoomLevel, Group::Visuals }
    };
}

void PrintLoadedOffsets() {
    std::cout << "Loaded Offsets:\n";
    for (const LoadedOffsetEntry& entry : GetLoadedOffsetsSnapshot()) {
        std::cout << ' ' << entry.name << "=0x" << std::hex
            << entry.value << std::dec << '\n';
    }
}

namespace {
    OffsetUpdateStatus g_offsetStatus;

    bool ReadTextFile(const char* path, std::string& text) {
        FILE* file = nullptr;
        fopen_s(&file, path, "rb");
        if (!file) return false;
        fseek(file, 0, SEEK_END);
        const long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (size <= 0) {
            fclose(file);
            return false;
        }
        text.assign(static_cast<size_t>(size), '\0');
        const bool readOk = fread(text.data(), 1, text.size(), file) == text.size();
        fclose(file);
        return readOk;
    }

    bool HasJsonValue(const std::string& json, const char* key) {
        const std::string token = std::string("\"") + key + "\"";
        const size_t position = json.find(token);
        if (position == std::string::npos) return false;
        const size_t colon = json.find(':', position + token.size());
        if (colon == std::string::npos) return false;
        size_t value = colon + 1;
        while (value < json.size() &&
            (json[value] == ' ' || json[value] == '\t' || json[value] == '\r' || json[value] == '\n' || json[value] == '"')) {
            ++value;
        }
        return value < json.size() && (std::isdigit(static_cast<unsigned char>(json[value])) || json[value] == '-');
    }

    bool HasClassField(const std::string& json, const char* className, const char* fieldName) {
        const std::string classToken = std::string("\n      \"") + className + "\": {";
        const size_t classPosition = json.find(classToken);
        if (classPosition == std::string::npos) return false;
        const size_t nextClass = json.find("\n      \"", classPosition + classToken.size());
        const std::string fieldToken = std::string("\"") + fieldName + "\"";
        const size_t fieldPosition = json.find(fieldToken, classPosition + classToken.size());
        return fieldPosition != std::string::npos &&
            (nextClass == std::string::npos || fieldPosition < nextClass);
    }

    std::string ReadBuildInfo(const char* path) {
        std::string json;
        if (!ReadTextFile(path, json)) return {};
        const std::string key = "\"build_number\"";
        const size_t keyPosition = json.find(key);
        if (keyPosition == std::string::npos) return {};
        const size_t colon = json.find(':', keyPosition + key.size());
        if (colon == std::string::npos) return {};
        size_t start = colon + 1;
        while (start < json.size() && std::isspace(static_cast<unsigned char>(json[start]))) ++start;
        size_t end = start;
        while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) ++end;
        return end > start ? json.substr(start, end - start) : std::string{};
    }

    bool ValidateOffsetPair(const char* offsetsPath, const char* schemaPath,
        int& globalsFound, int& schemaFound, std::string& error) {
        std::string offsetsJson;
        std::string schemaJson;
        if (!ReadTextFile(offsetsPath, offsetsJson)) {
            error = std::string("No se pudo leer ") + offsetsPath;
            return false;
        }
        if (!ReadTextFile(schemaPath, schemaJson)) {
            error = std::string("No se pudo leer ") + schemaPath;
            return false;
        }

        const char* requiredGlobals[] = {
            "dwEntityList", "dwViewMatrix", "dwViewAngles", "dwViewRender",
            "dwGlobalVars", "dwLocalPlayerController", "dwLocalPlayerPawn", "dwWeaponC4"
        };
        globalsFound = 0;
        std::vector<std::string> missing;
        for (const char* key : requiredGlobals) {
            if (HasJsonValue(offsetsJson, key)) ++globalsFound;
            else missing.emplace_back(key);
        }

        struct RequiredField { const char* className; const char* fieldName; };
        const RequiredField requiredSchema[] = {
            { "CCSPlayerController", "m_hPlayerPawn" },
            { "CCSPlayerController", "m_bControllingBot" },
            { "CCSPlayerController", "m_iPawnHealth" },
            { "CCSPlayerController", "m_bPawnIsAlive" },
            { "C_BaseEntity", "m_iTeamNum" },
            { "C_BaseEntity", "m_iHealth" },
            { "C_BaseEntity", "m_lifeState" },
            { "C_BaseEntity", "m_pGameSceneNode" },
            { "CGameSceneNode", "m_vecAbsOrigin" },
            { "C_BasePlayerPawn", "m_vOldOrigin" },
            { "C_BasePlayerPawn", "m_pItemServices" },
            { "C_BasePlayerPawn", "m_pWeaponServices" },
            { "C_CSPlayerPawn", "m_ArmorValue" },
            { "C_CSPlayerPawn", "m_bHasFemaleVoice" },
            { "C_CSPlayerPawn", "m_bNeedToReApplyGloves" },
            { "C_CSPlayerPawn", "m_EconGloves" },
            { "C_CSPlayerPawn", "m_nEconGlovesChanged" },
            { "C_BaseCombatCharacter", "m_hMyWearables" },
            { "C_CSPlayerPawn", "m_entitySpottedState" },
            { "EntitySpottedState_t", "m_bSpotted" },
            { "CPlayer_WeaponServices", "m_hActiveWeapon" },
            { "C_EconEntity", "m_AttributeManager" },
            { "C_AttributeContainer", "m_Item" },
            { "C_EconItemView", "m_iItemDefinitionIndex" },
            { "CCSPlayerController", "m_iMusicKitMVPs" },
            { "C_BasePlayerWeapon", "m_iClip1" },
            { "CEntityInstance", "m_pEntity" },
            { "CEntityIdentity", "m_designerName" },
            { "C_BaseModelEntity", "m_Glow" },
            { "CGlowProperty", "m_bGlowing" },
            { "C_PlantedC4", "m_bBombTicking" },
            { "C_PlantedC4", "m_nBombSite" },
            { "C_PlantedC4", "m_bBombDefused" },
            { "C_CSGameRules", "m_bBombPlanted" },
            { "C_CSPlayerPawnBase", "m_flFlashDuration" },
            { "C_SmokeGrenadeProjectile", "m_vSmokeColor" }
        };
        schemaFound = 0;
        for (const RequiredField& field : requiredSchema) {
            if (HasClassField(schemaJson, field.className, field.fieldName)) ++schemaFound;
            else missing.emplace_back(std::string(field.className) + "::" + field.fieldName);
        }

        if (!missing.empty()) {
            error = "Salida incompleta. Falta: ";
            const size_t shown = missing.size() < 4 ? missing.size() : 4;
            for (size_t index = 0; index < shown; ++index) {
                if (index != 0) error += ", ";
                error += missing[index];
            }
            if (missing.size() > shown)
                error += " y " + std::to_string(missing.size() - shown) + " mas";
            return false;
        }
        return true;
    }

    bool LoadValidatedPair(const char* offsetsPath, const char* schemaPath,
        const char* infoPath, const char* source, bool updated) {
        int globalsFound = 0;
        int schemaFound = 0;
        std::string error;
        if (!ValidateOffsetPair(offsetsPath, schemaPath, globalsFound, schemaFound, error)) {
            g_offsetStatus.message = error;
            return false;
        }

        LoadOffsetsFromJSON(offsetsPath);
        LoadClientSchemaOffsetsFromJSON(schemaPath);
        g_offsetStatus.loaded = true;
        g_offsetStatus.updatedThisRun = updated;
        g_offsetStatus.globalOffsetsFound = globalsFound;
        g_offsetStatus.schemaOffsetsFound = schemaFound;
        g_offsetStatus.source = source;
        g_offsetStatus.build = ReadBuildInfo(infoPath);
        g_offsetStatus.message = updated
            ? "Dumper ejecutado y offsets validados correctamente."
            : "Offsets validados cargados desde la cache.";
        return true;
    }

    bool PublishStagedOutput() {
        CreateDirectoryA("output", nullptr);
        const char* files[] = { "offsets.json", "client_dll.json", "info.json" };
        for (const char* file : files) {
            const std::string source = std::string("offsets_staging\\") + file;
            const std::string temporary = std::string("output\\") + file + ".new";
            if (!CopyFileA(source.c_str(), temporary.c_str(), FALSE)) {
                if (std::string(file) == "info.json") continue;
                g_offsetStatus.message = std::string("No se pudo preparar output\\") + file;
                return false;
            }
        }
        for (const char* file : files) {
            const std::string temporary = std::string("output\\") + file + ".new";
            const std::string destination = std::string("output\\") + file;
            if (GetFileAttributesA(temporary.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            if (!MoveFileExA(temporary.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                g_offsetStatus.message = std::string("No se pudo publicar output\\") + file;
                return false;
            }
        }
        return true;
    }
}

const OffsetUpdateStatus& GetOffsetUpdateStatus() {
    return g_offsetStatus;
}

bool ReloadOffsetsFromOutput() {
    return LoadValidatedPair("output\\offsets.json", "output\\client_dll.json",
        "output\\info.json", "output (cache validada)", false);
}

bool RunOffsetAutoUpdate() {
    const char* dumperPath = "cs2-dumper.exe";
    g_offsetStatus.dumperFound = GetFileAttributesA(dumperPath) != INVALID_FILE_ATTRIBUTES;
    if (!g_offsetStatus.dumperFound) {
        g_offsetStatus.message = "No se encontro cs2-dumper.exe junto a OverlayAI.exe.";
        return false;
    }

    CreateDirectoryA("offsets_staging", nullptr);
    DeleteFileA("offsets_staging\\offsets.json");
    DeleteFileA("offsets_staging\\client_dll.json");
    DeleteFileA("offsets_staging\\info.json");

    std::string command = "\"cs2-dumper.exe\" --file-types json --output offsets_staging";
    std::vector<char> commandLine(command.begin(), command.end());
    commandLine.push_back('\0');
    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo)) {
        g_offsetStatus.message = "No se pudo iniciar cs2-dumper.exe.";
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 30000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(processInfo.hProcess, 1);
        g_offsetStatus.message = "cs2-dumper excedio el limite de 30 segundos.";
    } else {
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    if (waitResult != WAIT_OBJECT_0 || exitCode != 0) {
        if (waitResult == WAIT_OBJECT_0)
            g_offsetStatus.message = "cs2-dumper termino con codigo " + std::to_string(exitCode) + ".";
        return false;
    }

    int globalsFound = 0;
    int schemaFound = 0;
    std::string validationError;
    if (!ValidateOffsetPair("offsets_staging\\offsets.json", "offsets_staging\\client_dll.json",
        globalsFound, schemaFound, validationError)) {
        g_offsetStatus.message = "Dumper rechazado: " + validationError;
        return false;
    }
    if (!PublishStagedOutput()) return false;
    return LoadValidatedPair("output\\offsets.json", "output\\client_dll.json",
        "output\\info.json", "cs2-dumper automatico", true);
}

void InitializeOffsetSystem() {
    if (RunOffsetAutoUpdate()) return;

    const std::string updateError = g_offsetStatus.message;
    if (ReloadOffsetsFromOutput()) {
        if (!updateError.empty())
            g_offsetStatus.message = updateError + " Usando la cache validada.";
        return;
    }

    // Last-resort compatibility with the previous manual system.
    if (GetFileAttributesA("offsets.json") != INVALID_FILE_ATTRIBUTES) {
        LoadOffsetsFromJSON("offsets.json");
        LoadClientSchemaOffsetsFromJSON("client_dll.json");
        g_offsetStatus.loaded = true;
        g_offsetStatus.source = "offsets.json legado (sin validar)";
        g_offsetStatus.message = "No hubo un par valido; se uso el archivo manual legado.";
    } else {
        LoadOffsetsFromFile("offsets.ini");
        LoadOffsetsFromFile("offsets.txt");
        g_offsetStatus.loaded = false;
        g_offsetStatus.source = "valores integrados / INI legado";
        g_offsetStatus.message = "No se encontraron offsets JSON validos.";
    }
}
