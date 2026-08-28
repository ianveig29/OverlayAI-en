#include "Menu.h"
#include <algorithm>
#include "ModelDiagnostics.h"
#include "Config.h"
#include "imgui.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include "../Glow.h"
#include "SmokeColor.h"
#include "Bhop.h"
#include "Stats.h"
#include "WeaponIcons.h"
#include "InventoryCatalog.h"
#include "InventoryChanger.h"
#include "InventoryLog.h"
#include "InventoryPreview.h"
#include "InventoryStore.h"
#include "InventoryValidator.h"
#include "Localization.h"
#include "TextFonts.h"
#include "UiAppearance.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "BombInfo.h"
#include "SpectatorList.h"

namespace {
    bool g_PanicShutdownRequested = false;
}

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
    case VK_XBUTTON1: return "MOUSE4";
    case VK_XBUTTON2: return "MOUSE5";
    case VK_MENU: return "ALT";
    case VK_SPACE: return "SPACE";
    default:
        if (vk >= 'A' && vk <= 'Z') { snprintf(buf, sizeof(buf), "%c", vk); return buf; }
        if (vk >= '0' && vk <= '9') { snprintf(buf, sizeof(buf), "%c", vk); return buf; }
        snprintf(buf, sizeof(buf), "VK_%d", vk);
        return buf;
    }

    // Debug overlay readouts moved to RenderPersistentWindows()
}


static void DrawColorEdit(const char* label, int* r, int* g, int* b) {
    float col[3] = { *r / 255.0f, *g / 255.0f, *b / 255.0f };
    if (ImGui::ColorEdit3(label, col, ImGuiColorEditFlags_NoInputs)) {
        *r = (int)(col[0] * 255.0f);
        *g = (int)(col[1] * 255.0f);
        *b = (int)(col[2] * 255.0f);
    }
}

static void DrawColorRow(const char* text, const char* id, int* r, int* g, int* b) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    DrawColorEdit(id, r, g, b);
}

static std::string LocalizeOffsetDiagnostic(std::string value) {
    if (GetUiLanguage() != UiLanguage::English) return value;
    const struct Translation {
        const char* spanish;
        const char* english;
    } translations[] = {
        { "Dumper ejecutado y offsets validados correctamente.",
          "Dumper executed and offsets validated successfully." },
        { "Offsets validados cargados desde la cache.",
          "Validated offsets loaded from cache." },
        { "No se encontro cs2-dumper.exe junto a OverlayAI.exe.",
          "cs2-dumper.exe was not found next to OverlayAI.exe." },
        { "No se pudo iniciar cs2-dumper.exe.",
          "cs2-dumper.exe could not be started." },
        { "cs2-dumper excedio el limite de 30 segundos.",
          "cs2-dumper exceeded the 30-second timeout." },
        { "No hubo un par valido; se uso el archivo manual legado.",
          "No valid pair was found; the legacy manual file was used." },
        { "No se encontraron offsets JSON validos.",
          "No valid JSON offsets were found." },
        { " Usando la cache validada.", " Using the validated cache." },
        { "Salida incompleta. Falta: ", "Incomplete output. Missing: " },
        { "Dumper rechazado: ", "Dumper rejected: " },
        { "No se pudo preparar ", "Could not prepare " },
        { "No se pudo publicar ", "Could not publish " },
        { "cs2-dumper termino con codigo ", "cs2-dumper exited with code " }
    };
    for (const Translation& translation : translations) {
        const std::string spanish = translation.spanish;
        const std::size_t position = value.find(spanish);
        if (position != std::string::npos)
            value.replace(position, spanish.size(), translation.english);
    }
    return value;
}

static std::string LocalizeOffsetSource(std::string value) {
    if (GetUiLanguage() != UiLanguage::English) return value;
    if (value == "output (cache validada)") return "output (validated cache)";
    if (value == "cs2-dumper automatico") return "automatic cs2-dumper";
    if (value == "offsets.json legado (sin validar)")
        return "legacy offsets.json (unvalidated)";
    if (value == "valores integrados / INI legado")
        return "built-in values / legacy INI";
    return value;
}

void PollMenuKeyBind() {
    if (!g_App.waitingForMenuKey) return;
    for (int vk = 1; vk < 256; ++vk) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
        if (g_App.panicBindEnabled && vk == g_App.panicVk) continue;
        if (GetAsyncKeyState(vk) & 0x8000) {
            g_App.menuToggleVk = vk;
            g_App.waitingForMenuKey = false;
            break;
        }
    }
}

void PollPanicKeyBind() {
    static bool panicWasDown = false;

    if (g_App.waitingForMenuKey) {
        panicWasDown = false;
        return;
    }

    if (g_App.waitingForPanicKey) {
        panicWasDown = false;
        for (int vk = 1; vk < 256; ++vk) {
            if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
            if (!(GetAsyncKeyState(vk) & 0x8000)) continue;
            if (vk == VK_ESCAPE) {
                g_App.waitingForPanicKey = false;
                return;
            }
            if (vk == g_App.menuToggleVk) continue;

            g_App.panicVk = vk;
            g_App.waitingForPanicKey = false;
            return;
        }
        return;
    }

    if (!g_App.panicBindEnabled || g_App.panicVk <= 0 || g_App.panicVk >= 256 ||
        g_App.panicVk == g_App.menuToggleVk) {
        panicWasDown = false;
        return;
    }

    const bool panicDown = (GetAsyncKeyState(g_App.panicVk) & 0x8000) != 0;
    if (panicDown && !panicWasDown)
        g_PanicShutdownRequested = true;
    panicWasDown = panicDown;
}

bool ConsumePanicShutdownRequest() {
    const bool requested = g_PanicShutdownRequested;
    g_PanicShutdownRequested = false;
    return requested;
}

void RenderPersistentWindows()
{
    // Persistent debug readout window (separate from main menu)
    if (g_Esp.showDebugWindow) {
        ImGui::SetNextWindowSize(ImVec2(390, 260), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug Readouts", &g_Esp.showDebugWindow, ImGuiWindowFlags_NoCollapse)) {
        // Show resolved local pawn address and some fields for verification
        uintptr_t entityList = GetEntityListBase();
        ImGui::Text("EntityList: 0x%p", (void*)entityList);
        uintptr_t localController = 0, localPawn = 0;
        int localTeam = 0, localIndex = -1;

        // Resolve local using existing helper so values match other systems
        ResolveActiveLocal(entityList, g_entityStride, localController, localPawn, localTeam, localIndex);

        ImGui::Text("LocalController: 0x%p", (void*)localController);
        ImGui::Text("LocalPawn: 0x%p", (void*)localPawn);

        if (IsValidPtr(localPawn)) {
            float flashOverlay = mem.Read<float>(localPawn + Offsets::m_flFlashOverlayAlpha);
            float flashMax = mem.Read<float>(localPawn + Offsets::m_flFlashMaxAlpha);
            float flashDur = mem.Read<float>(localPawn + Offsets::m_flFlashDuration);
            float flashBangTime = mem.Read<float>(localPawn + Offsets::m_flFlashBangTime);
            float lastSmoke = mem.Read<float>(localPawn + Offsets::m_flLastSmokeOverlayAlpha);
            uint8_t flashBuildUp = mem.Read<uint8_t>(localPawn + Offsets::m_bFlashBuildUp);

            ImGui::Text("pawn+flashOverlayAlpha: %.6f", flashOverlay);
            ImGui::Text("pawn+flashMaxAlpha: %.6f", flashMax);
            ImGui::Text("pawn+flashDuration: %.6f", flashDur);
            ImGui::Text("pawn+flashBangTime: %.6f", flashBangTime);
            ImGui::Text("pawn+lastSmokeOverlayAlpha: %.6f", lastSmoke);
            ImGui::Text("pawn+flashBuildUp: %u", (unsigned)flashBuildUp);

            // Show process / module state
            ImGui::Separator();
            ImGui::Text("Process attached: %s", mem.hProcess ? "yes" : "no");
            ImGui::Text("PID: %u", mem.pid);
            ImGui::Text("clientModule: 0x%p", (void*)mem.clientModule);

            const BhopTelemetry bhop = GetBhopTelemetry();
            const char* strafe = bhop.strafeDirection < 0 ? "A" :
                (bhop.strafeDirection > 0 ? "D" : "none");
            ImGui::Separator();
            ImGui::Text("Bhop speed: %.1f u/s | vertical: %.1f", bhop.horizontalSpeed, bhop.verticalSpeed);
            ImGui::Text("Ground: %s | jump response: %d ms", bhop.onGround ? "yes" : "no", bhop.jumpResponseMs);
            ImGui::Text("Jumps: %u | retries: %u | strafe: %s",
                bhop.successfulJumps, bhop.groundRetries, strafe);

            ImGui::Separator();
            ImGui::Text("Weapon ESP: OK %d | Fail %d",
                Stats::weaponResolvedCount.load(), Stats::weaponFailedCount.load());
            ImGui::Text("Last weapon: ID %d | ammo %d",
                Stats::weaponLastDefinition.load(), Stats::weaponLastClip.load());
            ImGui::Text("Icon font: %s | last icon: %s",
                IsWeaponIconFontAvailable() ? "loaded" : "missing",
                HasWeaponIcon(Stats::weaponLastDefinition.load()) ? "yes" : "no");
            ImGui::Text("Weapon offsets: attr 0x%llX | item 0x%llX | clip 0x%llX",
                static_cast<unsigned long long>(Offsets::m_AttributeManager),
                static_cast<unsigned long long>(Offsets::m_Item),
                static_cast<unsigned long long>(Offsets::m_iClip1));

        } else {
            ImGui::TextDisabled("Local pawn not resolved - cannot read flash fields.");
        }

            ImGui::End();
        }
    }

    RenderSpectatorListWindow();

    // Bomb Info window is separate and controlled by showBombInfo
    RenderBombInfoWindow();
}

void ApplyOverlayUiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    const UiAppearanceSettings& appearance = GetUiAppearance();
    const auto color = [](const float source[3], float scale, float alpha) {
        return ImVec4(
            std::clamp(source[0] * scale, 0.0f, 1.0f),
            std::clamp(source[1] * scale, 0.0f, 1.0f),
            std::clamp(source[2] * scale, 0.0f, 1.0f), alpha);
    };
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = color(appearance.background, 1.0f, 0.96f);
    colors[ImGuiCol_ChildBg] = color(appearance.background, 1.18f, 0.98f);
    colors[ImGuiCol_PopupBg] = color(appearance.background, 1.12f, 0.98f);
    colors[ImGuiCol_FrameBg] = color(appearance.background, 1.48f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = color(appearance.accent, 0.72f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = color(appearance.accent, 0.88f, 1.0f);
    colors[ImGuiCol_TitleBg] = color(appearance.accent, 0.72f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = color(appearance.accent, 1.0f, 1.0f);
    colors[ImGuiCol_Header] = color(appearance.accent, 0.72f, 0.86f);
    colors[ImGuiCol_HeaderHovered] = color(appearance.accent, 0.92f, 0.92f);
    colors[ImGuiCol_HeaderActive] = color(appearance.accent, 1.05f, 1.0f);
    colors[ImGuiCol_Button] = color(appearance.accent, 0.75f, 0.92f);
    colors[ImGuiCol_ButtonHovered] = color(appearance.accent, 0.96f, 1.0f);
    colors[ImGuiCol_ButtonActive] = color(appearance.accent, 1.08f, 1.0f);
    colors[ImGuiCol_CheckMark] = color(appearance.accent, 1.35f, 1.0f);
    colors[ImGuiCol_SliderGrab] = color(appearance.accent, 1.1f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = color(appearance.accent, 1.35f, 1.0f);
    colors[ImGuiCol_SeparatorActive] = color(appearance.accent, 1.0f, 1.0f);
    colors[ImGuiCol_ResizeGripActive] = color(appearance.accent, 1.0f, 0.95f);
    colors[ImGuiCol_Text] = color(appearance.text, 1.0f, 1.0f);
    colors[ImGuiCol_TextDisabled] = color(appearance.text, 0.58f, 1.0f);
}

void RenderEspMenu() {
    ImGui::SetNextWindowSize(ImVec2(540, 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.96f);
    if (GetUiFont()) ImGui::PushFont(GetUiFont());
    char windowTitle[96]{};
    sprintf_s(windowTitle, "%s###OverlayAIMainMenu",
        g_App.menuTitle[0] ? g_App.menuTitle : "OverlayAI");
    const bool menuVisible = ImGui::Begin(
        windowTitle, &g_MenuOpen, ImGuiWindowFlags_NoCollapse);
    if (menuVisible) {
        ImGui::InputText(Localized("Titulo de la ventana##window_title",
            "Window title##window_title"), g_App.menuTitle, sizeof(g_App.menuTitle));

        if (g_App.waitingForMenuKey)
            ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "%s",
                Localized("Pulsa la tecla para el menu...", "Press the menu key..."));
        if (ImGui::Button(g_App.waitingForMenuKey
                ? Localized("Esperando tecla...##menu_key", "Waiting for key...##menu_key")
                : Localized("Cambiar tecla del menu##menu_key", "Change menu key##menu_key"))) {
            g_App.waitingForPanicKey = false;
            g_App.waitingForMenuKey = true;
        }
        ImGui::SameLine();
        ImGui::Text(Localized("Actual: %s", "Current: %s"), VkToString(g_App.menuToggleVk));

        // Top tabs: Home | VISUALS | AIM | Movement | Inventory Changer | Misc
        // Keep tab order fixed so Inventory Changer stays next to Movement
        if (ImGui::BeginTabBar("##MainTabs")) {
            if (ImGui::BeginTabItem(Localized("Inicio##home", "Home##home"))) {
                const char* languageItems[] = { "Espanol", "English" };
                int language = GetUiLanguage() == UiLanguage::English ? 1 : 0;
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::Combo(Localized("Idioma##ui_language", "Language##ui_language"),
                        &language, languageItems, IM_ARRAYSIZE(languageItems)))
                    SetUiLanguage(language == 1
                        ? UiLanguage::English : UiLanguage::Spanish);

                const char* fontItems[] = {
                    "Moderna (Segoe UI)",
                    "Clasica / Retro (ImGui)"
                };
                ImGui::SetNextItemWidth(200.0f);
                ImGui::Combo(Localized("Tipografia / Fuente##font_mode", "Typography / Font##font_mode"),
                    &g_Esp.fontMode, fontItems, IM_ARRAYSIZE(fontItems));

                ImGui::TextDisabled("%s", Localized(
                    "UTF-8: los nombres de jugadores no dependen del idioma.",
                    "UTF-8: player names are language-independent."));
                ImGui::Separator();

                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s",
                    Localized("Salida de emergencia", "Emergency exit"));
                ImGui::TextWrapped(
                    "%s", Localized(
                        "Cierra OverlayAI de forma ordenada y restaura las modificaciones activas antes de salir.",
                        "Closes OverlayAI cleanly and restores active changes before exiting."));
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.64f, 0.12f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.17f, 0.17f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.08f, 0.08f, 1.0f));
                if (ImGui::Button(Localized("PANIC - Cerrar OverlayAI##panic",
                        "PANIC - Close OverlayAI##panic"),
                        ImVec2(ImGui::GetContentRegionAvail().x, 42.0f)))
                    g_PanicShutdownRequested = true;
                ImGui::PopStyleColor(3);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Checkbox(Localized("Activar tecla Panic##panic_enabled",
                    "Enable Panic key##panic_enabled"), &g_App.panicBindEnabled);

                if (g_App.waitingForPanicKey)
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "%s",
                        Localized("Pulsa una tecla (ESC para cancelar)...",
                            "Press a key (ESC to cancel)..."));
                if (ImGui::Button(g_App.waitingForPanicKey
                        ? Localized("Esperando tecla...##panic_key", "Waiting for key...##panic_key")
                        : Localized("Cambiar tecla Panic##panic_key", "Change Panic key##panic_key"))) {
                    g_App.waitingForMenuKey = false;
                    g_App.waitingForPanicKey = true;
                }
                ImGui::SameLine();
                ImGui::Text(Localized("Actual: %s", "Current: %s"), VkToString(g_App.panicVk));
                ImGui::TextDisabled("%s", Localized(
                    "La tecla del menu no puede utilizarse como Panic.",
                    "The menu key cannot also be used as Panic."));

                ImGui::SeparatorText(Localized("Apariencia", "Appearance"));
                UiAppearanceSettings appearance = GetUiAppearance();
                bool appearanceChanged = false;
                appearanceChanged |= ImGui::ColorEdit3(
                    Localized("Color de acento##ui_accent",
                        "Accent color##ui_accent"), appearance.accent);
                appearanceChanged |= ImGui::ColorEdit3(
                    Localized("Fondo de ventana##ui_background",
                        "Window background##ui_background"), appearance.background);
                appearanceChanged |= ImGui::ColorEdit3(
                    Localized("Color del texto##ui_text",
                        "Text color##ui_text"), appearance.text);
                if (appearanceChanged) {
                    SetUiAppearance(appearance);
                    ApplyOverlayUiStyle();
                }
                if (ImGui::Button(Localized("Restablecer apariencia##reset_ui",
                        "Reset appearance##reset_ui"))) {
                    ResetUiAppearance();
                    ApplyOverlayUiStyle();
                }
                ImGui::TextDisabled("%s", Localized(
                    "La apariencia se guarda independientemente de Configs.",
                    "Appearance is saved independently from Configs."));

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localized("VISUALES##visuals", "VISUALS##visuals"))) {
                static bool openVisualSectionsOnLaunch = true;

                if (openVisualSectionsOnLaunch)
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (ImGui::CollapsingHeader(Localized("Glow interno##internal_glow",
                        "In-game Glow##internal_glow"), ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (g_Esp.enableOtherGlow) ImGui::BeginDisabled();
                    ImGui::Checkbox(Localized("Activar glow##enable_glow",
                        "Enable glow##enable_glow"), &g_Esp.enableGlow);
                    if (g_Esp.enableOtherGlow) ImGui::EndDisabled();
                    if (g_Esp.enableGlow) {
                        ImGui::Checkbox(Localized("Glow en companeros##team_glow",
                            "Teammate glow##team_glow"), &g_Esp.showTeammateGlow);
                        ImGui::Checkbox(Localized("Usar color fijo##glow",
                            "Use fixed color##glow"), &g_Esp.glowUseStaticColor);
                        ImGui::SliderInt(Localized("Opacidad##glow_alpha",
                            "Opacity##glow_alpha"), &g_Esp.glowAlpha, 50, 255);
                    } else if (g_Esp.enableOtherGlow) {
                        ImGui::TextDisabled("%s", Localized(
                            "Desactiva Other Glow para usar el Glow interno.",
                            "Disable Other Glow to use the in-game Glow."));
                    } else {
                        ImGui::TextDisabled("%s", Localized(
                            "Activa Glow para mostrar sus opciones.",
                            "Enable Glow to display its options."));
                    }
                }

                if (ImGui::CollapsingHeader(Localized("Other Glow (overlay)##other_glow_header",
                        "Other Glow (overlay)##other_glow_header"), ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (g_Esp.enableGlow) ImGui::BeginDisabled();
                    ImGui::Checkbox(Localized("Activar Other Glow##enable_other_glow",
                        "Enable Other Glow##enable_other_glow"), &g_Esp.enableOtherGlow);
                    if (g_Esp.enableGlow) ImGui::EndDisabled();
                    if (g_Esp.enableOtherGlow) {
                        g_Esp.otherGlowBodyScale = std::clamp(g_Esp.otherGlowBodyScale, 0.85f, 2.0f);
                        g_Esp.otherGlowSoftness = std::clamp(g_Esp.otherGlowSoftness, 2.0f, 20.0f);
                        g_Esp.otherGlowLayers = std::clamp(g_Esp.otherGlowLayers, 1, 3);
                        ImGui::Checkbox(Localized("Usar color fijo##otherGlow",
                            "Use fixed color##otherGlow"), &g_Esp.otherGlowUseStaticColor);
                        ImGui::SliderInt(Localized("Intensidad##otherGlow",
                            "Intensity##otherGlow"), &g_Esp.otherGlowAlpha, 20, 255);
                        ImGui::SliderFloat(Localized("Volumen corporal##otherGlow",
                            "Body volume##otherGlow"),
                            &g_Esp.otherGlowBodyScale, 0.85f, 2.0f, "%.2f");
                        ImGui::SliderFloat(Localized("Suavidad##otherGlow",
                            "Softness##otherGlow"),
                            &g_Esp.otherGlowSoftness, 2.0f, 20.0f, "%.1f px");
                        ImGui::SliderInt(Localized("Pasadas de blur##otherGlow",
                            "Blur passes##otherGlow"), &g_Esp.otherGlowLayers, 1, 3);
                        ImGui::TextDisabled("%s", Localized(
                            "Usa los colores del Glow. No escribe memoria de render.",
                            "Uses the Glow colors. Does not write render memory."));
                    } else if (g_Esp.enableGlow) {
                        ImGui::TextDisabled("%s", Localized(
                            "Desactiva el Glow interno para usar Other Glow.",
                            "Disable the in-game Glow to use Other Glow."));
                    } else {
                        ImGui::TextDisabled("%s", Localized(
                            "Silueta externa basada en huesos y capsulas.",
                            "External silhouette based on bones and capsules."));
                    }
                }

                if (openVisualSectionsOnLaunch)
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (ImGui::CollapsingHeader("Box", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox(Localized("Mostrar cajas##show_boxes",
                        "Show boxes##show_boxes"), &g_Esp.showBoxes);
                    ImGui::SeparatorText(Localized("Elementos", "Elements"));
                        ImGui::Checkbox(Localized("Mostrar companeros##show_team",
                            "Show teammates##show_team"), &g_Esp.showTeammates);
                        ImGui::Checkbox(Localized("Nombres##show_names",
                            "Names##show_names"), &g_Esp.showNames);
                        ImGui::Checkbox(Localized("Texto de vida##hp_text",
                            "Health text##hp_text"), &g_Esp.showHpText);
                        ImGui::Checkbox(Localized("Barra de vida##hp_bar",
                            "Health bar##hp_bar"), &g_Esp.showHpBar);
                        ImGui::Checkbox(Localized("Texto de chaleco##armor_text",
                            "Armor text##armor_text"), &g_Esp.showArmorText);
                        ImGui::Checkbox(Localized("Barra de chaleco##armor_bar",
                            "Armor bar##armor_bar"), &g_Esp.showArmorBar);
                        ImGui::Checkbox(Localized("Arma actual##active_weapon",
                            "Active weapon##active_weapon"), &g_Esp.showActiveWeapon);
                    if (g_Esp.showActiveWeapon) {
                        ImGui::Indent();
                        const char* weaponModes[] = {
                            Localized("Texto", "Text"),
                            Localized("Icono", "Icon"),
                            Localized("Icono + texto", "Icon + text")
                        };
                        ImGui::Combo(Localized("Visualizacion##weapon",
                            "Display##weapon"), &g_Esp.weaponDisplayMode, weaponModes, 3);
                        if (g_Esp.weaponDisplayMode != 0)
                            ImGui::TextDisabled(Localized("Fuente de iconos: %s",
                                    "Icon font: %s"), IsWeaponIconFontAvailable()
                                ? Localized("cargada", "loaded")
                                : Localized("no disponible (usa texto)",
                                    "unavailable (uses text)"));
                        ImGui::Checkbox(Localized("Mostrar municion actual##weapon_ammo",
                            "Show current ammo##weapon_ammo"), &g_Esp.showWeaponAmmo);
                        if (g_Esp.weaponDisplayMode != 0)
                            ImGui::SliderFloat(Localized("Tamano icono##weapon",
                                "Icon size##weapon"), &g_Esp.weaponIconSize,
                                12.0f, 32.0f, "%.0f px");
                        ImGui::Unindent();
                    }

                    ImGui::SeparatorText(Localized("Equipo", "Equipment"));
                        ImGui::Checkbox(Localized("Portador del C4##c4_carrier",
                            "C4 carrier##c4_carrier"), &g_Esp.showBombCarrier);
                        ImGui::Checkbox(Localized("Kit de desactivacion##defuse_kit",
                            "Defuse kit##defuse_kit"), &g_Esp.showDefuseKits);
                        ImGui::Checkbox(Localized("Indicador de chaleco##armor_indicator",
                            "Armor indicator##armor_indicator"), &g_Esp.showArmorIndicator);
                        ImGui::Checkbox(Localized("Indicador de casco##helmet_indicator",
                            "Helmet indicator##helmet_indicator"), &g_Esp.showHelmetIndicator);
                    if (g_Esp.showBombCarrier || g_Esp.showDefuseKits ||
                        g_Esp.showArmorIndicator || g_Esp.showHelmetIndicator) {
                        ImGui::Indent();
                        const char* equipmentModes[] = {
                            Localized("Texto", "Text"),
                            Localized("Icono", "Icon"),
                            Localized("Icono + texto", "Icon + text")
                        };
                        ImGui::Combo(Localized("Visualizacion##equipment",
                            "Display##equipment"), &g_Esp.equipmentDisplayMode,
                            equipmentModes, 3);
                        if (g_Esp.equipmentDisplayMode != 0)
                            ImGui::SliderFloat(Localized("Tamano iconos##equipment",
                                "Icon size##equipment"), &g_Esp.equipmentIconSize,
                                12.0f, 32.0f, "%.0f px");
                        ImGui::Unindent();
                    }

                    ImGui::SeparatorText(Localized("Apariencia", "Appearance"));
                    ImGui::SliderFloat(Localized("Grosor borde caja##box_outline",
                        "Box outline thickness##box_outline"),
                        &g_Esp.boxOutlineThickness, 0.0f, 5.0f, "%.1f px");
                    if (g_Esp.boxOutlineThickness <= 0.0f)
                        ImGui::TextDisabled("%s", Localized(
                            "Borde desactivado (0)", "Outline disabled (0)"));

                    const char* textPositions[] = {
                        Localized("Arriba", "Top"), Localized("Abajo", "Bottom"),
                        Localized("Izquierda", "Left"), Localized("Derecha", "Right")
                    };
                    ImGui::Combo(Localized("Posicion del nombre##name_position",
                        "Name position##name_position"), &g_Esp.nameTextAnchor, textPositions, 4);
                    ImGui::Combo(Localized("Posicion de vida##health_position",
                        "Health position##health_position"), &g_Esp.hpTextAnchor, textPositions, 4);
                    ImGui::Combo(Localized("Posicion de chaleco##armor_position",
                        "Armor position##armor_position"), &g_Esp.armorTextAnchor, textPositions, 4);
                    ImGui::Combo(Localized("Posicion del arma##weapon_position",
                        "Weapon position##weapon_position"), &g_Esp.weaponTextAnchor, textPositions, 4);
                    ImGui::Combo(Localized("Posicion de indicadores##indicator_position",
                        "Indicator position##indicator_position"),
                        &g_Esp.equipmentTextAnchor, textPositions, 4);
                }

                if (ImGui::CollapsingHeader(Localized("Esqueleto##skeleton_header",
                        "Skeleton##skeleton_header"))) {
                    ImGui::Checkbox(Localized("Mostrar esqueleto##show_skeleton",
                        "Show skeleton##show_skeleton"), &g_Esp.showSkeleton);
                    if (g_Esp.showSkeleton) {
                        const char* skeletonColorModes[] = {
                            Localized("Por visibilidad", "By visibility"),
                            Localized("Color fijo", "Fixed color"),
                            Localized("Heredar color de Box", "Inherit Box color")
                        };
                        ImGui::Combo(Localized("Modo de color##skeleton",
                            "Color mode##skeleton"), &g_Esp.skeletonColorMode,
                            skeletonColorModes, 3);
                        ImGui::SliderInt(Localized("Opacidad##skeleton",
                            "Opacity##skeleton"), &g_Esp.skeletonAlpha, 0, 255);
                        ImGui::SliderFloat(Localized("Grosor##skeleton",
                            "Thickness##skeleton"), &g_Esp.skeletonThickness,
                            0.5f, 6.0f, "%.1f px");
                        ImGui::SliderFloat(Localized("Tamano de articulaciones##skeleton",
                            "Joint size##skeleton"), &g_Esp.skeletonScale,
                            0.5f, 2.0f, "%.2f");
                        ImGui::Checkbox(Localized("Mostrar articulaciones##skeleton_joints",
                            "Show joints##skeleton_joints"), &g_Esp.skeletonShowJoints);
                    } else {
                        ImGui::TextDisabled("%s", Localized(
                            "Activa Skeleton para mostrar sus opciones.",
                            "Enable Skeleton to display its options."));
                    }
                }

                if (openVisualSectionsOnLaunch)
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                if (ImGui::CollapsingHeader(Localized("Colores##colors_header",
                        "Colors##colors_header"), ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::BeginTabBar("##VisualColorTabs")) {
                        if (ImGui::BeginTabItem("Glow")) {
                            if (g_Esp.glowUseStaticColor)
                                ImGui::TextDisabled("%s", Localized(
                                    "Modo activo: color fijo", "Active mode: fixed color"));
                            else
                                ImGui::TextDisabled("%s", Localized(
                                    "Modo activo: visible / oculto", "Active mode: visible / hidden"));
                            if (ImGui::BeginTable("##GlowColors", 2, ImGuiTableFlags_SizingStretchProp)) {
                                ImGui::TableSetupColumn(Localized("Elemento", "Element"), ImGuiTableColumnFlags_WidthStretch, 0.65f);
                                ImGui::TableSetupColumn(Localized("Color", "Color"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
                                if (g_Esp.glowUseStaticColor) {
                                    DrawColorRow(Localized("Color fijo", "Fixed color"), "##glowStatic", &g_Esp.glowStaticR,
                                        &g_Esp.glowStaticG, &g_Esp.glowStaticB);
                                } else {
                                    DrawColorRow(Localized("Visible", "Visible"), "##glowVisible", &g_Esp.glowVisibleR,
                                        &g_Esp.glowVisibleG, &g_Esp.glowVisibleB);
                                    DrawColorRow(Localized("Oculto", "Hidden"), "##glowInvisible", &g_Esp.glowInvisibleR,
                                        &g_Esp.glowInvisibleG, &g_Esp.glowInvisibleB);
                                }
                                ImGui::EndTable();
                            }
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Other Glow")) {
                            if (g_Esp.otherGlowUseStaticColor)
                                ImGui::TextDisabled("%s", Localized(
                                    "Modo activo: color fijo independiente",
                                    "Active mode: independent fixed color"));
                            else
                                ImGui::TextDisabled("%s", Localized(
                                    "Modo activo: visible / oculto compartido",
                                    "Active mode: shared visible / hidden colors"));
                            if (ImGui::BeginTable("##OtherGlowColors", 2, ImGuiTableFlags_SizingStretchProp)) {
                                ImGui::TableSetupColumn(Localized("Elemento", "Element"), ImGuiTableColumnFlags_WidthStretch, 0.65f);
                                ImGui::TableSetupColumn(Localized("Color", "Color"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
                                if (g_Esp.otherGlowUseStaticColor) {
                                    DrawColorRow(Localized("Color fijo", "Fixed color"), "##otherGlowStatic", &g_Esp.otherGlowStaticR,
                                        &g_Esp.otherGlowStaticG, &g_Esp.otherGlowStaticB);
                                } else {
                                    DrawColorRow(Localized("Visible", "Visible"), "##otherGlowVisible", &g_Esp.glowVisibleR,
                                        &g_Esp.glowVisibleG, &g_Esp.glowVisibleB);
                                    DrawColorRow(Localized("Oculto", "Hidden"), "##otherGlowInvisible", &g_Esp.glowInvisibleR,
                                        &g_Esp.glowInvisibleG, &g_Esp.glowInvisibleB);
                                }
                                ImGui::EndTable();
                            }
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Box")) {
                            ImGui::Checkbox(Localized("Color por visibilidad##boxColors",
                                "Color by visibility##boxColors"), &g_Esp.visibilityBoxes);
                            if (ImGui::BeginTable("##BoxColors", 2, ImGuiTableFlags_SizingStretchProp)) {
                                ImGui::TableSetupColumn(Localized("Elemento", "Element"), ImGuiTableColumnFlags_WidthStretch, 0.65f);
                                ImGui::TableSetupColumn(Localized("Color", "Color"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
                                if (g_Esp.visibilityBoxes) {
                                    DrawColorRow(Localized("Visible", "Visible"), "##boxVisible", &g_Esp.boxVisibleR,
                                        &g_Esp.boxVisibleG, &g_Esp.boxVisibleB);
                                    DrawColorRow(Localized("Oculta", "Hidden"), "##boxHidden", &g_Esp.boxHiddenR,
                                        &g_Esp.boxHiddenG, &g_Esp.boxHiddenB);
                                } else {
                                    DrawColorRow(Localized("Enemigo", "Enemy"), "##enemyBox", &g_Esp.enemyBoxR,
                                        &g_Esp.enemyBoxG, &g_Esp.enemyBoxB);
                                    DrawColorRow(Localized("Companero", "Teammate"), "##teamBox", &g_Esp.teamBoxR,
                                        &g_Esp.teamBoxG, &g_Esp.teamBoxB);
                                }
                                ImGui::EndTable();
                            }
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem(Localized("Texto##text_colors",
                                "Text##text_colors"))) {
                            ImGui::Checkbox(Localized("Nombre por visibilidad##nameColors",
                                "Name color by visibility##nameColors"), &g_Esp.visibilityNames);
                            if (ImGui::BeginTable("##TextColors", 2, ImGuiTableFlags_SizingStretchProp)) {
                                ImGui::TableSetupColumn(Localized("Elemento", "Element"), ImGuiTableColumnFlags_WidthStretch, 0.65f);
                                ImGui::TableSetupColumn(Localized("Color", "Color"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
                                DrawColorRow("HP", "##hpText", &g_Esp.hpTextR, &g_Esp.hpTextG, &g_Esp.hpTextB);
                                if (g_Esp.visibilityNames) {
                                    DrawColorRow(Localized("Nombre visible", "Visible name"), "##nameVisible", &g_Esp.nameVisibleR,
                                        &g_Esp.nameVisibleG, &g_Esp.nameVisibleB);
                                    DrawColorRow(Localized("Nombre oculto", "Hidden name"), "##nameHidden", &g_Esp.nameHiddenR,
                                        &g_Esp.nameHiddenG, &g_Esp.nameHiddenB);
                                } else {
                                    DrawColorRow(Localized("Nombre fijo", "Fixed name"), "##nameFixed", &g_Esp.nameTextR,
                                        &g_Esp.nameTextG, &g_Esp.nameTextB);
                                }
                                DrawColorRow(Localized("Chaleco", "Armor"), "##armorBar", &g_Esp.armorBarR,
                                    &g_Esp.armorBarG, &g_Esp.armorBarB);
                                DrawColorRow(Localized("Texto arma", "Weapon text"), "##weaponText", &g_Esp.weaponTextR,
                                    &g_Esp.weaponTextG, &g_Esp.weaponTextB);
                                ImGui::EndTable();
                            }
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem(Localized("Iconos##icon_colors",
                                "Icons##icon_colors"))) {
                            if (ImGui::BeginTable("##IconColors", 2, ImGuiTableFlags_SizingStretchProp)) {
                                ImGui::TableSetupColumn(Localized("Elemento", "Element"), ImGuiTableColumnFlags_WidthStretch, 0.65f);
                                ImGui::TableSetupColumn(Localized("Color", "Color"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
                                DrawColorRow(Localized("Icono arma", "Weapon icon"), "##weaponIcon", &g_Esp.weaponIconR,
                                    &g_Esp.weaponIconG, &g_Esp.weaponIconB);
                                DrawColorRow(Localized("Portador C4", "C4 carrier"), "##bombIcon", &g_Esp.bombIconR,
                                    &g_Esp.bombIconG, &g_Esp.bombIconB);
                                DrawColorRow(Localized("Kit desactivacion", "Defuse kit"), "##defuseIcon", &g_Esp.defuseIconR,
                                    &g_Esp.defuseIconG, &g_Esp.defuseIconB);
                                DrawColorRow(Localized("Chaleco", "Armor"), "##armorIcon", &g_Esp.armorIconR,
                                    &g_Esp.armorIconG, &g_Esp.armorIconB);
                                DrawColorRow(Localized("Casco", "Helmet"), "##helmetIcon", &g_Esp.helmetIconR,
                                    &g_Esp.helmetIconG, &g_Esp.helmetIconB);
                                DrawColorRow(Localized("Fondo indicador", "Indicator background"), "##weaponBackground", &g_Esp.weaponBackgroundR,
                                    &g_Esp.weaponBackgroundG, &g_Esp.weaponBackgroundB);
                                ImGui::EndTable();
                            }
                            ImGui::SliderInt(Localized("Opacidad fondo##weaponBackground",
                                "Background opacity##weaponBackground"), &g_Esp.weaponBackgroundAlpha,
                                0, 255, "%d / 255");
                            ImGui::TextDisabled("%s", Localized(
                                "0 = sin fondo | 255 = fondo solido",
                                "0 = no background | 255 = solid background"));
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Skeleton")) {
                            if (g_Esp.skeletonColorMode == 2) {
                                ImGui::TextDisabled("%s", Localized(
                                    "Skeleton hereda el color activo de Box.",
                                    "Skeleton inherits the active Box color."));
                            } else if (ImGui::BeginTable("##SkeletonColors", 2, ImGuiTableFlags_SizingStretchProp)) {
                                ImGui::TableSetupColumn(Localized("Elemento", "Element"), ImGuiTableColumnFlags_WidthStretch, 0.65f);
                                ImGui::TableSetupColumn(Localized("Color", "Color"), ImGuiTableColumnFlags_WidthStretch, 0.35f);
                                if (g_Esp.skeletonColorMode == 0) {
                                    DrawColorRow(Localized("Visible", "Visible"), "##skeletonVisible", &g_Esp.skeletonVisibleR,
                                        &g_Esp.skeletonVisibleG, &g_Esp.skeletonVisibleB);
                                    DrawColorRow(Localized("Oculto", "Hidden"), "##skeletonHidden", &g_Esp.skeletonHiddenR,
                                        &g_Esp.skeletonHiddenG, &g_Esp.skeletonHiddenB);
                                } else {
                                    DrawColorRow(Localized("Color fijo", "Fixed color"), "##skeletonFixed", &g_Esp.skeletonR,
                                        &g_Esp.skeletonG, &g_Esp.skeletonB);
                                }
                                ImGui::EndTable();
                            }
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                }
                openVisualSectionsOnLaunch = false;

                ImGui::Separator();
                ImGui::Checkbox(Localized("Quitar Punchview##quit_punchview",
                    "Quit Punchview##quit_punchview"), &g_Esp.quitPunchview);

                ImGui::EndTabItem();
            }

            // Inventory Changer tab removed from this position and will be inserted after Movement

            if (ImGui::BeginTabItem(Localized("PUNTERIA##aim_tab", "AIM##aim_tab"))) {
                ImGui::Text("Triggerbot");
                ImGui::Checkbox(Localized("Activar triggerbot##trigger_enabled",
                    "Enable triggerbot##trigger_enabled"), &g_Triggerbot.enabled);
                ImGui::Checkbox(Localized("Requiere tecla pulsada##trigger_hold",
                    "Require held key##trigger_hold"), &g_Triggerbot.requireHoldKey);
                if (g_Triggerbot.waitingForHoldKey)
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Pulsa la tecla del triggerbot...");
                if (ImGui::Button(g_Triggerbot.waitingForHoldKey ? "Esperando tecla..." : "Cambiar tecla triggerbot"))
                    g_Triggerbot.waitingForHoldKey = true;
                ImGui::SameLine();
                ImGui::Text("Actual: %s", VkToString(g_Triggerbot.holdKeyVk));
                ImGui::SliderInt("Retraso (ms)", &g_Triggerbot.delayMs, 0, 500);
                ImGui::Checkbox("Solo si visible", &g_Triggerbot.requireVisible);
                ImGui::Checkbox("Disparar companeros", &g_Triggerbot.shootTeammates);

                // Triggerbot: allow when flashed / in smoke
                ImGui::Checkbox(Localized("Permitir Triggerbot estando flasheado##trigger_flash",
                    "Allow Triggerbot when flashed##trigger_flash"), &g_Triggerbot.allowWhenFlashed);
                ImGui::Checkbox(Localized("Permitir Triggerbot dentro del humo##trigger_smoke",
                    "Allow Triggerbot in smoke##trigger_smoke"), &g_Triggerbot.allowWhenInSmoke);

                ImGui::Separator();
                ImGui::Text("Aimlock");
                ImGui::Checkbox(Localized("Activar aimlock##aim_enabled",
                    "Enable aimlock##aim_enabled"), &g_Aim.enabled);
                ImGui::Checkbox(Localized("Requiere tecla pulsada##aim",
                    "Require held key##aim"), &g_Aim.requireHoldKey);
                if (g_Aim.waitingForHoldKey)
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Pulsa la tecla del aimlock...");
                if (ImGui::Button(g_Aim.waitingForHoldKey ? "Esperando tecla..." : "Cambiar tecla aimlock"))
                    g_Aim.waitingForHoldKey = true;
                ImGui::SameLine();
                ImGui::Text("Actual: %s", VkToString(g_Aim.holdKeyVk));
                ImGui::SliderFloat("FOV sin scope (grados)", &g_Aim.fovDegrees, 0.5f, 45.0f, "%.1f");
                ImGui::Checkbox(Localized("FOV independiente con mira##scoped_fov",
                    "Independent scoped FOV##scoped_fov"), &g_Aim.useScopedFov);
                if (g_Aim.useScopedFov) {
                    ImGui::SliderFloat("Single scope FOV", &g_Aim.singleScopeFovDegrees, 0.25f, 30.0f, "%.2f deg");
                    ImGui::SliderFloat("Double scope FOV", &g_Aim.doubleScopeFovDegrees, 0.25f, 20.0f, "%.2f deg");
                }
                ImGui::SliderFloat("Smoothing", &g_Aim.smoothing, 1.0f, 30.0f, "%.1f");
                ImGui::Checkbox(Localized("Dibujar circulo de FOV##draw_fov",
                    "Draw FOV circle##draw_fov"), &g_Aim.drawFov);
                ImGui::Checkbox(Localized("Solo si es visible##aim",
                    "Only if visible##aim"), &g_Aim.requireVisible);

                const char* aimParts[] = { "Head", "Torso", "Legs", "Auto (Head->Torso->Legs)" };
                ImGui::Combo("Target Part", &g_Aim.targetPart, aimParts, IM_ARRAYSIZE(aimParts));
                // Calibration offsets are shown in Misc when calibrator is enabled
                ImGui::Separator();
                ImGui::TextUnformatted(Localized("Comportamiento anti flash/humo",
                    "Anti-flash/smoke behavior"));
                ImGui::Checkbox(Localized("Permitir Aimlock estando flasheado##aim_flash",
                    "Allow Aimlock when flashed##aim_flash"), &g_Aim.allowWhenFlashed);
                ImGui::Checkbox(Localized("Permitir Aimlock dentro del humo##aim_smoke",
                    "Allow Aimlock in smoke##aim_smoke"), &g_Aim.allowWhenInSmoke);
                ImGui::Checkbox(Localized("Control de retroceso (RCS)##rcs_enabled",
                    "Recoil Control System##rcs_enabled"), &g_Aim.recoilControlSystem);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localized("MOVIMIENTO##movement",
                    "MOVEMENT##movement"))) {
                ImGui::Text("Bunny hop");
                ImGui::Checkbox(Localized("Activar bhop##bhop_enabled",
                    "Enable bhop##bhop_enabled"), &g_Bhop.enabled);
                ImGui::Checkbox(Localized("Requiere tecla pulsada##bhop",
                    "Require held key##bhop"), &g_Bhop.requireHoldKey);
                if (g_Bhop.waitingForHoldKey)
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Pulsa la tecla del bhop...");
                if (ImGui::Button(g_Bhop.waitingForHoldKey ? "Esperando tecla..." : "Cambiar tecla bhop"))
                    g_Bhop.waitingForHoldKey = true;
                ImGui::SameLine();
                ImGui::Text("Actual: %s", VkToString(g_Bhop.holdKeyVk));
                ImGui::Checkbox(Localized("Asistencia de strafe (solo sin A/D)##strafe",
                    "Strafe assist (only without A/D)##strafe"), &g_Bhop.strafeAssist);
                ImGui::TextDisabled("%s", Localized(
                    "Coordina A/D con el giro del mouse durante el aire.",
                    "Coordinates A/D with mouse movement while airborne."));

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localized("INVENTARIO##inventory_tab",
                    "INVENTORY##inventory_tab"))) {
                static ULONGLONG lastInventoryCatalogSyncMs = 0;
                const ULONGLONG inventoryNowMs = GetTickCount64();
                if (lastInventoryCatalogSyncMs == 0 ||
                    inventoryNowMs - lastInventoryCatalogSyncMs >= 1000) {
                    SynchronizeLocalInventoryCatalog();
                    lastInventoryCatalogSyncMs = inventoryNowMs;
                }
                static char catalogSearch[80]{};
                static int categoryFilter = 0;
                static int rarityFilter = 0;
                static int catalogSort = 0;
                static int selectedCatalogIndex = 0;
                static int draftCatalogIndex = -1;
                static LocalInventoryItem draftItem;
                static int editingCollectionSlot = -1;
                static LocalInventoryItem collectionDraft;
                static std::string inventoryFeedback;
                static bool confirmClearCollection = false;
                static int selectedLoadoutTeam = LocalInventoryTeamBoth;
                static bool preciseDraftWear = false;
                static bool preciseSavedWear = false;

                const char* categoryNames[] = {
                    Localized("Todo", "All"), "Music Kits",
                    Localized("Armas", "Weapons"), Localized("Cuchillos", "Knives"),
                    Localized("Guantes", "Gloves"), Localized("Agentes", "Agents"),
                    Localized("Coleccionables", "Collectibles"),
                    Localized("Cajas/Contenedores", "Cases/Containers"),
                    Localized("Llaves", "Keys"), "Stickers"
                };
                const char* rarityNames[] = {
                    Localized("Todas las rarezas", "All rarities"),
                    "Base Grade", "Consumer Grade", "Industrial Grade",
                    "Mil-Spec Grade", "High Grade", "Distinguished", "Restricted",
                    "Remarkable", "Exceptional", "Classified", "Exotic",
                    "Superior", "Covert", "Master", "Default",
                    "Extraordinary", "Contraband"
                };
                const char* sortNames[] = {
                    Localized("Nombre A-Z", "Name A-Z"),
                    Localized("Rareza alta-baja", "Rarity high-low"),
                    Localized("Grupo y nombre", "Group and name"), "Definition ID"
                };
                const int itemCount = CountLocalInventoryItems();
                ImGui::TextUnformatted(Localized("Inventario local", "Local inventory"));
                ImGui::SameLine();
                ImGui::TextDisabled(Localized("Catalogo: %zu | Coleccion: %d/%d",
                        "Catalog: %zu | Collection: %d/%d"),
                    GetInventoryCatalogSize(), itemCount, kMaxLocalInventoryItems);
                ImGui::TextDisabled(
                    "%s", Localized(
                        "La coleccion se guarda con Configs. No crea ni modifica objetos de Steam.",
                        "The collection is saved with Configs. It does not create or modify Steam items."));
                if (ImGui::Checkbox(Localized("Activar coleccion y runtime local##inventory_enabled",
                        "Enable local collection and runtime##inventory_enabled"),
                    &g_InventoryChanger.enabled))
                    RequestInventoryChangerRefresh();
                ImGui::TextDisabled("%s", Localized(
                    "Control maestro: publica la coleccion al Bridge y habilita la aplicacion local.",
                    "Master control: publishes the collection to the Bridge and enables local application."));
                const InventoryChangerStatus& runtimeStatus =
                    GetInventoryChangerStatus();
                ImGui::SeparatorText(Localized("Motor de aplicacion", "Application engine"));
                if (runtimeStatus.backend ==
                    InventoryRuntimeBackend::InjectedBridge) {
                    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.55f, 1.0f),
                        "%s", Localized("Bridge activo (modo completo)",
                            "Bridge active (full mode)"));
                    ImGui::TextWrapped("%s", Localized(
                        "SOCache, Panorama y runtime interno disponibles. El aplicador externo esta suspendido.",
                        "SOCache, Panorama and internal runtime are available. The external applicator is suspended."));
                } else if (runtimeStatus.backend ==
                    InventoryRuntimeBackend::ExternalOverlay) {
                    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                        "%s", Localized("Overlay activo (modo limitado)",
                            "Overlay active (limited mode)"));
                    ImGui::TextWrapped("%s", Localized(
                        "Music Kits por Controller/InventoryServices. Sin SOCache ni coleccion Panorama.",
                        "Music Kits through Controller/InventoryServices. No SOCache or Panorama collection."));
                } else if (runtimeStatus.bridgeActive) {
                    ImGui::TextDisabled("%s", Localized(
                        "Bridge conectado; activa Inventory Changer para publicar la coleccion.",
                        "Bridge connected; enable Inventory Changer to publish the collection."));
                } else {
                    ImGui::TextDisabled("%s", Localized(
                        "Modo Overlay disponible. Equipa un Music Kit y activa Inventory Changer.",
                        "Overlay mode available. Equip a Music Kit and enable Inventory Changer."));
                }
                ImGui::SeparatorText(Localized("Preferencias", "Preferences"));
                if (ImGui::Checkbox(Localized(
                        "Presentar automaticamente los articulos nuevos##auto_reveal",
                        "Automatically present new items##auto_reveal"),
                    &g_InventoryChanger.queueRevealWhenUnavailable))
                    RequestInventoryChangerRefresh();
                if (runtimeStatus.bridgeActive)
                    ImGui::TextDisabled("%s", Localized(
                        "Los articulos se acumulan y NEW ITEM se abre al entrar en Inventario.",
                        "Items are queued and NEW ITEM opens when entering Inventory."));
                else
                    ImGui::TextDisabled("%s", Localized(
                        "La preferencia queda guardada; NEW ITEM requiere el Bridge.",
                        "The preference is saved; NEW ITEM requires the Bridge."));
                if (ImGui::Checkbox(Localized("Aplicar mi cuchillo al controlar bots##bot_knife",
                        "Apply my knife when controlling bots##bot_knife"),
                    &g_InventoryChanger.applyKnivesToControlledBots))
                    RequestInventoryChangerRefresh();
                ImGui::TextDisabled("%s", Localized(
                    "Desactivado: el bot conserva su propio cuchillo (comportamiento realista).",
                    "Disabled: the bot keeps its own knife (realistic behavior)."));
                const int pendingRevealCount =
                    CountPendingLocalInventoryReveals(g_InventoryChanger);
                if (pendingRevealCount > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.28f, 1.0f),
                        Localized("Articulos nuevos pendientes: %d",
                            "Pending new items: %d"), pendingRevealCount);
                }
                ImGui::Separator();

                ImGui::SetNextItemWidth(125.0f);
                ImGui::Combo("##InventoryCategory", &categoryFilter,
                    categoryNames, IM_ARRAYSIZE(categoryNames));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(155.0f);
                ImGui::Combo("##InventoryRarity", &rarityFilter,
                    rarityNames, IM_ARRAYSIZE(rarityNames));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##InventorySort", &catalogSort,
                    sortNames, IM_ARRAYSIZE(sortNames));
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##InventorySearch", Localized(
                        "Buscar nombre, arma, rareza o ID...",
                        "Search name, weapon, rarity or ID..."),
                    catalogSearch, sizeof(catalogSearch));

                static std::vector<int> visibleCatalogItems;
                static int cachedCategoryFilter = -1;
                static int cachedRarityFilter = -1;
                static int cachedCatalogSort = -1;
                static char cachedCatalogSearch[80]{};
                const bool rebuildCatalogView = cachedCategoryFilter != categoryFilter ||
                    cachedRarityFilter != rarityFilter || cachedCatalogSort != catalogSort ||
                    strcmp(cachedCatalogSearch, catalogSearch) != 0;
                if (rebuildCatalogView) {
                    visibleCatalogItems.clear();
                    const int requestedType = categoryFilter - 1;
                    for (std::size_t index = 0; index < GetInventoryCatalogSize(); ++index) {
                        const InventoryCatalogItem* item = GetInventoryCatalogItem(index);
                        if (!item || (requestedType >= 0 && item->type != requestedType) ||
                            (rarityFilter > 0 && strcmp(item->rarity, rarityNames[rarityFilter]) != 0) ||
                            !InventoryCatalogTextMatches(*item, catalogSearch))
                            continue;
                        visibleCatalogItems.push_back(static_cast<int>(index));
                    }
                    std::stable_sort(visibleCatalogItems.begin(), visibleCatalogItems.end(),
                        [](int leftIndex, int rightIndex) {
                            const InventoryCatalogItem* left = GetInventoryCatalogItem(leftIndex);
                            const InventoryCatalogItem* right = GetInventoryCatalogItem(rightIndex);
                            if (!left || !right) return left != nullptr;
                            if (catalogSort == 1) {
                                const int leftRank = GetInventoryRarityRank(left->rarity);
                                const int rightRank = GetInventoryRarityRank(right->rarity);
                                if (leftRank != rightRank) return leftRank > rightRank;
                            } else if (catalogSort == 2) {
                                const int groupOrder = strcmp(left->group, right->group);
                                if (groupOrder != 0) return groupOrder < 0;
                            } else if (catalogSort == 3) {
                                if (left->definitionIndex != right->definitionIndex)
                                    return left->definitionIndex < right->definitionIndex;
                                if (left->paintIndex != right->paintIndex)
                                    return left->paintIndex < right->paintIndex;
                            }
                            return strcmp(left->name, right->name) < 0;
                        });
                    cachedCategoryFilter = categoryFilter;
                    cachedRarityFilter = rarityFilter;
                    cachedCatalogSort = catalogSort;
                    strncpy_s(cachedCatalogSearch, catalogSearch, _TRUNCATE);
                }
                if (!visibleCatalogItems.empty()) {
                    bool selectedIsVisible = false;
                    for (const int index : visibleCatalogItems) {
                        if (index == selectedCatalogIndex) {
                            selectedIsVisible = true;
                            break;
                        }
                    }
                    if (!selectedIsVisible)
                        selectedCatalogIndex = visibleCatalogItems.front();
                }

                if (ImGui::BeginTable("##InventoryCatalogLayout", 2,
                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Catalogo", ImGuiTableColumnFlags_WidthStretch, 0.55f);
                    ImGui::TableSetupColumn("Articulo", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled(Localized("%d resultados", "%d results"),
                        static_cast<int>(visibleCatalogItems.size()));
                    ImGui::BeginChild("##InventoryCatalogList", ImVec2(0.0f, 245.0f), true);
                    ImGuiListClipper catalogClipper;
                    catalogClipper.Begin(static_cast<int>(visibleCatalogItems.size()));
                    while (catalogClipper.Step()) {
                        for (int row = catalogClipper.DisplayStart;
                            row < catalogClipper.DisplayEnd; ++row) {
                            const int catalogIndex = visibleCatalogItems[row];
                            const InventoryCatalogItem* item = GetInventoryCatalogItem(catalogIndex);
                            if (!item) continue;
                            ImGui::PushID(catalogIndex);
                            char label[160]{};
                            sprintf_s(label, "[%s] %s", GetLocalInventoryItemTypeName(item->type), item->name);
                            if (ImGui::Selectable(label, selectedCatalogIndex == catalogIndex))
                                selectedCatalogIndex = catalogIndex;
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s\n%s | %s", item->name, item->group, item->rarity);
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();

                    ImGui::TableNextColumn();
                    const InventoryCatalogItem* catalogItem = visibleCatalogItems.empty()
                        ? nullptr
                        : GetInventoryCatalogItem(static_cast<std::size_t>(
                            selectedCatalogIndex < 0 ? 0 : selectedCatalogIndex));
                    if (catalogItem) {
                        if (draftCatalogIndex != selectedCatalogIndex) {
                            draftCatalogIndex = selectedCatalogIndex;
                            draftItem = {};
                            draftItem.occupied = true;
                            draftItem.type = catalogItem->type;
                            draftItem.definitionIndex = catalogItem->definitionIndex;
                            draftItem.paintIndex = catalogItem->paintIndex;
                            draftItem.wear = std::clamp(
                                0.15f, catalogItem->minWear, catalogItem->maxWear);
                            strncpy_s(draftItem.displayName, catalogItem->name, _TRUNCATE);
                        }

                        const uint32_t rgb = catalogItem->rarityColor;
                        const ImVec4 rarityColor(
                            static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                            static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                            static_cast<float>(rgb & 0xFF) / 255.0f, 1.0f);
                        RequestInventoryPreview(selectedCatalogIndex, catalogItem->imageUrl);
                        ID3D11ShaderResourceView* previewTexture = GetInventoryPreviewTexture();
                        const InventoryPreviewInfo previewInfo = GetInventoryPreviewInfo();
                        if (previewTexture && previewInfo.width > 0 && previewInfo.height > 0) {
                            const float availableWidth = ImGui::GetContentRegionAvail().x;
                            const float scale = (std::min)(
                                availableWidth / static_cast<float>(previewInfo.width),
                                96.0f / static_cast<float>(previewInfo.height));
                            const ImVec2 previewSize(
                                static_cast<float>(previewInfo.width) * scale,
                                static_cast<float>(previewInfo.height) * scale);
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                (availableWidth - previewSize.x) * 0.5f);
                            ImGui::Image(
                                static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(previewTexture)),
                                previewSize);
                        } else if (previewInfo.state == InventoryPreviewState::Loading) {
                            ImGui::TextDisabled("%s", Localized("Cargando imagen...", "Loading image..."));
                        } else if (previewInfo.state == InventoryPreviewState::Failed) {
                            ImGui::TextDisabled("%s", previewInfo.error);
                            if (ImGui::SmallButton(Localized("Reintentar imagen##retry_image",
                                    "Retry image##retry_image")))
                                RetryInventoryPreview();
                        }
                        ImGui::TextColored(rarityColor, "%s", catalogItem->name);
                        ImGui::TextDisabled("%s | %s", catalogItem->group, catalogItem->rarity);
                        ImGui::Text("Definition: %d", catalogItem->definitionIndex);
                        if (catalogItem->paintIndex > 0)
                            ImGui::Text("%s: %d",
                                GetInventoryCatalogItemVariantName(*catalogItem),
                                catalogItem->paintIndex);

                        const bool hasFinish =
                            IsInventoryCatalogItemWearCustomizable(*catalogItem);
                        if (hasFinish) {
                            ImGui::SliderFloat(Localized("Desgaste##draft_wear",
                                    "Wear##draft_wear"), &draftItem.wear,
                                catalogItem->minWear, catalogItem->maxWear, "%.6f");
                            ImGui::Checkbox(Localized(
                                    "Introducir desgaste exacto##draft_precise_wear",
                                    "Enter exact wear##draft_precise_wear"),
                                &preciseDraftWear);
                            if (preciseDraftWear) {
                                ImGui::SetNextItemWidth(-1.0f);
                                ImGui::InputFloat(Localized(
                                        "Valor exacto##draft_wear_value",
                                        "Exact value##draft_wear_value"),
                                    &draftItem.wear, 0.0f, 0.0f, "%.6f");
                            }
                            draftItem.wear = std::clamp(draftItem.wear,
                                catalogItem->minWear, catalogItem->maxWear);
                            ImGui::InputInt(Localized("Semilla##draft_seed",
                                "Seed##draft_seed"), &draftItem.seed);
                            draftItem.seed = std::clamp(draftItem.seed, 0, 1000);
                        }

                        const bool supportsStatTrak =
                            IsInventoryCatalogItemStatTrakAllowed(*catalogItem);
                        if (supportsStatTrak) {
                            if (ImGui::Checkbox("StatTrak", &draftItem.statTrak)) {
                                if (draftItem.statTrak)
                                    draftItem.souvenir = false;
                                else
                                    draftItem.statTrakCount = 0;
                            }
                        }
                        if (catalogItem->souvenirAllowed) {
                            ImGui::SameLine();
                            if (ImGui::Checkbox("Souvenir", &draftItem.souvenir) && draftItem.souvenir) {
                                draftItem.statTrak = false;
                                draftItem.statTrakCount = 0;
                            }
                        }
                        if (draftItem.statTrak) {
                            ImGui::InputInt(
                                draftItem.type == LocalInventoryMusicKit
                                    ? Localized("MVPs StatTrak", "StatTrak MVPs")
                                    : Localized("Bajas StatTrak", "StatTrak kills"),
                                &draftItem.statTrakCount);
                            draftItem.statTrakCount = std::clamp(
                                draftItem.statTrakCount, 0, 999999999);
                        }

                        if (ImGui::Button(Localized("Anadir a mi coleccion##add_collection",
                                "Add to my collection##add_collection"), ImVec2(-1.0f, 0.0f))) {
                            const int previousCount = CountLocalInventoryItems();
                            const int slot = AddLocalInventoryItem(draftItem);
                            if (slot < 0) {
                                inventoryFeedback = previousCount >= kMaxLocalInventoryItems
                                    ? "La coleccion esta llena."
                                    : "La combinacion seleccionada no es valida.";
                            } else {
                                if (CountLocalInventoryItems() <= previousCount) {
                                    inventoryFeedback =
                                        "No se pudo crear la instancia local.";
                                } else if (!g_InventoryChanger.queueRevealWhenUnavailable) {
                                    inventoryFeedback =
                                        "Articulo anadido. NEW ITEM automatico esta desactivado.";
                                } else {
                                    inventoryFeedback =
                                        "Articulo anadido; NEW ITEM quedo encolado.";
                                }
                            }
                        }
                        if (IsInventoryItemExternallyApplicable(catalogItem->type))
                            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.55f, 1.0f),
                                "%s", Localized("Aplicacion externa disponible.",
                                    "External application available."));
                        else if (catalogItem->type == LocalInventoryAgent)
                            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.55f, 1.0f),
                                "%s", Localized("Runtime local disponible para su faccion.",
                                    "Local runtime available for its faction."));
                        else if (catalogItem->type == LocalInventoryKnife)
                            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.55f, 1.0f),
                                "%s", Localized("Runtime local disponible.",
                                    "Local runtime available."));
                        else if (IsInventoryItemLoadoutSupported(catalogItem->type))
                            ImGui::TextDisabled("%s", Localized(
                                "Loadout local disponible; runtime del modelo pendiente.",
                                "Local loadout available; model runtime pending."));
                        else if (IsInventoryItemNativeCollectionSupported(
                                catalogItem->type))
                            ImGui::TextColored(
                                ImVec4(0.35f, 1.0f, 0.55f, 1.0f),
                                "%s", Localized(
                                    "Coleccion nativa y NEW ITEM disponibles mediante el Bridge.",
                                    "Native collection and NEW ITEM are available through the Bridge."));
                        else
                            ImGui::TextDisabled("%s", Localized(
                                "Catalogo local: aplicacion reservada para la fase interna.",
                                "Local catalog: application reserved for the internal phase."));
                    } else {
                        ImGui::TextDisabled("%s", Localized(
                            "No hay articulos que coincidan con el filtro.",
                            "No items match the current filter."));
                    }
                    ImGui::EndTable();
                }

                if (!inventoryFeedback.empty())
                    ImGui::TextWrapped("%s", inventoryFeedback.c_str());

                ImGui::SeparatorText(Localized("Mi coleccion local", "My local collection"));
                if (ImGui::BeginTable("##InventoryCollectionLayout", 2,
                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn(Localized("Coleccion", "Collection"),
                        ImGuiTableColumnFlags_WidthStretch, 0.55f);
                    ImGui::TableSetupColumn(Localized("Acciones", "Actions"),
                        ImGuiTableColumnFlags_WidthStretch, 0.45f);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::BeginChild("##InventoryCollectionList", ImVec2(0.0f, 175.0f), true);
                    if (itemCount == 0) {
                        ImGui::TextDisabled("%s", Localized(
                            "La coleccion esta vacia.", "The collection is empty."));
                    } else {
                        for (int slot = 0; slot < kMaxLocalInventoryItems; ++slot) {
                            const LocalInventoryItem& item = g_InventoryChanger.items[slot];
                            if (!item.occupied) continue;
                            ImGui::PushID(slot);
                            char label[160]{};
                            const char* teamTag = item.equippedTeam ==
                                LocalInventoryTeamTerrorist ? "[T] " :
                                item.equippedTeam ==
                                    LocalInventoryTeamCounterTerrorist ? "[CT] " :
                                item.equippedTeam == LocalInventoryTeamBoth
                                    ? "[T/CT] " : "";
                            sprintf_s(label, "%s%s%s | %s",
                                IsLocalInventoryItemEquippedById(item.localId)
                                    ? Localized("[EQUIPADO] ", "[EQUIPPED] ") : "",
                                teamTag,
                                GetLocalInventoryItemTypeName(item.type), item.displayName);
                            if (ImGui::Selectable(label,
                                g_InventoryChanger.selectedLocalId == item.localId))
                                SelectLocalInventoryItemById(item.localId);
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();

                    ImGui::TableNextColumn();
                    const int selectedSlot = GetSelectedLocalInventorySlot();
                    const bool hasSelection = selectedSlot >= 0 &&
                        selectedSlot < kMaxLocalInventoryItems &&
                        g_InventoryChanger.items[selectedSlot].occupied;
                    if (hasSelection) {
                        const LocalInventoryItem selectedItem = g_InventoryChanger.items[selectedSlot];
                        const InventoryCatalogItem* selectedCatalogItem = FindInventoryCatalogItem(
                            selectedItem.type, selectedItem.definitionIndex, selectedItem.paintIndex);
                        if (editingCollectionSlot != selectedSlot) {
                            editingCollectionSlot = selectedSlot;
                            collectionDraft = selectedItem;
                            if (selectedItem.type == LocalInventoryAgent && selectedCatalogItem)
                                selectedLoadoutTeam = GetInventoryCatalogItemTeam(
                                    *selectedCatalogItem);
                            else
                                selectedLoadoutTeam = LocalInventoryTeamBoth;
                        }
                        char validationReason[128]{};
                        if (selectedCatalogItem &&
                            IsInventoryCatalogItemWearCustomizable(
                                *selectedCatalogItem)) {
                            ImGui::SliderFloat(Localized("Desgaste guardado##saved_wear",
                                    "Saved wear##saved_wear"), &collectionDraft.wear,
                                selectedCatalogItem->minWear, selectedCatalogItem->maxWear, "%.6f");
                            ImGui::Checkbox(Localized(
                                    "Editar desgaste exacto##saved_precise_wear",
                                    "Edit exact wear##saved_precise_wear"),
                                &preciseSavedWear);
                            if (preciseSavedWear) {
                                ImGui::SetNextItemWidth(-1.0f);
                                ImGui::InputFloat(Localized(
                                        "Valor exacto guardado##saved_wear_value",
                                        "Saved exact value##saved_wear_value"),
                                    &collectionDraft.wear, 0.0f, 0.0f,
                                    "%.6f");
                            }
                            collectionDraft.wear = std::clamp(
                                collectionDraft.wear,
                                selectedCatalogItem->minWear,
                                selectedCatalogItem->maxWear);
                            ImGui::InputInt(Localized("Semilla guardada##saved_seed",
                                "Saved seed##saved_seed"), &collectionDraft.seed);
                            collectionDraft.seed = std::clamp(collectionDraft.seed, 0, 1000);
                        }
                        if (selectedCatalogItem &&
                            IsInventoryCatalogItemStatTrakAllowed(*selectedCatalogItem)) {
                            if (ImGui::Checkbox(Localized("StatTrak guardado##saved_stattrak",
                                "Saved StatTrak##saved_stattrak"), &collectionDraft.statTrak)) {
                                if (collectionDraft.statTrak)
                                    collectionDraft.souvenir = false;
                                else
                                    collectionDraft.statTrakCount = 0;
                            }
                        }
                        if (selectedCatalogItem && selectedCatalogItem->souvenirAllowed) {
                            if (ImGui::Checkbox(Localized("Souvenir guardado##saved_souvenir",
                                "Saved Souvenir##saved_souvenir"), &collectionDraft.souvenir) &&
                                collectionDraft.souvenir) {
                                collectionDraft.statTrak = false;
                                collectionDraft.statTrakCount = 0;
                            }
                        }
                        if (collectionDraft.statTrak) {
                            ImGui::InputInt(
                                collectionDraft.type == LocalInventoryMusicKit
                                    ? Localized("MVPs StatTrak guardados",
                                        "Saved StatTrak MVPs")
                                    : Localized("Bajas StatTrak guardadas",
                                        "Saved StatTrak kills"),
                                &collectionDraft.statTrakCount);
                            collectionDraft.statTrakCount = std::clamp(
                                collectionDraft.statTrakCount, 0, 999999999);
                        }
                        const bool validItem = ValidateLocalInventoryItem(
                            collectionDraft, validationReason, sizeof(validationReason));
                        ImGui::TextWrapped("%s", selectedItem.displayName);
                        ImGui::TextDisabled("%s",
                            GetLocalInventoryItemTypeName(selectedItem.type));
                        if (g_Esp.showDebug)
                            ImGui::TextDisabled("Definition %d | Local ID %llu",
                                selectedItem.definitionIndex,
                                static_cast<unsigned long long>(selectedItem.localId));
                        ImGui::TextDisabled(Localized("Estado: %s", "Status: %s"),
                            GetLocalInventoryValidityName(selectedItem.validity));
                        if (selectedCatalogItem && selectedItem.paintIndex > 0) {
                            if (IsInventoryCatalogItemWearCustomizable(
                                    *selectedCatalogItem))
                                ImGui::TextDisabled("%s %d | Wear %.6f | Seed %d",
                                    GetInventoryCatalogItemVariantName(
                                        *selectedCatalogItem),
                                    selectedItem.paintIndex, selectedItem.wear,
                                    selectedItem.seed);
                            else
                                ImGui::TextDisabled("%s %d",
                                    GetInventoryCatalogItemVariantName(
                                        *selectedCatalogItem),
                                    selectedItem.paintIndex);
                            if (selectedItem.souvenir) ImGui::TextColored(
                                ImVec4(1.0f, 0.8f, 0.25f, 1.0f), "Souvenir");
                        }
                        if (selectedItem.statTrak) ImGui::TextColored(
                            ImVec4(1.0f, 0.55f, 0.15f, 1.0f),
                            selectedItem.type == LocalInventoryMusicKit
                                ? "StatTrak: %d MVPs"
                                : Localized("StatTrak: %d bajas", "StatTrak: %d kills"),
                            selectedItem.statTrakCount);
                        if (!validItem)
                            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                "%s", validationReason);

                        const bool supportsVariants = selectedCatalogItem &&
                            (selectedItem.paintIndex > 0 ||
                                IsInventoryCatalogItemStatTrakAllowed(
                                    *selectedCatalogItem) ||
                                selectedCatalogItem->souvenirAllowed);
                        if (!supportsVariants || !validItem) ImGui::BeginDisabled();
                        if (ImGui::Button(Localized("Guardar cambios##save_item",
                                "Save changes##save_item"))) {
                            inventoryFeedback = UpdateLocalInventoryItem(selectedSlot, collectionDraft)
                                ? Localized("Cambios guardados en la coleccion.",
                                    "Changes saved to the collection.")
                                : Localized("No se pudo guardar una combinacion invalida.",
                                    "The invalid combination could not be saved.");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(Localized("Duplicar variante##duplicate_item",
                                "Duplicate variant##duplicate_item"))) {
                            const int previousCount = CountLocalInventoryItems();
                            const int duplicatedSlot = AddLocalInventoryItem(collectionDraft);
                            if (duplicatedSlot < 0) {
                                inventoryFeedback = Localized(
                                    "No se pudo duplicar la variante.",
                                    "The variant could not be duplicated.");
                            } else {
                                editingCollectionSlot = -1;
                                inventoryFeedback = CountLocalInventoryItems() > previousCount
                                    ? Localized("Instancia independiente duplicada.",
                                        "Independent instance duplicated.")
                                    : Localized("No se pudo duplicar la instancia.",
                                        "The instance could not be duplicated.");
                            }
                        }
                        if (!supportsVariants || !validItem) ImGui::EndDisabled();

                        if (selectedItem.type == LocalInventoryKnife) {
                            int teamIndex = selectedLoadoutTeam == LocalInventoryTeamTerrorist
                                ? 0 : selectedLoadoutTeam == LocalInventoryTeamCounterTerrorist ? 1 : 2;
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted(Localized("Equipar para:", "Equip for:"));
                            ImGui::SameLine();
                            ImGui::RadioButton("T", &teamIndex, 0);
                            ImGui::SameLine();
                            ImGui::RadioButton("CT", &teamIndex, 1);
                            ImGui::SameLine();
                            ImGui::RadioButton(Localized("Ambos##both_teams",
                                "Both##both_teams"), &teamIndex, 2);
                            selectedLoadoutTeam = teamIndex == 0
                                ? LocalInventoryTeamTerrorist
                                : teamIndex == 1
                                    ? LocalInventoryTeamCounterTerrorist
                                    : LocalInventoryTeamBoth;
                        } else if (selectedItem.type == LocalInventoryAgent && selectedCatalogItem) {
                            selectedLoadoutTeam = GetInventoryCatalogItemTeam(*selectedCatalogItem);
                            ImGui::TextDisabled(Localized("Equipo compatible: %s",
                                    "Compatible team: %s"),
                                selectedLoadoutTeam == LocalInventoryTeamTerrorist
                                    ? Localized("Terrorista", "Terrorist")
                                    : Localized("Anti-Terrorista", "Counter-Terrorist"));
                        } else if (selectedItem.type == LocalInventoryMusicKit) {
                            selectedLoadoutTeam = LocalInventoryTeamBoth;
                        }

                        const bool loadoutSupported = validItem && selectedCatalogItem &&
                            IsInventoryItemLoadoutSupported(selectedItem.type) &&
                            CanInventoryCatalogItemEquipForTeam(
                                *selectedCatalogItem, selectedLoadoutTeam);
                        if (!loadoutSupported) ImGui::BeginDisabled();
                        const char* equipLabel = selectedItem.type == LocalInventoryMusicKit
                            ? Localized("Equipar localmente##equip_item",
                                "Equip locally##equip_item")
                            : Localized("Equipar en loadout##equip_item",
                                "Equip in loadout##equip_item");
                        if (ImGui::Button(equipLabel)) {
                            if (!EquipLocalInventoryItemById(
                                selectedItem.localId, selectedLoadoutTeam)) {
                                inventoryFeedback = Localized(
                                    "No se pudo equipar el articulo local.",
                                    "The local item could not be equipped.");
                            } else if (selectedItem.type == LocalInventoryMusicKit) {
                                inventoryFeedback = runtimeStatus.bridgeActive
                                    ? Localized("Music Kit equipado mediante el Bridge.",
                                        "Music Kit equipped through the Bridge.")
                                    : Localized("Music Kit equipado mediante el modo Overlay.",
                                        "Music Kit equipped through Overlay mode.");
                            } else {
                                inventoryFeedback = runtimeStatus.bridgeActive
                                    ? Localized("Loadout enviado al Bridge; esperando el refresco del juego.",
                                        "Loadout sent to the Bridge; waiting for the game to refresh.")
                                    : Localized("Loadout guardado; conecta el Bridge para aplicarlo.",
                                        "Loadout saved; connect the Bridge to apply it.");
                            }
                        }
                        if (!loadoutSupported) ImGui::EndDisabled();
                        ImGui::SameLine();
                        const bool equippedForTeam = selectedItem.equippedTeam ==
                            LocalInventoryTeamBoth || selectedItem.equippedTeam ==
                            selectedLoadoutTeam;
                        if (!equippedForTeam) ImGui::BeginDisabled();
                        if (ImGui::Button(Localized("Desequipar##unequip_item",
                                "Unequip##unequip_item"))) {
                            if (UnequipLocalInventoryItemSelection(
                                selectedItem.type, selectedLoadoutTeam))
                                inventoryFeedback = Localized(
                                    "Articulo desequipado del loadout local.",
                                    "Item removed from the local loadout.");
                        }
                        if (!equippedForTeam) ImGui::EndDisabled();

                        if (ImGui::Button(Localized("Eliminar seleccionado##delete_item",
                                "Delete selected##delete_item"))) {
                            RemoveLocalInventoryItem(selectedSlot);
                            editingCollectionSlot = -1;
                            inventoryFeedback = Localized("Articulo eliminado.",
                                "Item deleted.");
                        }
                    } else {
                        ImGui::TextDisabled("%s", Localized(
                            "Selecciona un articulo de tu coleccion.",
                            "Select an item from your collection."));
                    }
                    ImGui::EndTable();
                }

                if (itemCount > 0) {
                    if (!confirmClearCollection) {
                        if (ImGui::Button(Localized("Vaciar coleccion##clear_collection",
                                "Clear collection##clear_collection")))
                            confirmClearCollection = true;
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                            "%s", Localized("Confirmar eliminacion de toda la coleccion?",
                                "Delete the entire collection?"));
                        if (ImGui::Button(Localized("Si, vaciar##confirm_clear",
                                "Yes, clear##confirm_clear"))) {
                            ClearLocalInventoryItems();
                            confirmClearCollection = false;
                            inventoryFeedback = Localized("Coleccion eliminada.",
                                "Collection cleared.");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(Localized("Cancelar##cancel_clear",
                                "Cancel##cancel_clear"))) confirmClearCollection = false;
                    }
                }

                const InventoryChangerStatus& status = GetInventoryChangerStatus();
                ImGui::SeparatorText(Localized("Estado de aplicacion", "Application status"));
                const bool hasEquippedMusicKit =
                    g_InventoryChanger.loadout.musicKit != kInvalidLocalItemId;
                if (status.backend == InventoryRuntimeBackend::InjectedBridge &&
                    hasEquippedMusicKit) {
                    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.55f, 1.0f),
                        Localized("Music Kit %d delegado al Bridge.",
                            "Music Kit %d delegated to the Bridge."),
                        status.selectedDefinitionIndex);
                    ImGui::TextDisabled("%s", Localized(
                        "El estado visual, SOCache y Panorama son propiedad del Bridge.",
                        "Visual state, SOCache and Panorama are managed by the Bridge."));
                } else if (status.applied) {
                    ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.55f, 1.0f),
                        Localized("Music Kit %d aplicado localmente.",
                            "Music Kit %d applied locally."), status.selectedDefinitionIndex);
                } else if (hasEquippedMusicKit) {
                    if (status.selectionValid && status.localController == 0)
                        ImGui::TextDisabled("%s", Localized(
                            "Menu principal: se aplicara al crear un LocalController.",
                            "Main menu: it will be applied when a LocalController is created."));
                    else
                        ImGui::TextDisabled(status.selectionValid
                            ? Localized("Esperando LocalController/InventoryServices...",
                                "Waiting for LocalController/InventoryServices...")
                            : Localized("El articulo equipado ya no es valido.",
                                "The equipped item is no longer valid."));
                } else {
                    ImGui::TextDisabled("%s", Localized(
                        "No hay un Music Kit aplicable equipado.",
                        "No applicable Music Kit is equipped."));
                }
                if (g_Esp.showDebug) {
                    ImGui::TextDisabled("Controller ID: %d | Service ID: %u",
                        status.currentControllerMusicId,
                        static_cast<unsigned>(status.currentServiceMusicId));
                    const FrameSnapshot& inventoryFrame = GetCurrentFrameSnapshot();
                    ImGui::TextDisabled(
                        "Account Controller: 0x%llX | Active Controller: 0x%llX",
                        static_cast<unsigned long long>(status.localController),
                        static_cast<unsigned long long>(inventoryFrame.localController));
                    ImGui::TextDisabled("InventoryServices: 0x%llX",
                        static_cast<unsigned long long>(status.inventoryServices));
                }
                if (hasEquippedMusicKit && ImGui::Button(Localized(
                        "Reaplicar Music Kit##reapply_music",
                        "Reapply Music Kit##reapply_music"))) {
                    RequestMusicKitReapply();
                    inventoryFeedback = status.bridgeActive
                        ? Localized("Reaplicacion solicitada al Bridge.",
                            "Reapplication requested from the Bridge.")
                        : Localized("Reaplicacion externa solicitada.",
                            "External reapplication requested.");
                }
                ImGui::TextDisabled(status.bridgeActive
                    ? Localized("Menu principal disponible mediante SOCache/Panorama del Bridge.",
                        "Main menu available through the Bridge SOCache/Panorama integration.")
                    : Localized("El modo Overlay no puede integrar el kit en la coleccion del menu principal.",
                        "Overlay mode cannot integrate the kit into the main-menu collection."));
                if (g_InventoryChanger.loadout.terroristAgent != kInvalidLocalItemId ||
                    g_InventoryChanger.loadout.counterTerroristAgent != kInvalidLocalItemId)
                    ImGui::TextDisabled(
                        "Agentes: runtime local activo y separado por faccion.");
                const auto findLoadoutItem = [](LocalItemId localId)
                    -> const LocalInventoryItem* {
                    if (localId == kInvalidLocalItemId) return nullptr;
                    for (const LocalInventoryItem& item : g_InventoryChanger.items) {
                        if (item.occupied && item.localId == localId) return &item;
                    }
                    return nullptr;
                };
                const LocalInventoryItem* equippedTKnife = findLoadoutItem(
                    g_InventoryChanger.loadout.terroristKnife);
                const LocalInventoryItem* equippedCTKnife = findLoadoutItem(
                    g_InventoryChanger.loadout.counterTerroristKnife);
                const bool hasKnifeLoadout = equippedTKnife || equippedCTKnife;
                if (hasKnifeLoadout)
                    ImGui::TextDisabled(
                        "Cuchillos: runtime local activo para todo el catalogo.");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localized("OTROS##misc", "MISC##misc"))) {
                ImGui::Separator();
                ImGui::TextUnformatted(Localized("Visuales adicionales", "Additional visuals"));
                ImGui::Checkbox(Localized("Radar##radar", "Radar##radar"), &g_Esp.enableRadarHack);
                ImGui::Checkbox(Localized("Mostrar punto de mira##crosshair",
                    "Show crosshair##crosshair"), &g_Esp.showCrosshair);
                ImGui::Checkbox(Localized("Mostrar espectadores##spectators",
                    "Show spectator list##spectators"), &g_Esp.showSpectatorList);
                ImGui::Checkbox(Localized("Mostrar informacion de la bomba##bomb_info",
                    "Show bomb info##bomb_info"), &g_Esp.showBombInfo);
                if (g_Esp.showBombInfo) {
                    ImGui::Indent(20.0f);
                    ImGui::Checkbox(Localized("Mostrar sitio (A / B)##bomb_site",
                        "Show bomb site (A / B)##bomb_site"), &g_Esp.bombInfoShowSite);
                    ImGui::Checkbox(Localized("Mostrar temporizador y barra de detonacion##bomb_timer",
                        "Show timer & explosion bar##bomb_timer"), &g_Esp.bombInfoShowTimer);
                    ImGui::Checkbox(Localized("Mostrar estado de desactivacion y kit##bomb_defuse",
                        "Show defuse status & kit##bomb_defuse"), &g_Esp.bombInfoShowDefusing);
                    ImGui::Checkbox(Localized("Mostrar decision ('RUN or KEEP DEFUSING')##bomb_decision",
                        "Show decision ('RUN or KEEP DEFUSING')##bomb_decision"), &g_Esp.bombInfoShowDecision);
                    ImGui::Checkbox(Localized("Ajuste automatico de ventana##bomb_autoresize",
                        "Auto resize window##bomb_autoresize"), &g_Esp.bombInfoAutoResize);
                    ImGui::TextDisabled("%s", Localized(
                        "Oculto automaticamente hasta que se planta la bomba.",
                        "Automatically hidden until bomb is planted."));
                    ImGui::Unindent(20.0f);
                }
                ImGui::Checkbox(Localized("Trayectoria de granadas##grenade_path",
                    "Grenade trajectory##grenade_path"), &g_Esp.showGrenadeTrajectory);
                if (g_Esp.showGrenadeTrajectory) {
                    const char* trajectoryModes[] = {
                        "Siempre con granada equipada", "Solo al mantener lanzamiento"
                    };
                    ImGui::Combo("Mostrar trayectoria", &g_Esp.grenadeTrajectoryMode,
                        trajectoryModes, 2);
                }

                ImGui::Separator();
                ImGui::Checkbox(Localized("Activar anti humo##anti_smoke",
                    "Enable anti smoke##anti_smoke"), &g_Esp.enableAntiSmoke);
                ImGui::Checkbox(Localized("Color personalizado del humo##smoke_color",
                    "Custom smoke color##smoke_color"), &g_Esp.enableSmokeColor);
                if (g_Esp.enableSmokeColor) {
                    DrawColorEdit("Smoke color", &g_Esp.smokeColorR,
                        &g_Esp.smokeColorG, &g_Esp.smokeColorB);
                    ImGui::TextDisabled("Detected: %d | tinted: %d",
                        GetDetectedSmokeProjectileCount(), GetTintedSmokeProjectileCount());
                    if (g_Esp.enableAntiSmoke)
                        ImGui::TextDisabled("Anti Smoke is hiding the tinted smoke.");
                }
                ImGui::Checkbox(Localized("Activar anti flashbang##anti_flash",
                    "Enable anti flashbang##anti_flash"), &g_Esp.enableAntiFlashbang);
                if (g_Esp.enableAntiFlashbang) {
                    ImGui::SliderInt("Flash opacity (%)", &g_Esp.antiFlashOpacityPercent, 0, 100);
                    ImGui::TextDisabled("0%% = invisible | 100%% = original");
                }

                ImGui::Separator();
                ImGui::Checkbox(Localized("Tercera persona##thirdperson",
                    "Enable Thirdperson##thirdperson"), &g_Esp.enableThirdperson);
                ImGui::Checkbox(Localized("Mostrar dinero##show_money",
                    "Show Money##show_money"), &g_Esp.showMoney);
                ImGui::Checkbox(Localized("Perfil falso##fake_profile",
                    "Fake profile##fake_profile"), &g_Esp.fakeProfile);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localized("DEPURACION##debug", "DEBUG##debug"))) {
                ImGui::SeparatorText("Overlay");
                ImGui::Checkbox(Localized("Mostrar Debug (en pantalla)##show_debug",
                    "Show Debug (on-screen)##show_debug"), &g_Esp.showDebug);
                ImGui::SameLine();
                ImGui::Checkbox(Localized("Ventana Debug persistente##debug_window",
                    "Persistent Debug Window##debug_window"), &g_Esp.showDebugWindow);

                ImGui::SeparatorText("Inventory / Panorama");
                const InventoryChangerStatus& inventoryRuntimeStatus =
                    GetInventoryChangerStatus();
                const int pendingRevealCount =
                    CountPendingLocalInventoryReveals(g_InventoryChanger);
                ImGui::TextDisabled("Backend: %s",
                    inventoryRuntimeStatus.backend == InventoryRuntimeBackend::InjectedBridge
                        ? "Bridge"
                        : inventoryRuntimeStatus.backend == InventoryRuntimeBackend::ExternalOverlay
                            ? "Overlay"
                            : Localized("Inactivo", "Inactive"));
                ImGui::TextDisabled(Localized("Bridge: %s | NEW ITEM pendientes: %d",
                    "Bridge: %s | Pending NEW ITEM: %d"),
                    inventoryRuntimeStatus.bridgeActive
                        ? Localized("conectado", "connected")
                        : Localized("desconectado", "disconnected"),
                    pendingRevealCount);
                if (inventoryRuntimeStatus.bridgeActive) {
                    if (ImGui::Checkbox(Localized("UI Panorama de prueba##panorama_debug_ui",
                        "Panorama test UI##panorama_debug_ui"),
                        &g_InventoryChanger.useDebugPanoramaUi))
                        RequestInventoryChangerRefresh();
                    ImGui::TextDisabled(Localized(
                        "Desactivado: reveal nativo. Activado: indicador naranja y modal personalizado.",
                        "Disabled: native reveal. Enabled: orange badge and custom modal."));
                } else {
                    ImGui::BeginDisabled();
                    bool panoramaDebugUi = g_InventoryChanger.useDebugPanoramaUi;
                    ImGui::Checkbox(Localized("UI Panorama de prueba##panorama_debug_ui",
                        "Panorama test UI##panorama_debug_ui"), &panoramaDebugUi);
                    ImGui::EndDisabled();
                    ImGui::TextDisabled(Localized(
                        "Conecta el Bridge para habilitar el frontend Panorama de prueba.",
                        "Connect the Bridge to enable the Panorama test frontend."));
                }
                if (ImGui::CollapsingHeader(Localized(
                        "Herramientas NEW ITEM", "NEW ITEM tools"))) {
                    static std::string revealDebugFeedback;
                    const LocalInventoryItem* selectedDebugItem =
                        FindLocalInventoryItemById(g_InventoryChanger,
                            g_InventoryChanger.selectedLocalId);
                    const bool validDebugItem = selectedDebugItem != nullptr &&
                        ValidateLocalInventoryItem(*selectedDebugItem);
                    if (selectedDebugItem) {
                        ImGui::TextDisabled(Localized("Seleccionado: %s | ID local %llu",
                            "Selected: %s | Local ID %llu"),
                            selectedDebugItem->displayName,
                            static_cast<unsigned long long>(selectedDebugItem->localId));
                    } else {
                        ImGui::TextDisabled(Localized(
                            "No hay un articulo seleccionado en la coleccion.",
                            "No item is selected in the collection."));
                    }
                    if (!validDebugItem) ImGui::BeginDisabled();
                    if (ImGui::Button(Localized("Reencolar NEW ITEM seleccionado",
                            "Requeue selected NEW ITEM"))) {
                        if (QueueLocalInventoryRevealById(selectedDebugItem->localId)) {
                            revealDebugFeedback = inventoryRuntimeStatus.bridgeActive
                                ? Localized("Articulo reencolado; abre Inventario en CS2.",
                                    "Item requeued; open Inventory in CS2.")
                                : Localized("Presentacion guardada hasta que se conecte el Bridge.",
                                    "Reveal saved until the Bridge connects.");
                        } else {
                            revealDebugFeedback = Localized("No se pudo reencolar el articulo.",
                                "The item could not be requeued.");
                        }
                    }
                    if (!validDebugItem) ImGui::EndDisabled();
                    ImGui::TextDisabled(Localized(
                        "Herramienta manual de diagnostico; no interviene en el reveal automatico.",
                        "Manual diagnostic tool; it does not affect automatic reveals."));
                    if (!revealDebugFeedback.empty())
                        ImGui::TextWrapped("%s", revealDebugFeedback.c_str());
                }
                if (ImGui::CollapsingHeader(Localized("Diagnosticos IPC", "IPC diagnostics"))) {
                    const std::vector<InventoryLogEntry> logEntries = GetInventoryLogSnapshot();
                    const std::size_t firstEntry = logEntries.size() > 16
                        ? logEntries.size() - 16 : 0;
                    if (logEntries.empty())
                        ImGui::TextDisabled(Localized("Sin actividad IPC.", "No IPC activity."));
                    for (std::size_t index = firstEntry; index < logEntries.size(); ++index) {
                        const InventoryLogEntry& entry = logEntries[index];
                        ImGui::TextDisabled("[%s] %s",
                            GetInventoryLogCategoryName(entry.category), entry.message.c_str());
                    }
                }

                ImGui::SeparatorText(Localized("Offsets automaticos", "Auto Offsets"));
                const OffsetUpdateStatus& offsetStatus = GetOffsetUpdateStatus();
                const ImVec4 statusColor = offsetStatus.loaded
                    ? ImVec4(0.35f, 0.95f, 0.55f, 1.0f)
                    : ImVec4(1.0f, 0.38f, 0.38f, 1.0f);
                ImGui::TextColored(statusColor, "%s",
                    offsetStatus.loaded ? Localized("Offsets listos", "Offsets ready")
                        : Localized("Offsets sin validar", "Unvalidated offsets"));
                if (!offsetStatus.source.empty()) {
                    const std::string localizedSource =
                        LocalizeOffsetSource(offsetStatus.source);
                    ImGui::TextWrapped(Localized("Fuente: %s", "Source: %s"),
                        localizedSource.c_str());
                }
                if (!offsetStatus.build.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("| Build %s", offsetStatus.build.c_str());
                }
                const std::string localizedOffsetMessage =
                    LocalizeOffsetDiagnostic(offsetStatus.message);
                ImGui::TextWrapped("%s", localizedOffsetMessage.c_str());
                ImGui::TextDisabled(Localized("Globales: %d | Schema: %d",
                    "Globals: %d | Schema: %d"),
                    offsetStatus.globalOffsetsFound, offsetStatus.schemaOffsetsFound);
                if (ImGui::Button(Localized("Actualizar ahora", "Update now")))
                    RunOffsetAutoUpdate();
                ImGui::SameLine();
                if (ImGui::Button(Localized("Recargar cache", "Reload cache")))
                    ReloadOffsetsFromOutput();

                ImGui::SeparatorText("Glow");
                const GlowDiagnostics& glowDiagnostics = GetGlowDiagnostics();
                bool glowDiagnosticsEnabled = glowDiagnostics.enabled;
                if (ImGui::Checkbox(Localized("Diagnosticos de Glow##glow_diag",
                    "Glow diagnostics##glow_diag"), &glowDiagnosticsEnabled))
                    SetGlowDiagnosticsEnabled(glowDiagnosticsEnabled);
                if (glowDiagnosticsEnabled) {
                    const GlowDiagnostics& diagnostics = GetGlowDiagnostics();
                    ImGui::TextDisabled(Localized("Activos: %d | Muestras: %llu",
                        "Active: %d | Samples: %llu"),
                        diagnostics.activePawns,
                        static_cast<unsigned long long>(diagnostics.samples));
                    ImGui::TextDisabled(Localized("Desactivados por el juego: %llu | Alterados: %llu",
                        "Game disabled: %llu | Altered: %llu"),
                        static_cast<unsigned long long>(diagnostics.gameDisabledGlow),
                        static_cast<unsigned long long>(diagnostics.alteredProperties));
                    ImGui::TextDisabled(Localized(
                        "Fallos lectura: %llu | escritura: %llu | huecos snapshot: %llu",
                        "Read fail: %llu | Write fail: %llu | Snapshot gaps: %llu"),
                        static_cast<unsigned long long>(diagnostics.readFailures),
                        static_cast<unsigned long long>(diagnostics.writeFailures),
                        static_cast<unsigned long long>(diagnostics.snapshotGaps));
                    ImGui::TextDisabled(Localized("Ultimo pawn: 0x%llX", "Last pawn: 0x%llX"),
                        static_cast<unsigned long long>(diagnostics.lastAffectedPawn));
                    ImGui::TextDisabled(Localized("Tiempo Glow: %.3f | Inicio: %.3f",
                        "Glow time: %.3f | Start: %.3f"),
                        diagnostics.lastGlowTime, diagnostics.lastGlowStartTime);
                    ImGui::TextDisabled(Localized("Flasheado: %s | Elegible: %s",
                        "Flashing: %s | Eligible: %s"),
                        diagnostics.lastFlashing ? Localized("Si", "Yes") : "No",
                        diagnostics.lastEligible ? Localized("Si", "Yes") : "No");
                    if (ImGui::Button(Localized("Reiniciar diagnosticos de Glow",
                            "Reset Glow diagnostics")))
                        ResetGlowDiagnostics();
                }

                ImGui::SeparatorText("Models");
                const ModelDiagnostics& modelDiagnostics = GetModelDiagnostics();
                bool modelDiagnosticsEnabled = modelDiagnostics.enabled;
                if (ImGui::Checkbox(Localized("Diagnosticos de modelo/hitbox##model_diag",
                        "Model/Hitbox diagnostics##model_diag"), &modelDiagnosticsEnabled))
                    SetModelDiagnosticsEnabled(modelDiagnosticsEnabled);
                if (modelDiagnosticsEnabled) {
                    const ModelDiagnostics& diagnostics = GetModelDiagnostics();
                    ImGui::TextDisabled(Localized(
                        "Muestras: %llu | Fallos lectura: %llu | Conjunto hitbox: %d",
                        "Samples: %llu | Read fail: %llu | Hitbox set: %d"),
                        static_cast<unsigned long long>(diagnostics.samples),
                        static_cast<unsigned long long>(diagnostics.readFailures),
                        diagnostics.hitboxSet);
                    ImGui::TextDisabled("Model handle: 0x%llX | Data: 0x%llX",
                        static_cast<unsigned long long>(diagnostics.modelHandle),
                        static_cast<unsigned long long>(diagnostics.modelData));
                    ImGui::TextDisabled(Localized(
                        "Candidatos: %d | Cuaternion del hueso: %s (%.3f)",
                        "Candidates: %d | Bone quaternion: %s (%.3f)"),
                        diagnostics.validCandidates,
                        diagnostics.boneRotationValid ? Localized("Valido", "Valid")
                            : Localized("Invalido", "Invalid"),
                        diagnostics.boneRotationNorm);
                    ImGui::TextDisabled(Localized("Huesos del modelo: %d | Partes externas: %d",
                        "Model bones: %d | External parts: %d"),
                        diagnostics.modelBoneCount, diagnostics.externalPartCount);
                    ImGui::TextDisabled(Localized(
                        "Conjuntos hitbox: %d | Hitboxes: %d | Offset: 0x%X",
                        "Hitbox sets: %d | Hitboxes: %d | Offset: 0x%X"),
                        diagnostics.modelHitboxSetCount, diagnostics.modelHitboxCount,
                        diagnostics.hitboxListOffset);
                    ImGui::TextWrapped(Localized("Primer hueso hitbox: %s",
                        "First hitbox bone: %s"),
                        diagnostics.firstHitboxBone.empty() ? "--" : diagnostics.firstHitboxBone.c_str());
                    ImGui::TextWrapped("Model: %s", diagnostics.modelName.empty()
                        ? "--" : diagnostics.modelName.c_str());
                    ImGui::TextWrapped("Resource: %s", diagnostics.resourceName.empty()
                        ? "--" : diagnostics.resourceName.c_str());
                    if (ImGui::Button(Localized("Reiniciar diagnosticos de modelo",
                            "Reset Model diagnostics")))
                        ResetModelDiagnostics();
                }

                ImGui::SeparatorText(Localized("Punteria", "Aim"));
                ImGui::Checkbox(Localized("Activar calibrador de Aimlock##aim_cal",
                    "Enable Aimlock Calibrator##aim_cal"), &g_Esp.enableAimlockCalibrator);
                if (g_Esp.enableAimlockCalibrator) {
                    ImGui::Text("%s", Localized(
                        "Calibracion Aimlock (unidades del mundo sobre los pies)",
                        "Aimlock calibration (world units above feet)"));
                    ImGui::SliderFloat(Localized("Offset cabeza##cal", "Head Offset##cal"),
                        &g_Aim.headOffset, 40.0f, 100.0f, "%.1f");
                    ImGui::SliderFloat(Localized("Offset torso##cal", "Torso Offset##cal"),
                        &g_Aim.torsoOffset, 20.0f, 80.0f, "%.1f");
                    ImGui::SliderFloat(Localized("Offset piernas##cal", "Leg Offset##cal"),
                        &g_Aim.legOffset, 0.0f, 40.0f, "%.1f");
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(Localized("CONFIGURACIONES##configs",
                    "CONFIGS##configs"))) {
                static bool initialized = false;
                static std::vector<std::string> presets;
                static std::string selected;
                static std::string preload;
                static char newPresetName[64]{};
                static char status[128]{};

                auto displayName = [](const std::string& fileName) {
                    if (fileName == "slot1.cfg") return std::string("Slot 1");
                    if (fileName == "slot2.cfg") return std::string("Slot 2");
                    if (fileName == "slot3.cfg") return std::string("Slot 3");
                    std::string result = fileName;
                    if (result.size() > 4 && result.substr(result.size() - 4) == ".cfg")
                        result.resize(result.size() - 4);
                    for (char& ch : result) if (ch == '_') ch = ' ';
                    return result;
                };
                auto refreshPresets = [&]() {
                    presets = ListConfigPresets();
                    if (selected.empty() && !presets.empty()) selected = presets.front();
                    bool selectionExists = false;
                    for (const std::string& preset : presets)
                        selectionExists = selectionExists || preset == selected;
                    if (!selectionExists) selected = presets.empty() ? std::string{} : presets.front();
                };
                if (!initialized) {
                    refreshPresets();
                    preload = LoadPreloadConfig();
                    initialized = true;
                }

                ImGui::TextUnformatted("Preload");
                const std::string preloadPreview = preload.empty()
                    ? std::string("Ninguno")
                    : displayName(preload);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##ConfigPreload", preloadPreview.c_str())) {
                    if (ImGui::Selectable("Ninguno", preload.empty())) {
                        preload.clear();
                        SavePreloadConfig({});
                        snprintf(status, sizeof(status), "Preload desactivado");
                    }
                    for (const std::string& preset : presets) {
                        const std::string label = displayName(preset);
                        if (ImGui::Selectable(label.c_str(), preload == preset)) {
                            preload = preset;
                            SavePreloadConfig(preload);
                            snprintf(status, sizeof(status), "Preload: %s", label.c_str());
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::Spacing();
                if (ImGui::BeginTable("##ConfigManager", 2,
                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Presets", ImGuiTableColumnFlags_WidthStretch, 0.58f);
                    ImGui::TableSetupColumn("Acciones", ImGuiTableColumnFlags_WidthStretch, 0.42f);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted("Configuraciones");
                    ImGui::BeginChild("##ConfigPresetList", ImVec2(0.0f, 180.0f), true);
                    for (const std::string& preset : presets) {
                        const std::string label = displayName(preset);
                        if (ImGui::Selectable(label.c_str(), selected == preset))
                            selected = preset;
                    }
                    ImGui::EndChild();

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted("Acciones");
                    const float actionWidth = ImGui::GetContentRegionAvail().x;
                    ImGui::BeginDisabled(selected.empty());
                    if (ImGui::Button("Guardar", ImVec2(actionWidth, 0.0f))) {
                        if (SaveConfigPreset(selected))
                            snprintf(status, sizeof(status), "Guardado: %s", displayName(selected).c_str());
                    }
                    if (ImGui::Button("Cargar", ImVec2(actionWidth, 0.0f))) {
                        if (LoadConfigPreset(selected))
                            snprintf(status, sizeof(status), "Cargado: %s", displayName(selected).c_str());
                    }
                    ImGui::EndDisabled();
                    if (ImGui::Button("Restablecer", ImVec2(actionWidth, 0.0f))) {
                        ResetConfigDefaults();
                        snprintf(status, sizeof(status), "Valores predeterminados restaurados");
                    }
                    const bool canDelete = !selected.empty() && !IsDefaultConfigSlot(selected);
                    ImGui::BeginDisabled(!canDelete);
                    if (ImGui::Button("Eliminar preset", ImVec2(actionWidth, 0.0f))) {
                        if (DeleteConfigPreset(selected)) {
                            if (preload == selected) preload.clear();
                            refreshPresets();
                            snprintf(status, sizeof(status), "Preset eliminado");
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Crear preset");
                ImGui::SetNextItemWidth(-120.0f);
                const bool submitted = ImGui::InputTextWithHint(
                    "##NewConfigName", "Nombre del preset", newPresetName,
                    sizeof(newPresetName), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if (ImGui::Button("Crear", ImVec2(-1.0f, 0.0f)) || submitted) {
                    std::string created;
                    if (SaveConfigPreset(newPresetName, &created)) {
                        selected = created;
                        newPresetName[0] = '\0';
                        refreshPresets();
                        snprintf(status, sizeof(status), "Creado: %s", displayName(created).c_str());
                    } else {
                        snprintf(status, sizeof(status), "Nombre de preset no valido");
                    }
                }
                if (status[0] != '\0') ImGui::TextDisabled("%s", status);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

    }
    ImGui::End();
    if (GetUiFont()) ImGui::PopFont();
}
