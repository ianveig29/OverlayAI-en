// ============================================================
// InventoryHost.cpp
// Inventory host process. Runs as a separate process to handle items without interfering with the overlay.
// ============================================================

#include "InventoryIpcServer.h"
#include "InventoryProtocol.h"
#include "PanoramaBridgeContract.h"
#include "third_party/nlohmann/json.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>

using Json = nlohmann::json;

namespace {
    constexpr DWORD kConnectTimeoutMs = 3000;
    constexpr DWORD kDefaultWatchIntervalMs = 1250;
    constexpr int kDefaultWatchIterations = 10;

    bool WriteExact(HANDLE pipe, const void* source, DWORD size) {
        const auto* bytes = static_cast<const unsigned char*>(source);
        DWORD completed = 0;
        while (completed < size) {
            DWORD written = 0;
            if (!WriteFile(pipe, bytes + completed, size - completed, &written, nullptr) ||
                written == 0)
                return false;
            completed += written;
        }
        return true;
    }

    bool ReadExact(HANDLE pipe, void* destination, DWORD size) {
        auto* bytes = static_cast<unsigned char*>(destination);
        DWORD completed = 0;
        while (completed < size) {
            DWORD read = 0;
            if (!ReadFile(pipe, bytes + completed, size - completed, &read, nullptr) ||
                read == 0)
                return false;
            completed += read;
        }
        return true;
    }

    void SleepQuietly(DWORD milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    const char* ItemTypeName(int type) {
        switch (type) {
        case 0: return "Music Kit";
        case 1: return "Arma";
        case 2: return "Cuchillo";
        case 3: return "Guantes";
        case 4: return "Agente";
        default: return "Desconocido";
        }
    }

    bool TryParsePositive(const char* text, uint64_t& value) {
        if (!text || !*text) return false;
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(text, &end, 10);
        if (!end || *end != '\0' || parsed == 0) return false;
        value = static_cast<uint64_t>(parsed);
        return true;
    }

    bool TryParseTeam(const char* text, int& team) {
        if (!text) return false;
        if (_stricmp(text, "t") == 0 || _stricmp(text, "terrorist") == 0) {
            team = LocalInventoryTeamTerrorist;
            return true;
        }
        if (_stricmp(text, "ct") == 0 ||
            _stricmp(text, "counter-terrorist") == 0) {
            team = LocalInventoryTeamCounterTerrorist;
            return true;
        }
        if (_stricmp(text, "both") == 0 || _stricmp(text, "ambos") == 0) {
            team = LocalInventoryTeamBoth;
            return true;
        }
        return false;
    }

    std::string BuildSessionId() {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "inventory-host-%lu",
            static_cast<unsigned long>(GetCurrentProcessId()));
        return buffer;
    }

    Json MakeEnvelope(const char* type, uint64_t requestId, Json payload) {
        return Json{
            {"protocol_version", OverlayAIPanoramaContract::kInventoryProtocolVersion},
            {"message_type", type ? type : "inventory.request_refresh"},
            {"request_id", requestId},
            {"payload", std::move(payload)}
        };
    }

    class PipeClient {
    public:
        ~PipeClient() { Close(); }

        bool Connect() {
            Close();
            if (!WaitNamedPipeW(kInventoryIpcPipeName, kConnectTimeoutMs))
                return false;

            pipe_ = CreateFileW(
                kInventoryIpcPipeName,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            return pipe_ != INVALID_HANDLE_VALUE;
        }

        void Close() {
            if (pipe_ != INVALID_HANDLE_VALUE) {
                CloseHandle(pipe_);
                pipe_ = INVALID_HANDLE_VALUE;
            }
        }

        bool SendJson(const Json& request, Json& response) {
            if (pipe_ == INVALID_HANDLE_VALUE) return false;
            const std::string payload = request.dump();
            const uint32_t requestBytes = static_cast<uint32_t>(payload.size());
            if (requestBytes == 0 || requestBytes > kInventoryProtocolMaxFrameBytes)
                return false;

            if (!WriteExact(pipe_, &requestBytes, sizeof(requestBytes)) ||
                !WriteExact(pipe_, payload.data(), requestBytes))
                return false;

            uint32_t responseBytes = 0;
            if (!ReadExact(pipe_, &responseBytes, sizeof(responseBytes)) ||
                responseBytes == 0 || responseBytes > kInventoryProtocolMaxFrameBytes)
                return false;

            std::string responsePayload(responseBytes, '\0');
            if (!ReadExact(pipe_, responsePayload.data(), responseBytes))
                return false;

            response = Json::parse(responsePayload, nullptr, false);
            return !response.is_discarded();
        }

    private:
        HANDLE pipe_ = INVALID_HANDLE_VALUE;
    };

    void PrintUsage() {
        std::printf(
            "OverlayAI.InventoryHost\n"
            "Uso:\n"
            "  InventoryHost.exe list [--json]\n"
            "  InventoryHost.exe watch [iteraciones] [intervalo_ms] [--json]\n"
            "  InventoryHost.exe equip <local_id> [t|ct|both]\n"
            "  InventoryHost.exe unequip [local_id] [t|ct|both]\n"
            "  InventoryHost.exe duplicate <local_id>\n"
            "  InventoryHost.exe remove <local_id>\n"
            "  InventoryHost.exe inspect <local_id>\n"
            "  InventoryHost.exe close-reveal <local_id>\n");
    }

    bool IsSnapshot(const Json& message) {
        return message.is_object() &&
            message.value("message_type", "") == "inventory.snapshot" &&
            message.contains("payload") && message.at("payload").is_object();
    }

    bool IsProtocolError(const Json& message) {
        return message.is_object() &&
            message.value("message_type", "") == "protocol.error";
    }

    void PrintMessageSummary(const Json& message) {
        const std::string type = message.value("message_type", "unknown");
        const uint64_t requestId = message.value("request_id", 0ULL);
        std::printf("[%s] request_id=%llu\n", type.c_str(),
            static_cast<unsigned long long>(requestId));
        if (!message.contains("payload") || !message.at("payload").is_object())
            return;
        const Json& payload = message.at("payload");
        if (IsProtocolError(message)) {
            std::printf("  code=%s\n", payload.value("code", "").c_str());
            std::printf("  message=%s\n", payload.value("message", "").c_str());
            return;
        }
        if (payload.contains("local_id"))
            std::printf("  local_id=%llu\n",
                static_cast<unsigned long long>(payload.value("local_id", 0ULL)));
        if (payload.contains("success"))
            std::printf("  success=%s\n", payload.value("success", false) ? "true" : "false");
        if (payload.contains("detail"))
            std::printf("  detail=%s\n", payload.value("detail", "").c_str());
    }

    void PrintSnapshot(const Json& snapshot, bool rawJson) {
        if (rawJson) {
            std::printf("%s\n", snapshot.dump(2).c_str());
            return;
        }

        const Json& payload = snapshot.at("payload");
        const Json& items = payload.value("items", Json::array());
        const Json& loadout = payload.value("loadout", Json::object());
        const uint64_t equippedMusicKit = loadout.value("music_kit", 0ULL);
        const uint64_t terroristKnife = loadout.value("terrorist_knife", 0ULL);
        const uint64_t counterTerroristKnife = loadout.value("counter_terrorist_knife", 0ULL);
        const uint64_t terroristAgent = loadout.value("terrorist_agent", 0ULL);
        const uint64_t counterTerroristAgent = loadout.value("counter_terrorist_agent", 0ULL);

        std::printf(
            "inventory_version=%u | items=%zu | selected=%llu | pending=%llu | enabled=%s\n",
            payload.value("inventory_version", 0U),
            items.size(),
            static_cast<unsigned long long>(payload.value("selected_local_id", 0ULL)),
            static_cast<unsigned long long>(payload.value("pending_reveal_item_id", 0ULL)),
            payload.value("enabled", false) ? "true" : "false");
        std::printf(
            "loadout: music=%llu | knife T=%llu CT=%llu | agent T=%llu CT=%llu\n",
            static_cast<unsigned long long>(equippedMusicKit),
            static_cast<unsigned long long>(terroristKnife),
            static_cast<unsigned long long>(counterTerroristKnife),
            static_cast<unsigned long long>(terroristAgent),
            static_cast<unsigned long long>(counterTerroristAgent));

        for (const Json& item : items) {
            const uint64_t localId = item.value("local_id", 0ULL);
            const bool equipped = localId != 0 &&
                (localId == equippedMusicKit || localId == terroristKnife ||
                    localId == counterTerroristKnife || localId == terroristAgent ||
                    localId == counterTerroristAgent);
            std::string displayName = item.value("display_name", "");
            if (displayName.empty())
                displayName = ItemTypeName(item.value("type", 0));
            std::printf(
                "  [%llu]%s %s | def=%d paint=%d | validity=%s | rarity=%s\n",
                static_cast<unsigned long long>(localId),
                equipped ? "*" : " ",
                displayName.c_str(),
                item.value("definition_index", 0),
                item.value("paint_kit", 0),
                item.value("validity", "").c_str(),
                item.value("rarity", "").c_str());

            const std::string customName = item.value("custom_name", "");
            const std::string group = item.value("group", "");
            if (!customName.empty() || !group.empty()) {
                std::printf("       group=%s | custom=%s\n",
                    group.empty() ? "--" : group.c_str(),
                    customName.empty() ? "--" : customName.c_str());
            }
        }
    }

    bool PerformHello(PipeClient& client, uint64_t& requestId, Json& snapshot) {
        const Json hello = MakeEnvelope("client.hello", ++requestId, {
            {"client_session_id", BuildSessionId()}
        });
        if (!client.SendJson(hello, snapshot))
            return false;
        return IsSnapshot(snapshot);
    }

    bool PerformRefresh(PipeClient& client, uint64_t& requestId, Json& snapshot) {
        return client.SendJson(
            MakeEnvelope("inventory.request_refresh", ++requestId, Json::object()),
            snapshot) && IsSnapshot(snapshot);
    }

    bool PerformAction(
        PipeClient& client, const char* messageType, uint64_t& requestId,
        uint64_t localId, bool includeLocalId, int team, bool includeTeam,
        Json& response) {
        Json payload = Json::object();
        if (includeLocalId)
            payload["local_id"] = localId;
        if (includeTeam)
            payload["team"] = team;
        return client.SendJson(MakeEnvelope(messageType, ++requestId, std::move(payload)), response);
    }
}

int main(int argc, char** argv) {
    if (argc <= 1) {
        PrintUsage();
        return 0;
    }

    const std::string command = argv[1] ? argv[1] : "";
    bool rawJson = false;
    for (int index = 2; index < argc; ++index) {
        if (argv[index] && std::strcmp(argv[index], "--json") == 0)
            rawJson = true;
    }

    PipeClient client;
    if (!client.Connect()) {
        std::fprintf(stderr,
            "No se pudo conectar con %ls. Asegurate de que OverlayAI este iniciado.\n",
            kInventoryIpcPipeName);
        return 1;
    }

    uint64_t requestId = 1000;
    Json snapshot;
    if (!PerformHello(client, requestId, snapshot)) {
        std::fprintf(stderr, "Handshake fallido o respuesta invalida.\n");
        return 1;
    }

    if (command == "list") {
        if (!PerformRefresh(client, requestId, snapshot)) {
            std::fprintf(stderr, "No se pudo solicitar snapshot.\n");
            return 1;
        }
        PrintSnapshot(snapshot, rawJson);
        return 0;
    }

    if (command == "watch") {
        int iterations = kDefaultWatchIterations;
        DWORD intervalMs = kDefaultWatchIntervalMs;
        if (argc >= 3 && argv[2] && std::strcmp(argv[2], "--json") != 0)
            iterations = (std::max)(1, std::atoi(argv[2]));
        if (argc >= 4 && argv[3] && std::strcmp(argv[3], "--json") != 0)
            intervalMs = static_cast<DWORD>((std::max)(100, std::atoi(argv[3])));

        for (int step = 0; step < iterations; ++step) {
            if (!PerformRefresh(client, requestId, snapshot)) {
                std::fprintf(stderr, "watch: refresh fallido en iteracion %d.\n", step + 1);
                return 1;
            }
            std::printf("---- snapshot %d/%d ----\n", step + 1, iterations);
            PrintSnapshot(snapshot, rawJson);
            if (step + 1 < iterations)
                SleepQuietly(intervalMs);
        }
        return 0;
    }

    const bool requiresLocalId =
        command == "equip" || command == "duplicate" || command == "remove" ||
        command == "inspect" || command == "close-reveal";
    const bool unequipHasLocalId = command == "unequip" && argc >= 3;
    const bool includesLocalId = requiresLocalId || unequipHasLocalId;

    uint64_t localId = 0;
    if (includesLocalId) {
        if (argc < 3 || !TryParsePositive(argv[2], localId)) {
            std::fprintf(stderr, "El comando %s requiere un local_id positivo.\n", command.c_str());
            return 1;
        }
    }

    int team = LocalInventoryTeamNone;
    bool includeTeam = false;
    const int teamArgument = command == "equip" || unequipHasLocalId ? 3 : -1;
    if (teamArgument > 0 && argc > teamArgument) {
        if (!TryParseTeam(argv[teamArgument], team)) {
            std::fprintf(stderr, "Equipo invalido: usa t, ct o both.\n");
            return 1;
        }
        includeTeam = true;
    }

    const char* messageType = nullptr;
    if (command == "equip") messageType = "inventory.equip";
    else if (command == "unequip") messageType = "inventory.unequip";
    else if (command == "duplicate") messageType = "inventory.duplicate";
    else if (command == "remove") messageType = "inventory.remove";
    else if (command == "inspect") messageType = "inventory.inspect";
    else if (command == "close-reveal") messageType = "inventory.close_reveal";
    else {
        PrintUsage();
        return 1;
    }

    Json response;
    if (!PerformAction(client, messageType, requestId, localId, includesLocalId,
        team, includeTeam, response)) {
        std::fprintf(stderr, "No se pudo enviar la accion %s.\n", command.c_str());
        return 1;
    }

    PrintMessageSummary(response);
    if (IsProtocolError(response))
        return 1;

    if (command == "equip" || command == "unequip" ||
        command == "duplicate" || command == "remove" ||
        command == "close-reveal") {
        if (!PerformRefresh(client, requestId, snapshot)) {
            std::fprintf(stderr, "La accion respondio, pero el refresh posterior fallo.\n");
            return 1;
        }
        PrintSnapshot(snapshot, rawJson);
    }

    return 0;
}
