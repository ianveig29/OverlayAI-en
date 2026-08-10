#pragma once

// ============================================================
// Offsets.h
// Game memory offset declarations.
// ============================================================

#include <cstdint>
#include <string>
#include <vector>

namespace Offsets {
    extern uintptr_t dwEntityList;
    extern uintptr_t dwGameEntitySystemHighestEntityIndex;
    extern uintptr_t dwViewMatrix;
    extern uintptr_t dwViewAngles;
    extern uintptr_t dwViewRender;
    extern uintptr_t dwPlantedC4;
    extern uintptr_t dwGlobalVars;
    extern uintptr_t dwLocalPlayerController;
    extern uintptr_t dwLocalPlayerPawn;
    extern uintptr_t dwWeaponC4;

    // Bomb / C4
    extern uintptr_t m_bBombTicking;
    extern uintptr_t m_nBombSite;
    extern uintptr_t m_bBombDefused;
    extern uintptr_t m_hBombDefuser;
    extern uintptr_t m_pBombDefuser;
    extern uintptr_t m_bBombPlanted;
    extern uintptr_t m_bBombDropped;
    extern uintptr_t m_bombsiteCenterA;
    extern uintptr_t m_bombsiteCenterB;
    extern uintptr_t m_flBombRadius;

    extern uintptr_t m_hPlayerPawn;
    extern uintptr_t m_bControllingBot;
    extern uintptr_t m_bHasFemaleVoice;
    extern uintptr_t m_iPawnHealth;
    extern uintptr_t m_bPawnIsAlive;
    extern uintptr_t m_iTeamNum;
    extern uintptr_t m_iHealth;
    extern uintptr_t m_lifeState;
    extern uintptr_t m_pGameSceneNode;
    extern uintptr_t m_hOwnerEntity;
    extern uintptr_t m_hHudModelArms;
    extern uintptr_t m_hMyWearables;
    extern uintptr_t m_pOwner;
    extern uintptr_t m_pChild;
    extern uintptr_t m_pNextSibling;
    extern uintptr_t m_vecAbsOrigin;
    extern uintptr_t m_vOldOrigin;
    extern uintptr_t m_pBoneArray;
    extern uintptr_t m_boneStride;
    extern uintptr_t m_modelState;
    extern uintptr_t m_hModel;
    extern uintptr_t m_ModelName;
    extern uintptr_t m_nHitboxSet;
    extern uintptr_t m_iConnected;
    extern uintptr_t m_iszPlayerName;
    extern uintptr_t m_sSanitizedPlayerName;
    extern uintptr_t m_entitySpottedState;
    extern uintptr_t m_bSpotted;
    extern uintptr_t m_bSpottedByMask;
    extern uintptr_t m_pItemServices;
    extern uintptr_t m_bHasDefuser;
    extern uintptr_t m_bHasHelmet;
    extern uintptr_t m_ArmorValue;
    extern uintptr_t m_iIDEntIndex;
    extern uintptr_t m_fFlags;
    extern uintptr_t m_vecAbsVelocity;
    extern uintptr_t m_MoveType;
    extern uintptr_t m_nActualMoveType;
    extern uintptr_t m_flWaterLevel;
    extern uintptr_t m_pWeaponServices;
    extern uintptr_t m_hActiveWeapon;
    extern uintptr_t m_nSubclassID;
    extern uintptr_t m_hViewmodelAttachment;
    extern uintptr_t m_pObserverServices;
    extern uintptr_t m_iObserverMode;
    extern uintptr_t m_hObserverTarget;
    extern uintptr_t m_hObserverPawn;
    extern uintptr_t m_AttributeManager;
    extern uintptr_t m_Item;
    extern uintptr_t m_iItemDefinitionIndex;
    extern uintptr_t m_iItemIDHigh;
    extern uintptr_t m_iItemIDLow;
    extern uintptr_t m_iItemID;
    extern uintptr_t m_iAccountID;
    extern uintptr_t m_iInventoryPosition;
    extern uintptr_t m_iEntityQuality;
    extern uintptr_t m_iEntityLevel;
    extern uintptr_t m_iEntityQuantity;
    extern uintptr_t m_iRarityOverride;
    extern uintptr_t m_iQualityOverride;
    extern uintptr_t m_bInitialized;
    extern uintptr_t m_bDisallowSOC;
    extern uintptr_t m_AttributeList;
    extern uintptr_t m_NetworkedDynamicAttributes;
    extern uintptr_t m_szCustomName;
    extern uintptr_t m_pInventoryServices;
    extern uintptr_t m_vecNetworkableLoadout;
    extern uintptr_t m_unMusicID;
    extern uintptr_t m_iMusicKitID;
    extern uintptr_t m_iMusicKitMVPs;
    extern uintptr_t m_nFallbackPaintKit;
    extern uintptr_t m_nFallbackSeed;
    extern uintptr_t m_flFallbackWear;
    extern uintptr_t m_nFallbackStatTrak;
    extern uintptr_t m_bNeedToReApplyGloves;
    extern uintptr_t m_EconGloves;
    extern uintptr_t m_nEconGlovesChanged;
    extern uintptr_t m_iClip1;
    extern uintptr_t m_bPawnHasDefuser;
    extern uintptr_t m_bIsScoped;
    extern uintptr_t m_zoomLevel;
    extern uintptr_t m_vecViewOffset;
    extern uintptr_t m_bThrowAnimating;
    extern uintptr_t m_fThrowTime;
    extern uintptr_t m_flThrowStrength;

    extern uintptr_t m_Glow;
    extern uintptr_t m_flGlowBackfaceMult;
    extern uintptr_t glow_m_fGlowColor;
    extern uintptr_t glow_m_iGlowType;
    extern uintptr_t glow_m_iGlowTeam;
    extern uintptr_t glow_m_nGlowRange;
    extern uintptr_t glow_m_nGlowRangeMin;
    extern uintptr_t glow_m_glowColorOverride;
    extern uintptr_t glow_m_bFlashing;
    extern uintptr_t glow_m_flGlowTime;
    extern uintptr_t glow_m_flGlowStartTime;
    extern uintptr_t glow_m_bGlowing;
    extern uintptr_t glow_m_bEligibleForScreenHighlight;
    // Flash / Smoke overlays (per-player pawn fields)
    extern uintptr_t m_flFlashOverlayAlpha;
    extern uintptr_t m_flFlashMaxAlpha;
    extern uintptr_t m_flFlashDuration;
    extern uintptr_t m_flLastSmokeOverlayAlpha;
    extern uintptr_t m_flFlashedAmount;
    extern uintptr_t m_bFlashing;
    extern uintptr_t m_flFlashBangTime;
    extern uintptr_t m_flFlashScreenshotAlpha;
    extern uintptr_t m_flLastSpawnTimeIndex;
    extern uintptr_t m_bFlashBuildUp;
    extern uintptr_t m_bFlashDspHasBeenCleared;
    extern uintptr_t m_bFlashScreenshotHasBeenGrabbed;

    // Entity identity and smoke projectile fields.
    extern uintptr_t m_pEntityIdentity;
    extern uintptr_t m_designerName;
    extern uintptr_t m_vSmokeColor;

	//C4 OFFSETS NEW!!

    
    

}

void LoadOffsetsFromFile(const char* path);
void LoadOffsetsFromJSON(const char* path);
void LoadClientSchemaOffsetsFromJSON(const char* path);
void PrintLoadedOffsets();

enum class OffsetGroup {
    Core,
    Inventory,
    Visuals
};

struct LoadedOffsetEntry {
    const char* name = "";
    uintptr_t value = 0;
    OffsetGroup group = OffsetGroup::Core;
};

std::vector<LoadedOffsetEntry> GetLoadedOffsetsSnapshot();

struct OffsetUpdateStatus {
    bool loaded = false;
    bool updatedThisRun = false;
    bool dumperFound = false;
    int globalOffsetsFound = 0;
    int schemaOffsetsFound = 0;
    std::string source;
    std::string build;
    std::string message;
};

// Runs the bundled dumper at startup and falls back to the last validated output.
void InitializeOffsetSystem();
bool RunOffsetAutoUpdate();
bool ReloadOffsetsFromOutput();
const OffsetUpdateStatus& GetOffsetUpdateStatus();
