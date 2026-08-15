#include "SpectatorList.h"

#include "Config.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"
#include "imgui.h"

#include <string>
#include <utility>
#include <vector>

namespace {
    struct PlayerRecord {
        uintptr_t controller = 0;
        uintptr_t pawn = 0;
        uintptr_t observerPawn = 0;
        bool alive = false;
        std::string name;
    };

    std::vector<std::string> g_spectators;
    ULONGLONG g_lastUpdateMs = 0;

    uintptr_t ResolveHandle(uintptr_t entityList, uint32_t handle) {
        if (!handle || handle == 0xFFFFFFFF) return 0;
        return GetEntityByIndexAuto(entityList, handle & 0x7FFF);
    }

    std::string GetControllerName(uintptr_t controller, int index) {
        char name[128]{};
        ReadPlayerName(controller, name, sizeof(name));
        if (name[0] != '\0') return name;
        return "Player " + std::to_string(index);
    }

    uintptr_t ReadObserverTarget(const PlayerRecord& player, uintptr_t entityList)
    {
        const uintptr_t candidates[] = { player.observerPawn, player.pawn };
        for (uintptr_t candidate : candidates) {
            if (!IsValidPtr(candidate)) continue;
            const uintptr_t services = mem.Read<uintptr_t>(candidate + Offsets::m_pObserverServices);
            if (!IsValidPtr(services)) continue;
            const uint8_t mode = mem.Read<uint8_t>(services + Offsets::m_iObserverMode);
            // Only first-person and chase modes represent a player actively
            // spectating another pawn. Fixed/roaming modes must not enter the list.
            if (mode != 2 && mode != 3) continue;
            const uint32_t targetHandle = mem.Read<uint32_t>(services + Offsets::m_hObserverTarget);
            const uintptr_t target = ResolveHandle(entityList, targetHandle);
            if (IsValidPtr(target) && target != candidate) return target;
        }
        return 0;
    }

    void UpdateRows() {
        const ULONGLONG nowMs = GetTickCount64();
        if (g_lastUpdateMs != 0 && nowMs - g_lastUpdateMs < 100) return;
        g_lastUpdateMs = nowMs;
        g_spectators.clear();

        const uintptr_t entityList = GetEntityListBase();
        if (!IsValidPtr(entityList)) return;
        const FrameSnapshot& frame = GetCurrentFrameSnapshot();
        if (!IsValidPtr(frame.localPawn)) return;
        const uintptr_t nativeLocalController = mem.Read<uintptr_t>(
            mem.clientModule + Offsets::dwLocalPlayerController);

        std::vector<PlayerRecord> players;
        players.reserve(64);
        for (int index = 1; index <= 64; ++index) {
            const uintptr_t controller = GetEntityByIndexAuto(entityList, index);
            if (!IsValidPtr(controller)) continue;

            PlayerRecord player{};
            player.controller = controller;
            player.pawn = ResolveHandle(entityList,
                mem.Read<uint32_t>(controller + Offsets::m_hPlayerPawn));
            player.observerPawn = ResolveHandle(entityList,
                mem.Read<uint32_t>(controller + Offsets::m_hObserverPawn));
            if (!IsValidPtr(player.pawn) && !IsValidPtr(player.observerPawn)) continue;
            player.alive = mem.Read<bool>(controller + Offsets::m_bPawnIsAlive);
            player.name = GetControllerName(controller, index);
            players.push_back(std::move(player));
        }

        for (const PlayerRecord& player : players) {
            if (player.controller == frame.localController ||
                player.controller == nativeLocalController || player.alive)
                continue;
            if (ReadObserverTarget(player, entityList) == frame.localPawn)
                g_spectators.push_back(player.name);
        }
    }
}

void RenderSpectatorListWindow() {
    if (!g_Esp.showSpectatorList) return;
    UpdateRows();

    ImGui::SetNextWindowSize(ImVec2(260.0f, 130.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Spectator List", &g_Esp.showSpectatorList,
        ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Watching you: %d", static_cast<int>(g_spectators.size()));
    ImGui::Separator();
    if (g_spectators.empty()) {
        ImGui::TextDisabled("No spectators");
    } else {
        for (const std::string& spectator : g_spectators)
            ImGui::BulletText("%s", spectator.c_str());
    }
    ImGui::End();
}

void ResetSpectatorList() {
    g_spectators.clear();
    g_lastUpdateMs = 0;
}
