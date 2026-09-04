#pragma once

#include <windows.h>
#include <cstdint>

#include "InventoryTypes.h"

struct Vector3 { float x, y, z; };
struct Matrix4x4 { float m[4][4]; };

enum EspTextAnchor {
    EspTextTop = 0,
    EspTextBottom = 1,
    EspTextLeft = 2,
    EspTextRight = 3
};

struct EspSettings {
    bool showTeammates = true;
    bool showBoxes = true;
    bool showNames = true;
    bool visibilityNames = true;
    bool visibilityBoxes = true;
    bool showHpText = true;
    bool showHpBar = true;
    bool showArmorText = true;
    bool showArmorBar = true;
    bool showActiveWeapon = true;
    bool showWeaponAmmo = true;
    int weaponDisplayMode = 1;
    float weaponIconSize = 18.0f;
    bool showDebug = false;
    // Persistent debug window (separate from inline debug text)
    bool showDebugWindow = false;
   
    bool enableAntiSmoke = false;
    bool enableAntiFlashbang = false;
    bool enableSmokeColor = false;
    int smokeColorR = 0;
    int smokeColorG = 170;
    int smokeColorB = 255;
    int nameTextAnchor = EspTextTop;
    int hpTextAnchor = EspTextLeft;
    int armorTextAnchor = EspTextRight;
    int weaponTextAnchor = EspTextBottom;
    int equipmentTextAnchor = EspTextRight;

    int enemyBoxR = 240, enemyBoxG = 50, enemyBoxB = 50;
    int teamBoxR = 80, teamBoxG = 160, teamBoxB = 255;
    int hpTextR = 255, hpTextG = 255, hpTextB = 255;
    int nameTextR = 255, nameTextG = 255, nameTextB = 255;
    int nameVisibleR = 80, nameVisibleG = 255, nameVisibleB = 120;
    int nameHiddenR = 255, nameHiddenG = 70, nameHiddenB = 70;
    int boxVisibleR = 80, boxVisibleG = 255, boxVisibleB = 120;
    int boxHiddenR = 255, boxHiddenG = 70, boxHiddenB = 70;
    int armorBarR = 70, armorBarG = 140, armorBarB = 255;
    int weaponTextR = 255, weaponTextG = 210, weaponTextB = 80;
    int weaponIconR = 255, weaponIconG = 255, weaponIconB = 255;
    int weaponBackgroundR = 0, weaponBackgroundG = 0, weaponBackgroundB = 0;
    int weaponBackgroundAlpha = 70;
    int equipmentDisplayMode = 1;
    float equipmentIconSize = 17.0f;
    int bombIconR = 255, bombIconG = 80, bombIconB = 80;
    int defuseIconR = 80, defuseIconG = 190, defuseIconB = 255;
    int armorIconR = 80, armorIconG = 150, armorIconB = 255;
    int helmetIconR = 210, helmetIconG = 225, helmetIconB = 255;
    float boxOutlineThickness = 1.5f;

    // Glow in-game (CGlowProperty)
    bool enableGlow = false;
    bool showTeammateGlow = false;
    bool glowUseStaticColor = false;
    int glowAlpha = 255;

    // Glow Visible (Green)
    int glowVisibleR = 0;
    int glowVisibleG = 255;
    int glowVisibleB = 0;

    // Glow Invisible (Red)
    int glowInvisibleR = 255;
    int glowInvisibleG = 0;
    int glowInvisibleB = 0;

    // Glow Static (Blue)
    int glowStaticR = 100;
    int glowStaticG = 200;
    int glowStaticB = 255;
    // Glow update interval in milliseconds (throttle writes)
    int glowUpdateMs = 100;

    // Overlay-rendered alternative to the native CGlowProperty implementation.
    bool enableOtherGlow = false;
    bool otherGlowUseStaticColor = false;
    int otherGlowAlpha = 180;
    int otherGlowStaticR = 100;
    int otherGlowStaticG = 200;
    int otherGlowStaticB = 255;
    float otherGlowBodyScale = 1.0f;
    float otherGlowSoftness = 7.0f;
    int otherGlowLayers = 1;

    // Per-player equipment indicators.
    bool showBombCarrier = false;
    bool showDefuseKits = false;
    bool showArmorIndicator = false;
    bool showHelmetIndicator = false;

    // Misc display toggles
    bool enableRadarHack = false;
    bool showCrosshair = false;
    bool showSpectatorList = false;
    bool showBombInfo = false;
    bool bombInfoShowSite = true;
    bool bombInfoShowTimer = true;
    bool bombInfoShowDefusing = true;
    bool bombInfoShowDecision = true;
    bool bombInfoAutoResize = true;
    int fontMode = 0; // 0 = Modern (Segoe UI), 1 = Classic (Retro ImGui)
    bool showGrenadeTrajectory = false;
    // 0 = whenever a grenade is equipped, 1 = only while holding a throw button.
    int grenadeTrajectoryMode = 0;
    // Grenade trajectory calibration (adjustable from the menu).
    // Base throw speed at full strength (community reference: 1090 u/s
    // for CS2) and effective projectile gravity (default sv_gravity:
    // 800 u/s^2). Fine-tune them live against the game's native
    // trajectory (sv_grenade_trajectory 1 in a practice match).
    float grenadeThrowSpeed = 1090.0f;
    float grenadeGravity = 800.0f;
    // Aimlock calibrator toggle shown in Misc
    bool enableAimlockCalibrator = false;
    // Flash/smoke thresholds
    float flashThreshold = 0.5f;
    float smokeThreshold = 0.5f;
    // Percentage of the original flash visual retained by Anti Flash.
    int antiFlashOpacityPercent = 0;
    // Show skeleton overlay (visuals)
    bool showSkeleton = false;
    // Skeleton customization
    int skeletonR = 200;
    int skeletonG = 200;
    int skeletonB = 200;
    int skeletonAlpha = 230;
    // 0 = custom visibility colors, 1 = fixed color, 2 = inherit Box color.
    int skeletonColorMode = 0;
    int skeletonVisibleR = 0;
    int skeletonVisibleG = 255;
    int skeletonVisibleB = 0;
    int skeletonHiddenR = 255;
    int skeletonHiddenG = 0;
    int skeletonHiddenB = 0;
    float skeletonThickness = 2.0f; // line thickness
    bool skeletonShowJoints = true;
    float skeletonScale = 1.0f; // scale multiplier for offsets
    bool skeletonUseTeamColor = false; // legacy setting kept for config compatibility
    // UI placeholder flags
    bool enableThirdperson = false;
    int thirdPersonKeyVk = 0x50; // Default P key to toggle third person
    bool waitingForThirdPersonKey = false; // true when user is about to assign key
    bool showMoney = false;
    bool fakeProfile = false;
    bool quitPunchview = false;
};

struct AppSettings {
    char menuTitle[64] = "Ianveig29 C.H.E.A.T";
    int menuToggleVk = VK_INSERT;
    bool waitingForMenuKey = false;
    bool panicBindEnabled = true;
    int panicVk = VK_END;
    bool waitingForPanicKey = false;
    bool autoCloseOnGameExit = true; // Close OverlayAI when CS2 closes
};

struct TriggerbotSettings {
    bool enabled = false;
    bool requireHoldKey = true;
    int holdKeyVk = VK_MENU;
    bool waitingForHoldKey = false;
    int delayMs = 80;
    bool requireVisible = true;
    bool shootTeammates = false;
    // Allow triggerbot behavior when flashed/in smoke
    bool allowWhenFlashed = false;
    bool allowWhenInSmoke = false;
};

struct BhopSettings {
    bool enabled = false;
    bool requireHoldKey = true;
    int holdKeyVk = VK_SPACE;
    bool waitingForHoldKey = false;
    bool strafeAssist = false;
};

struct AimSettings {
    bool enabled = false;
    bool requireHoldKey = true;
    int holdKeyVk = VK_MENU; // ALT by default
    bool waitingForHoldKey = false;
    float fovDegrees = 6.0f; // target FOV in degrees
    bool useScopedFov = true;
    float singleScopeFovDegrees = 3.0f;
    float doubleScopeFovDegrees = 1.5f;
    float smoothing = 6.0f; // higher = slower smoothing
    // Draw aimlock FOV circle on-screen when enabled
    bool drawFov = false;
    bool requireVisible = true;
    // Targeting: 0=Head,1=Torso,2=Legs,3=Auto (head->torso->legs)
    int targetPart = 0;
    // Offsets (world units above feet) used to compute aim point for each part
    float headOffset = 65.0f;
    float torsoOffset = 50.0f;
    float legOffset = 24.0f;
    // Allow aimlock when flashed/in smoke
    bool allowWhenFlashed = false;
    bool allowWhenInSmoke = false;
    // UI placeholder flag
    bool recoilControlSystem = false;
};
