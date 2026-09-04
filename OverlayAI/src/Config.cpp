#include "Config.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
#include "Types.h"
#include "InventoryPersistence.h"


EspSettings g_Esp;
AppSettings g_App;
TriggerbotSettings g_Triggerbot;
BhopSettings g_Bhop;
InventoryChangerSettings g_InventoryChanger;
AimSettings g_Aim;
HWND g_OverlayHwnd = nullptr;
bool g_MenuOpen = false;

namespace {
    namespace fs = std::filesystem;
    const fs::path kConfigDirectory = "configs";
    const fs::path kPreloadFile = "preload_config.txt";

    std::string NormalizeConfigName(std::string name) {
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
            name.erase(name.begin());
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.pop_back();
        if (name.size() > 4 && name.substr(name.size() - 4) == ".cfg")
            name.resize(name.size() - 4);

        std::string safe;
        safe.reserve(name.size());
        for (unsigned char ch : name) {
            if (std::isalnum(ch) || ch == '-' || ch == '_')
                safe.push_back(static_cast<char>(ch));
            else if (std::isspace(ch) && !safe.empty() && safe.back() != '_')
                safe.push_back('_');
        }
        if (safe.empty() || safe == "." || safe == "..") return {};
        if (safe.size() > 48) safe.resize(48);
        return safe + ".cfg";
    }

    fs::path ConfigPath(const std::string& name) {
        const std::string normalized = NormalizeConfigName(name);
        return normalized.empty() ? fs::path{} : kConfigDirectory / normalized;
    }

}

void SaveEspConfig(const char* path) {
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
        "armor_bar=%d,%d,%d\n"
        "box_outline=%f\n"
        "triggerbot_enabled=%d\n"
        "triggerbot_require_hold=%d\n"
        "triggerbot_hold_key=%d\n"
        "triggerbot_delay_ms=%d\n"
        "triggerbot_require_visible=%d\n"
        "triggerbot_shoot_teammates=%d\n"
        "bhop_enabled=%d\n"
        "bhop_require_hold=%d\n"
        "bhop_hold_key=%d\n"
        "glow_enabled=%d\n"
        "glow_teammates=%d\n"
        "glow_static_color=%d\n"
        "glow_alpha=%d\n"
        "glow_visible=%d,%d,%d\n"
        "glow_invisible=%d,%d,%d\n"
        "glow_static=%d,%d,%d\n"
        "show_boxes=%d\n"
        "aim_enabled=%d\n"
        "aim_require_hold=%d\n"
        "aim_hold_key=%d\n"
        "aim_fov=%f\n"
        "aim_smoothing=%f\n",
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
        g_Esp.armorBarR, g_Esp.armorBarG, g_Esp.armorBarB,
        g_Esp.boxOutlineThickness,
        g_Triggerbot.enabled ? 1 : 0,
        g_Triggerbot.requireHoldKey ? 1 : 0,
        g_Triggerbot.holdKeyVk,
        g_Triggerbot.delayMs,
        g_Triggerbot.requireVisible ? 1 : 0,
        g_Triggerbot.shootTeammates ? 1 : 0,
        g_Bhop.enabled ? 1 : 0,
        g_Bhop.requireHoldKey ? 1 : 0,
        g_Bhop.holdKeyVk,
        g_Esp.enableGlow ? 1 : 0,
        g_Esp.showTeammateGlow ? 1 : 0,
        g_Esp.glowUseStaticColor ? 1 : 0,
        g_Esp.glowAlpha,
        g_Esp.glowVisibleR, g_Esp.glowVisibleG, g_Esp.glowVisibleB,
        g_Esp.glowInvisibleR, g_Esp.glowInvisibleG, g_Esp.glowInvisibleB,
        g_Esp.glowStaticR, g_Esp.glowStaticG, g_Esp.glowStaticB,
        g_Esp.showBoxes ? 1 : 0,
        g_Aim.enabled ? 1 : 0,
        g_Aim.requireHoldKey ? 1 : 0,
        g_Aim.holdKeyVk,
        g_Aim.fovDegrees,
        g_Aim.smoothing);
    fprintf(f, "panic_bind_enabled=%d\n", g_App.panicBindEnabled ? 1 : 0);
    fprintf(f, "auto_close_on_game_exit=%d\n", g_App.autoCloseOnGameExit ? 1 : 0);
    fprintf(f, "panic_key=%d\n", g_App.panicVk);
    // write new toggles
    fprintf(f, "show_bomb_carrier=%d\n", g_Esp.showBombCarrier ? 1 : 0);
    fprintf(f, "show_defuse_kits=%d\n", g_Esp.showDefuseKits ? 1 : 0);
    fprintf(f, "show_armor_indicator=%d\n", g_Esp.showArmorIndicator ? 1 : 0);
    fprintf(f, "show_helmet_indicator=%d\n", g_Esp.showHelmetIndicator ? 1 : 0);
    fprintf(f, "show_spectator_list=%d\n", g_Esp.showSpectatorList ? 1 : 0);
    fprintf(f, "show_bomb_info=%d\n", g_Esp.showBombInfo ? 1 : 0);
    fprintf(f, "bomb_info_show_site=%d\n", g_Esp.bombInfoShowSite ? 1 : 0);
    fprintf(f, "bomb_info_show_timer=%d\n", g_Esp.bombInfoShowTimer ? 1 : 0);
    fprintf(f, "bomb_info_show_defusing=%d\n", g_Esp.bombInfoShowDefusing ? 1 : 0);
    fprintf(f, "bomb_info_show_decision=%d\n", g_Esp.bombInfoShowDecision ? 1 : 0);
    fprintf(f, "bomb_info_auto_resize=%d\n", g_Esp.bombInfoAutoResize ? 1 : 0);
    fprintf(f, "font_mode=%d\n", g_Esp.fontMode);
    fprintf(f, "radar_hack_enabled=%d\n", g_Esp.enableRadarHack ? 1 : 0);
    fprintf(f, "show_crosshair=%d\n", g_Esp.showCrosshair ? 1 : 0);
    fprintf(f, "grenade_trajectory_enabled=%d\n", g_Esp.showGrenadeTrajectory ? 1 : 0);
    fprintf(f, "grenade_trajectory_mode=%d\n", g_Esp.grenadeTrajectoryMode);
    // Aimlock calibrator persistence
    fprintf(f, "enable_aim_calibrator=%d\n", g_Esp.enableAimlockCalibrator ? 1 : 0);
    fprintf(f, "aim_target_part=%d\n", g_Aim.targetPart);
    fprintf(f, "aim_head_offset=%f\n", g_Aim.headOffset);
    fprintf(f, "aim_torso_offset=%f\n", g_Aim.torsoOffset);
    fprintf(f, "aim_leg_offset=%f\n", g_Aim.legOffset);
    fprintf(f, "aim_require_visible=%d\n", g_Aim.requireVisible ? 1 : 0);
    fprintf(f, "aim_use_scoped_fov=%d\n", g_Aim.useScopedFov ? 1 : 0);
    fprintf(f, "aim_single_scope_fov=%f\n", g_Aim.singleScopeFovDegrees);
    fprintf(f, "anti_flash_enabled=%d\n", g_Esp.enableAntiFlashbang ? 1 : 0);
    fprintf(f, "thirdperson_enabled=%d\n", g_Esp.enableThirdperson ? 1 : 0);
    fprintf(f, "thirdperson_key=%d\n", g_Esp.thirdPersonKeyVk);
    fprintf(f, "show_money=%d\n", g_Esp.showMoney ? 1 : 0);
    fprintf(f, "fake_profile=%d\n", g_Esp.fakeProfile ? 1 : 0);
    fprintf(f, "quit_punchview=%d\n", g_Esp.quitPunchview ? 1 : 0);
    fprintf(f, "quit_aim_punch=%d\n", g_Esp.quitAimPunch ? 1 : 0);
    fprintf(f, "rcs_enabled=%d\n", g_Aim.recoilControlSystem ? 1 : 0);
    fprintf(f, "rcs_strength=%d\n", g_Aim.rcsStrengthPercent);
    fprintf(f, "flash_opacity_percent=%d\n", g_Esp.antiFlashOpacityPercent);
    fprintf(f, "flash_threshold=%f\n", g_Esp.flashThreshold);
    fprintf(f, "aim_allow_flashed=%d\n", g_Aim.allowWhenFlashed ? 1 : 0);
    fprintf(f, "trigger_allow_flashed=%d\n", g_Triggerbot.allowWhenFlashed ? 1 : 0);
    fprintf(f, "anti_smoke_enabled=%d\n", g_Esp.enableAntiSmoke ? 1 : 0);
    fprintf(f, "smoke_color_enabled=%d\n", g_Esp.enableSmokeColor ? 1 : 0);
    fprintf(f, "smoke_color=%d,%d,%d\n", g_Esp.smokeColorR, g_Esp.smokeColorG, g_Esp.smokeColorB);
    fprintf(f, "aim_allow_smoke=%d\n", g_Aim.allowWhenInSmoke ? 1 : 0);
    fprintf(f, "trigger_allow_smoke=%d\n", g_Triggerbot.allowWhenInSmoke ? 1 : 0);
    fprintf(f, "bhop_strafe_assist=%d\n", g_Bhop.strafeAssist ? 1 : 0);
    (void)SaveInventoryConfig(f, g_InventoryChanger);
    fprintf(f, "other_glow_enabled=%d\n", g_Esp.enableOtherGlow ? 1 : 0);
    fprintf(f, "other_glow_static_color=%d\n", g_Esp.otherGlowUseStaticColor ? 1 : 0);
    fprintf(f, "other_glow_static=%d,%d,%d\n",
        g_Esp.otherGlowStaticR, g_Esp.otherGlowStaticG, g_Esp.otherGlowStaticB);
    fprintf(f, "other_glow_alpha=%d\n", g_Esp.otherGlowAlpha);
    fprintf(f, "other_glow_body_scale=%f\n", g_Esp.otherGlowBodyScale);
    fprintf(f, "other_glow_softness=%f\n", g_Esp.otherGlowSoftness);
    fprintf(f, "other_glow_layers=%d\n", g_Esp.otherGlowLayers);
    fprintf(f, "show_skeleton=%d\n", g_Esp.showSkeleton ? 1 : 0);
    fprintf(f, "skeleton_color_mode=%d\n", g_Esp.skeletonColorMode);
    fprintf(f, "skeleton_visible=%d,%d,%d\n", g_Esp.skeletonVisibleR, g_Esp.skeletonVisibleG, g_Esp.skeletonVisibleB);
    fprintf(f, "skeleton_hidden=%d,%d,%d\n", g_Esp.skeletonHiddenR, g_Esp.skeletonHiddenG, g_Esp.skeletonHiddenB);
    fprintf(f, "skeleton_fixed=%d,%d,%d\n", g_Esp.skeletonR, g_Esp.skeletonG, g_Esp.skeletonB);
    fprintf(f, "skeleton_alpha=%d\n", g_Esp.skeletonAlpha);
    fprintf(f, "skeleton_thickness=%f\n", g_Esp.skeletonThickness);
    fprintf(f, "skeleton_joint_scale=%f\n", g_Esp.skeletonScale);
    fprintf(f, "skeleton_show_joints=%d\n", g_Esp.skeletonShowJoints ? 1 : 0);
    fprintf(f, "show_active_weapon=%d\n", g_Esp.showActiveWeapon ? 1 : 0);
    fprintf(f, "show_weapon_ammo=%d\n", g_Esp.showWeaponAmmo ? 1 : 0);
    fprintf(f, "weapon_display_mode=%d\n", g_Esp.weaponDisplayMode);
    fprintf(f, "weapon_icon_size=%f\n", g_Esp.weaponIconSize);
    fprintf(f, "hp_text_anchor=%d\n", g_Esp.hpTextAnchor);
    fprintf(f, "armor_text_anchor=%d\n", g_Esp.armorTextAnchor);
    fprintf(f, "weapon_text_anchor=%d\n", g_Esp.weaponTextAnchor);
    fprintf(f, "weapon_text=%d,%d,%d\n", g_Esp.weaponTextR, g_Esp.weaponTextG, g_Esp.weaponTextB);
    fprintf(f, "weapon_icon=%d,%d,%d\n", g_Esp.weaponIconR, g_Esp.weaponIconG, g_Esp.weaponIconB);
    fprintf(f, "weapon_background=%d,%d,%d\n",
        g_Esp.weaponBackgroundR, g_Esp.weaponBackgroundG, g_Esp.weaponBackgroundB);
    fprintf(f, "weapon_background_alpha=%d\n", g_Esp.weaponBackgroundAlpha);
    fprintf(f, "equipment_display_mode=%d\n", g_Esp.equipmentDisplayMode);
    fprintf(f, "equipment_icon_size=%f\n", g_Esp.equipmentIconSize);
    fprintf(f, "equipment_text_anchor=%d\n", g_Esp.equipmentTextAnchor);
    fprintf(f, "bomb_icon=%d,%d,%d\n", g_Esp.bombIconR, g_Esp.bombIconG, g_Esp.bombIconB);
    fprintf(f, "defuse_icon=%d,%d,%d\n", g_Esp.defuseIconR, g_Esp.defuseIconG, g_Esp.defuseIconB);
    fprintf(f, "armor_icon=%d,%d,%d\n", g_Esp.armorIconR, g_Esp.armorIconG, g_Esp.armorIconB);
    fprintf(f, "helmet_icon=%d,%d,%d\n", g_Esp.helmetIconR, g_Esp.helmetIconG, g_Esp.helmetIconB);
    fclose(f);
}

void SavePreloadSlot(int slot) {
    SavePreloadConfig(slot >= 1 && slot <= 3
        ? "slot" + std::to_string(slot) + ".cfg"
        : std::string{});
}

int LoadPreloadSlot() {
    const std::string preload = LoadPreloadConfig();
    for (int slot = 1; slot <= 3; ++slot) {
        if (preload == "slot" + std::to_string(slot) + ".cfg") return slot;
    }
    return 0;
}

void LoadEspConfig(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return;
    char line[1024];
    bool loadedOtherStaticMode = false;
    bool loadedOtherStaticColor = false;
    InventoryConfigLoadContext inventoryLoadContext;
    g_InventoryChanger = InventoryChangerSettings{};
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;
        if (TryLoadInventoryConfigLine(g_InventoryChanger, inventoryLoadContext, p)) continue;
        int i1 = 0, i2 = 0, i3 = 0;
        float f1 = 0.0f;
        if (sscanf_s(p, "show_teammates=%d", &i1) == 1) g_Esp.showTeammates = i1 != 0;
        else if (sscanf_s(p, "show_names=%d", &i1) == 1) g_Esp.showNames = i1 != 0;
        else if (sscanf_s(p, "visibility_names=%d", &i1) == 1) g_Esp.visibilityNames = i1 != 0;
        else if (sscanf_s(p, "show_hp_text=%d", &i1) == 1) g_Esp.showHpText = i1 != 0;
        else if (sscanf_s(p, "show_hp_bar=%d", &i1) == 1) g_Esp.showHpBar = i1 != 0;
        else if (sscanf_s(p, "show_armor=%d", &i1) == 1) g_Esp.showArmorBar = i1 != 0;
        else if (sscanf_s(p, "show_armor_bar=%d", &i1) == 1) g_Esp.showArmorBar = i1 != 0;
        else if (sscanf_s(p, "show_armor_text=%d", &i1) == 1) g_Esp.showArmorText = i1 != 0;
        else if (sscanf_s(p, "show_active_weapon=%d", &i1) == 1) g_Esp.showActiveWeapon = i1 != 0;
        else if (sscanf_s(p, "show_armor_indicator=%d", &i1) == 1) g_Esp.showArmorIndicator = i1 != 0;
        else if (sscanf_s(p, "show_helmet_indicator=%d", &i1) == 1) g_Esp.showHelmetIndicator = i1 != 0;
        else if (sscanf_s(p, "show_weapon_ammo=%d", &i1) == 1) g_Esp.showWeaponAmmo = i1 != 0;
        else if (sscanf_s(p, "weapon_display_mode=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 2) g_Esp.weaponDisplayMode = i1;
        } else if (sscanf_s(p, "weapon_icon_size=%f", &f1) == 1) {
            if (f1 >= 12.0f && f1 <= 32.0f) g_Esp.weaponIconSize = f1;
        } else if (sscanf_s(p, "equipment_display_mode=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 2) g_Esp.equipmentDisplayMode = i1;
        } else if (sscanf_s(p, "equipment_icon_size=%f", &f1) == 1) {
            if (f1 >= 12.0f && f1 <= 32.0f) g_Esp.equipmentIconSize = f1;
        }
        else if (sscanf_s(p, "hp_text_anchor=%d", &i1) == 1) {
            if (i1 >= EspTextTop && i1 <= EspTextRight) g_Esp.hpTextAnchor = i1;
        } else if (sscanf_s(p, "armor_text_anchor=%d", &i1) == 1) {
            if (i1 >= EspTextTop && i1 <= EspTextRight) g_Esp.armorTextAnchor = i1;
        } else if (sscanf_s(p, "weapon_text_anchor=%d", &i1) == 1) {
            if (i1 >= EspTextTop && i1 <= EspTextRight) g_Esp.weaponTextAnchor = i1;
        } else if (sscanf_s(p, "equipment_text_anchor=%d", &i1) == 1) {
            if (i1 >= EspTextTop && i1 <= EspTextRight) g_Esp.equipmentTextAnchor = i1;
        }
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
        } else if (sscanf_s(p, "panic_bind_enabled=%d", &i1) == 1) {
            g_App.panicBindEnabled = i1 != 0;
        } else if (sscanf_s(p, "auto_close_on_game_exit=%d", &i1) == 1) {
            g_App.autoCloseOnGameExit = i1 != 0;
        } else if (sscanf_s(p, "panic_key=%d", &i1) == 1) {
            if (i1 > 0 && i1 < 256 && i1 != g_App.menuToggleVk) g_App.panicVk = i1;
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
        } else if (sscanf_s(p, "weapon_text=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.weaponTextR = i1; g_Esp.weaponTextG = i2; g_Esp.weaponTextB = i3;
        } else if (sscanf_s(p, "weapon_icon=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.weaponIconR = i1; g_Esp.weaponIconG = i2; g_Esp.weaponIconB = i3;
        } else if (sscanf_s(p, "weapon_background=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.weaponBackgroundR = i1; g_Esp.weaponBackgroundG = i2; g_Esp.weaponBackgroundB = i3;
        } else if (sscanf_s(p, "weapon_background_alpha=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 255) g_Esp.weaponBackgroundAlpha = i1;
        } else if (sscanf_s(p, "bomb_icon=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.bombIconR = i1; g_Esp.bombIconG = i2; g_Esp.bombIconB = i3;
        } else if (sscanf_s(p, "defuse_icon=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.defuseIconR = i1; g_Esp.defuseIconG = i2; g_Esp.defuseIconB = i3;
        } else if (sscanf_s(p, "armor_icon=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.armorIconR = i1; g_Esp.armorIconG = i2; g_Esp.armorIconB = i3;
        } else if (sscanf_s(p, "helmet_icon=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.helmetIconR = i1; g_Esp.helmetIconG = i2; g_Esp.helmetIconB = i3;
        }
        else if (sscanf_s(p, "box_outline=%f", &f1) == 1) {
            if (f1 >= 0.0f && f1 <= 10.0f) g_Esp.boxOutlineThickness = f1;
        }
        else if (sscanf_s(p, "show_boxes=%d", &i1) == 1) {
            g_Esp.showBoxes = i1 != 0;
        } else if (sscanf_s(p, "triggerbot_enabled=%d", &i1) == 1) {
            g_Triggerbot.enabled = i1 != 0;
        } else if (sscanf_s(p, "triggerbot_require_hold=%d", &i1) == 1) {
            g_Triggerbot.requireHoldKey = i1 != 0;
        } else if (sscanf_s(p, "triggerbot_hold_key=%d", &i1) == 1) {
            if (i1 > 0 && i1 < 256) g_Triggerbot.holdKeyVk = i1;
        } else if (sscanf_s(p, "triggerbot_delay_ms=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 2000) g_Triggerbot.delayMs = i1;
        } else if (sscanf_s(p, "triggerbot_require_visible=%d", &i1) == 1) {
            g_Triggerbot.requireVisible = i1 != 0;
        } else if (sscanf_s(p, "triggerbot_shoot_teammates=%d", &i1) == 1) {
            g_Triggerbot.shootTeammates = i1 != 0;
        } else if (sscanf_s(p, "bhop_enabled=%d", &i1) == 1) {
            g_Bhop.enabled = i1 != 0;
        } else if (sscanf_s(p, "bhop_require_hold=%d", &i1) == 1) {
            g_Bhop.requireHoldKey = i1 != 0;
        } else if (sscanf_s(p, "bhop_hold_key=%d", &i1) == 1) {
            if (i1 > 0 && i1 < 256) g_Bhop.holdKeyVk = i1;
        } else if (sscanf_s(p, "bhop_strafe_assist=%d", &i1) == 1) {
            g_Bhop.strafeAssist = i1 != 0;
        } else if (sscanf_s(p, "glow_enabled=%d", &i1) == 1) {
            g_Esp.enableGlow = i1 != 0;
        } else if (sscanf_s(p, "glow_teammates=%d", &i1) == 1) {
            g_Esp.showTeammateGlow = i1 != 0;
        } else if (sscanf_s(p, "glow_static_color=%d", &i1) == 1) {
            g_Esp.glowUseStaticColor = i1 != 0;
        } else if (sscanf_s(p, "glow_alpha=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 255) g_Esp.glowAlpha = i1;
        } else if (sscanf_s(p, "glow_visible=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.glowVisibleR = i1; g_Esp.glowVisibleG = i2; g_Esp.glowVisibleB = i3;
        } else if (sscanf_s(p, "glow_invisible=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.glowInvisibleR = i1; g_Esp.glowInvisibleG = i2; g_Esp.glowInvisibleB = i3;
        } else if (sscanf_s(p, "glow_static=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.glowStaticR = i1; g_Esp.glowStaticG = i2; g_Esp.glowStaticB = i3;
        }
        else if (sscanf_s(p, "glow_enabled=%d", &i1) == 1) {
            g_Esp.enableGlow = i1 != 0;
        }
        else if (sscanf_s(p, "glow_teammates=%d", &i1) == 1) {
            g_Esp.showTeammateGlow = i1 != 0;
        }
        else if (sscanf_s(p, "glow_static_color=%d", &i1) == 1) {
            g_Esp.glowUseStaticColor = i1 != 0;
        }
        else if (sscanf_s(p, "glow_alpha=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 255) g_Esp.glowAlpha = i1;
        }
        else if (sscanf_s(p, "glow_visible=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.glowVisibleR = i1; g_Esp.glowVisibleG = i2; g_Esp.glowVisibleB = i3;
        } else if (sscanf_s(p, "glow_invisible=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.glowInvisibleR = i1; g_Esp.glowInvisibleG = i2; g_Esp.glowInvisibleB = i3;
        }
        else if (sscanf_s(p, "open_spectator_window=%d", &i1) == 1) {
            // Legacy alias retained so older presets migrate to the rebuilt window.
            if (i1 != 0) g_Esp.showSpectatorList = true;
        }
        else if (sscanf_s(p, "glow_static=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.glowStaticR = i1; g_Esp.glowStaticG = i2; g_Esp.glowStaticB = i3;
        }
        else if (sscanf_s(p, "aim_enabled=%d", &i1) == 1) {
            g_Aim.enabled = i1 != 0;
        } else if (sscanf_s(p, "aim_require_hold=%d", &i1) == 1) {
            g_Aim.requireHoldKey = i1 != 0;
        } else if (sscanf_s(p, "aim_hold_key=%d", &i1) == 1) {
            if (i1 > 0 && i1 < 256) g_Aim.holdKeyVk = i1;
        } else if (sscanf_s(p, "aim_fov=%f", &f1) == 1) {
            if (f1 >= 0.1f && f1 <= 180.0f) g_Aim.fovDegrees = f1;
        } else if (sscanf_s(p, "aim_smoothing=%f", &f1) == 1) {
            if (f1 > 0.0f) g_Aim.smoothing = f1;
        }
        else if (sscanf_s(p, "enable_aim_calibrator=%d", &i1) == 1) {
            g_Esp.enableAimlockCalibrator = i1 != 0;
        } else if (sscanf_s(p, "aim_target_part=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 3) g_Aim.targetPart = i1;
        } else if (sscanf_s(p, "aim_head_offset=%f", &f1) == 1) {
            if (f1 >= 0.0f && f1 <= 1000.0f) g_Aim.headOffset = f1;
        } else if (sscanf_s(p, "aim_torso_offset=%f", &f1) == 1) {
            if (f1 >= 0.0f && f1 <= 1000.0f) g_Aim.torsoOffset = f1;
        } else if (sscanf_s(p, "aim_leg_offset=%f", &f1) == 1) {
            if (f1 >= 0.0f && f1 <= 1000.0f) g_Aim.legOffset = f1;
        } else if (sscanf_s(p, "aim_require_visible=%d", &i1) == 1) {
            g_Aim.requireVisible = i1 != 0;
        } else if (sscanf_s(p, "aim_use_scoped_fov=%d", &i1) == 1) {
            g_Aim.useScopedFov = i1 != 0;
        } else if (sscanf_s(p, "aim_single_scope_fov=%f", &f1) == 1) {
            if (f1 >= 0.1f && f1 <= 89.0f) g_Aim.singleScopeFovDegrees = f1;
        } else if (sscanf_s(p, "aim_double_scope_fov=%f", &f1) == 1) {
            if (f1 >= 0.1f && f1 <= 89.0f) g_Aim.doubleScopeFovDegrees = f1;
        } else if (sscanf_s(p, "anti_flash_enabled=%d", &i1) == 1) {
            g_Esp.enableAntiFlashbang = i1 != 0;
        } else if (sscanf_s(p, "thirdperson_enabled=%d", &i1) == 1) {
            g_Esp.enableThirdperson = i1 != 0;
        } else if (sscanf_s(p, "thirdperson_key=%d", &i1) == 1) {
            g_Esp.thirdPersonKeyVk = i1;
        } else if (sscanf_s(p, "show_money=%d", &i1) == 1) {
            g_Esp.showMoney = i1 != 0;
        } else if (sscanf_s(p, "fake_profile=%d", &i1) == 1) {
            g_Esp.fakeProfile = i1 != 0;
        } else if (sscanf_s(p, "quit_punchview=%d", &i1) == 1) {
            g_Esp.quitPunchview = i1 != 0;
        } else if (sscanf_s(p, "quit_aim_punch=%d", &i1) == 1) {
            g_Esp.quitAimPunch = i1 != 0;
        } else if (sscanf_s(p, "rcs_enabled=%d", &i1) == 1) {
            g_Aim.recoilControlSystem = i1 != 0;
        } else if (sscanf_s(p, "rcs_strength=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 100) g_Aim.rcsStrengthPercent = i1;
        } else if (sscanf_s(p, "flash_opacity_percent=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 100) g_Esp.antiFlashOpacityPercent = i1;
        } else if (sscanf_s(p, "flash_threshold=%f", &f1) == 1) {
            if (f1 >= 0.0f && f1 <= 255.0f) g_Esp.flashThreshold = f1;
        } else if (sscanf_s(p, "aim_allow_flashed=%d", &i1) == 1) {
            g_Aim.allowWhenFlashed = i1 != 0;
        } else if (sscanf_s(p, "trigger_allow_flashed=%d", &i1) == 1) {
            g_Triggerbot.allowWhenFlashed = i1 != 0;
        } else if (sscanf_s(p, "anti_smoke_enabled=%d", &i1) == 1) {
            g_Esp.enableAntiSmoke = i1 != 0;
        } else if (sscanf_s(p, "smoke_color_enabled=%d", &i1) == 1) {
            g_Esp.enableSmokeColor = i1 != 0;
        } else if (sscanf_s(p, "smoke_color=%d,%d,%d", &i1, &i2, &i3) == 3) {
            if (i1 >= 0 && i1 <= 255 && i2 >= 0 && i2 <= 255 && i3 >= 0 && i3 <= 255) {
                g_Esp.smokeColorR = i1;
                g_Esp.smokeColorG = i2;
                g_Esp.smokeColorB = i3;
            }
        } else if (sscanf_s(p, "aim_allow_smoke=%d", &i1) == 1) {
            g_Aim.allowWhenInSmoke = i1 != 0;
        } else if (sscanf_s(p, "trigger_allow_smoke=%d", &i1) == 1) {
            g_Triggerbot.allowWhenInSmoke = i1 != 0;
        }
        if (sscanf_s(p, "show_bomb_carrier=%d", &i1) == 1) {
            g_Esp.showBombCarrier = i1 != 0;
        } else if (sscanf_s(p, "show_defuse_kits=%d", &i1) == 1) {
            g_Esp.showDefuseKits = i1 != 0;
        } else if (sscanf_s(p, "show_spectator_list=%d", &i1) == 1) {
            g_Esp.showSpectatorList = i1 != 0;
        } else if (sscanf_s(p, "show_bomb_info=%d", &i1) == 1) {
            g_Esp.showBombInfo = i1 != 0;
        } else if (sscanf_s(p, "bomb_info_show_site=%d", &i1) == 1) {
            g_Esp.bombInfoShowSite = i1 != 0;
        } else if (sscanf_s(p, "bomb_info_show_timer=%d", &i1) == 1) {
            g_Esp.bombInfoShowTimer = i1 != 0;
        } else if (sscanf_s(p, "bomb_info_show_defusing=%d", &i1) == 1) {
            g_Esp.bombInfoShowDefusing = i1 != 0;
        } else if (sscanf_s(p, "bomb_info_show_decision=%d", &i1) == 1) {
            g_Esp.bombInfoShowDecision = i1 != 0;
        } else if (sscanf_s(p, "bomb_info_auto_resize=%d", &i1) == 1) {
            g_Esp.bombInfoAutoResize = i1 != 0;
        } else if (sscanf_s(p, "font_mode=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 1) g_Esp.fontMode = i1;
        } else if (sscanf_s(p, "radar_hack_enabled=%d", &i1) == 1) {
            g_Esp.enableRadarHack = i1 != 0;
        } else if (sscanf_s(p, "show_crosshair=%d", &i1) == 1) {
            g_Esp.showCrosshair = i1 != 0;
        } else if (sscanf_s(p, "grenade_trajectory_enabled=%d", &i1) == 1) {
            g_Esp.showGrenadeTrajectory = i1 != 0;
        } else if (sscanf_s(p, "grenade_trajectory_mode=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 1) g_Esp.grenadeTrajectoryMode = i1;
        }
        if (sscanf_s(p, "other_glow_enabled=%d", &i1) == 1 ||
            sscanf_s(p, "custom_glow_enabled=%d", &i1) == 1) {
            g_Esp.enableOtherGlow = i1 != 0;
        } else if (sscanf_s(p, "other_glow_static_color=%d", &i1) == 1) {
            g_Esp.otherGlowUseStaticColor = i1 != 0;
            loadedOtherStaticMode = true;
        } else if (sscanf_s(p, "other_glow_static=%d,%d,%d", &i1, &i2, &i3) == 3) {
            if (i1 >= 0 && i1 <= 255 && i2 >= 0 && i2 <= 255 && i3 >= 0 && i3 <= 255) {
                g_Esp.otherGlowStaticR = i1;
                g_Esp.otherGlowStaticG = i2;
                g_Esp.otherGlowStaticB = i3;
                loadedOtherStaticColor = true;
            }
        } else if (sscanf_s(p, "other_glow_alpha=%d", &i1) == 1 ||
            sscanf_s(p, "custom_glow_alpha=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 255) g_Esp.otherGlowAlpha = i1;
        } else if (sscanf_s(p, "other_glow_body_scale=%f", &f1) == 1 ||
            sscanf_s(p, "custom_glow_body_scale=%f", &f1) == 1) {
            g_Esp.otherGlowBodyScale = std::clamp(f1, 0.85f, 2.0f);
        } else if (sscanf_s(p, "other_glow_softness=%f", &f1) == 1 ||
            sscanf_s(p, "custom_glow_softness=%f", &f1) == 1) {
            g_Esp.otherGlowSoftness = std::clamp(f1, 2.0f, 20.0f);
        } else if (sscanf_s(p, "other_glow_layers=%d", &i1) == 1 ||
            sscanf_s(p, "custom_glow_layers=%d", &i1) == 1) {
            g_Esp.otherGlowLayers = std::clamp(i1, 1, 3);
        }
        if (sscanf_s(p, "show_skeleton=%d", &i1) == 1) {
            g_Esp.showSkeleton = i1 != 0;
        } else if (sscanf_s(p, "skeleton_color_mode=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 2) g_Esp.skeletonColorMode = i1;
        } else if (sscanf_s(p, "skeleton_visible=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.skeletonVisibleR = i1; g_Esp.skeletonVisibleG = i2; g_Esp.skeletonVisibleB = i3;
        } else if (sscanf_s(p, "skeleton_hidden=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.skeletonHiddenR = i1; g_Esp.skeletonHiddenG = i2; g_Esp.skeletonHiddenB = i3;
        } else if (sscanf_s(p, "skeleton_fixed=%d,%d,%d", &i1, &i2, &i3) == 3) {
            g_Esp.skeletonR = i1; g_Esp.skeletonG = i2; g_Esp.skeletonB = i3;
        } else if (sscanf_s(p, "skeleton_alpha=%d", &i1) == 1) {
            if (i1 >= 0 && i1 <= 255) g_Esp.skeletonAlpha = i1;
        } else if (sscanf_s(p, "skeleton_thickness=%f", &f1) == 1) {
            if (f1 >= 0.5f && f1 <= 6.0f) g_Esp.skeletonThickness = f1;
        } else if (sscanf_s(p, "skeleton_joint_scale=%f", &f1) == 1) {
            if (f1 >= 0.5f && f1 <= 2.0f) g_Esp.skeletonScale = f1;
        } else if (sscanf_s(p, "skeleton_show_joints=%d", &i1) == 1) {
            g_Esp.skeletonShowJoints = i1 != 0;
        }
    }
    fclose(f);
    (void)FinalizeInventoryConfigLoad(g_InventoryChanger, inventoryLoadContext);
    if (!loadedOtherStaticMode)
        g_Esp.otherGlowUseStaticColor = g_Esp.glowUseStaticColor;
    if (!loadedOtherStaticColor) {
        g_Esp.otherGlowStaticR = g_Esp.glowStaticR;
        g_Esp.otherGlowStaticG = g_Esp.glowStaticG;
        g_Esp.otherGlowStaticB = g_Esp.glowStaticB;
    }
    if (g_Esp.enableGlow && g_Esp.enableOtherGlow)
        g_Esp.enableGlow = false;
}

void EnsureConfigStorage() {
    std::error_code error;
    fs::create_directories(kConfigDirectory, error);
    for (int slot = 1; slot <= 3; ++slot) {
        const std::string fileName = "slot" + std::to_string(slot) + ".cfg";
        const fs::path destination = kConfigDirectory / fileName;
        if (fs::exists(destination, error)) continue;

        const fs::path legacy = "esp_config_slot" + std::to_string(slot) + ".ini";
        if (fs::exists(legacy, error)) {
            fs::copy_file(legacy, destination, fs::copy_options::overwrite_existing, error);
            if (!error) continue;
            error.clear();
        }
        SaveEspConfig(destination.string().c_str());
    }
}

bool IsDefaultConfigSlot(const std::string& name) {
    const std::string normalized = NormalizeConfigName(name);
    return normalized == "slot1.cfg" || normalized == "slot2.cfg" ||
        normalized == "slot3.cfg";
}

std::vector<std::string> ListConfigPresets() {
    EnsureConfigStorage();
    std::vector<std::string> presets;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(kConfigDirectory, error)) {
        if (error) break;
        if (!entry.is_regular_file(error) || entry.path().extension() != ".cfg") continue;
        presets.push_back(entry.path().filename().string());
    }
    auto rank = [](const std::string& name) {
        if (name == "slot1.cfg") return 0;
        if (name == "slot2.cfg") return 1;
        if (name == "slot3.cfg") return 2;
        return 3;
    };
    std::sort(presets.begin(), presets.end(), [&](const std::string& left, const std::string& right) {
        const int leftRank = rank(left);
        const int rightRank = rank(right);
        if (leftRank != rightRank) return leftRank < rightRank;
        return left < right;
    });
    return presets;
}

bool SaveConfigPreset(const std::string& name, std::string* savedName) {
    EnsureConfigStorage();
    const std::string normalized = NormalizeConfigName(name);
    if (normalized.empty()) return false;
    const fs::path path = kConfigDirectory / normalized;
    SaveEspConfig(path.string().c_str());
    std::error_code error;
    const bool saved = fs::exists(path, error) && fs::file_size(path, error) > 0;
    if (saved && savedName) *savedName = normalized;
    return saved;
}

bool LoadConfigPreset(const std::string& name) {
    const fs::path path = ConfigPath(name);
    std::error_code error;
    if (path.empty() || !fs::is_regular_file(path, error)) return false;
    LoadEspConfig(path.string().c_str());
    return true;
}

bool DeleteConfigPreset(const std::string& name) {
    const std::string normalized = NormalizeConfigName(name);
    if (normalized.empty() || IsDefaultConfigSlot(normalized)) return false;
    const bool wasPreload = LoadPreloadConfig() == normalized;
    std::error_code error;
    const bool removed = fs::remove(kConfigDirectory / normalized, error);
    if (removed && wasPreload) SavePreloadConfig({});
    return removed && !error;
}

void ResetConfigDefaults() {
    g_Esp = EspSettings{};
    g_App = AppSettings{};
    g_Triggerbot = TriggerbotSettings{};
    g_Bhop = BhopSettings{};
    g_InventoryChanger = InventoryChangerSettings{};
    g_Aim = AimSettings{};
}

void SavePreloadConfig(const std::string& name) {
    const std::string normalized = name.empty() ? std::string{} : NormalizeConfigName(name);
    FILE* file = nullptr;
    fopen_s(&file, kPreloadFile.string().c_str(), "w");
    if (!file) return;
    fprintf(file, "%s\n", normalized.empty() ? "none" : normalized.c_str());
    fclose(file);
}

std::string LoadPreloadConfig() {
    EnsureConfigStorage();
    char value[128]{};
    FILE* file = nullptr;
    fopen_s(&file, kPreloadFile.string().c_str(), "r");
    if (file) {
        fscanf_s(file, "%127s", value, static_cast<unsigned>(_countof(value)));
        fclose(file);
        if (_stricmp(value, "none") == 0) return {};
        const std::string normalized = NormalizeConfigName(value);
        std::error_code error;
        if (!normalized.empty() && fs::is_regular_file(kConfigDirectory / normalized, error))
            return normalized;
        return {};
    }

    // Import the original numeric preload selection once when present.
    int legacySlot = 0;
    fopen_s(&file, "preload_slot.txt", "r");
    if (file) {
        if (fscanf_s(file, "%d", &legacySlot) != 1) legacySlot = 0;
        fclose(file);
    }
    if (legacySlot >= 1 && legacySlot <= 3) {
        const std::string imported = "slot" + std::to_string(legacySlot) + ".cfg";
        SavePreloadConfig(imported);
        return imported;
    }
    return {};
}

void SetMenuInputMode(bool menuActive) {
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
