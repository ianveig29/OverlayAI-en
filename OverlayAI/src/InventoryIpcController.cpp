// ============================================================
// InventoryIpcController.cpp
// IPC (inter-process communication) controller for sending inventory commands to the game.
// ============================================================

#include "InventoryIpcController.h"

#include "Config.h"
#include "InventoryCatalog.h"
#include "InventoryChanger.h"
#include "InventoryLog.h"
#include "Localization.h"
#include "InventoryProtocol.h"
#include "InventoryStore.h"
#include "Offsets.h"

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>

namespace {
    constexpr wchar_t kInventoryBridgeStopEvent[] =
        L"Local\\OverlayAI.InventoryBridge.Stop";
    constexpr std::size_t kMaxRememberedSessions = 8;
    constexpr std::size_t kMaxRememberedRequests = 256;
    constexpr int kMaxCommandsPerFrame = 32;
    constexpr ULONGLONG kInventoryBridgeLeaseMs = 4000;
    constexpr char kInventoryBridgeSession[] = "inventory-bridge";

    void SignalInventoryBridgeStop() {
        HANDLE stopEvent = OpenEventW(
            EVENT_MODIFY_STATE, FALSE, kInventoryBridgeStopEvent);
        if (!stopEvent) return;
        (void)SetEvent(stopEvent);
        CloseHandle(stopEvent);
    }

    std::string BuildRuntimeInventorySnapshot(uint64_t requestId) {
        InventoryRuntimeOffsets runtimeOffsets;
        runtimeOffsets.entityList = Offsets::dwEntityList;
        runtimeOffsets.localPlayerController = Offsets::dwLocalPlayerController;
        runtimeOffsets.localPlayerPawn = Offsets::dwLocalPlayerPawn;
        runtimeOffsets.inventoryServices = Offsets::m_pInventoryServices;
        runtimeOffsets.serviceMusicId = Offsets::m_unMusicID;
        runtimeOffsets.controllerMusicKitId = Offsets::m_iMusicKitID;
        runtimeOffsets.controllerMusicKitMvps = Offsets::m_iMusicKitMVPs;
        runtimeOffsets.playerPawnHandle = Offsets::m_hPlayerPawn;
        runtimeOffsets.controllingBot = Offsets::m_bControllingBot;
        runtimeOffsets.hasFemaleVoice = Offsets::m_bHasFemaleVoice;
        runtimeOffsets.teamNumber = Offsets::m_iTeamNum;
        runtimeOffsets.lifeState = Offsets::m_lifeState;
        runtimeOffsets.lastSpawnTimeIndex = Offsets::m_flLastSpawnTimeIndex;
        runtimeOffsets.gameSceneNode = Offsets::m_pGameSceneNode;
        runtimeOffsets.modelState = Offsets::m_modelState;
        runtimeOffsets.modelName = Offsets::m_ModelName;
        runtimeOffsets.ownerEntity = Offsets::m_hOwnerEntity;
        runtimeOffsets.hudModelArms = Offsets::m_hHudModelArms;
        runtimeOffsets.myWearables = Offsets::m_hMyWearables;
        runtimeOffsets.sceneNodeOwner = Offsets::m_pOwner;
        runtimeOffsets.sceneNodeChild = Offsets::m_pChild;
        runtimeOffsets.sceneNodeNextSibling = Offsets::m_pNextSibling;
        runtimeOffsets.weaponServices = Offsets::m_pWeaponServices;
        runtimeOffsets.activeWeapon = Offsets::m_hActiveWeapon;
        runtimeOffsets.subclassId = Offsets::m_nSubclassID;
        runtimeOffsets.viewmodelAttachment = Offsets::m_hViewmodelAttachment;
        runtimeOffsets.attributeManager = Offsets::m_AttributeManager;
        runtimeOffsets.item = Offsets::m_Item;
        runtimeOffsets.itemDefinitionIndex = Offsets::m_iItemDefinitionIndex;
        runtimeOffsets.entityQuality = Offsets::m_iEntityQuality;
        runtimeOffsets.fallbackPaintKit = Offsets::m_nFallbackPaintKit;
        runtimeOffsets.fallbackSeed = Offsets::m_nFallbackSeed;
        runtimeOffsets.fallbackWear = Offsets::m_flFallbackWear;
        runtimeOffsets.fallbackStatTrak = Offsets::m_nFallbackStatTrak;
        runtimeOffsets.itemId = Offsets::m_iItemID;
        runtimeOffsets.itemIdHigh = Offsets::m_iItemIDHigh;
        runtimeOffsets.itemIdLow = Offsets::m_iItemIDLow;
        runtimeOffsets.accountId = Offsets::m_iAccountID;
        runtimeOffsets.initialized = Offsets::m_bInitialized;
        runtimeOffsets.needToReApplyGloves = Offsets::m_bNeedToReApplyGloves;
        runtimeOffsets.econGloves = Offsets::m_EconGloves;
        runtimeOffsets.econGlovesChanged = Offsets::m_nEconGlovesChanged;
        return BuildInventorySnapshotMessage(
            g_InventoryChanger, requestId, &runtimeOffsets,
            GetMusicKitApplyRevision());
    }

    int ResolveLoadoutTeam(const LocalInventoryItem& item, int requestedTeam) {
        if (requestedTeam != LocalInventoryTeamNone) return requestedTeam;
        if (item.type == LocalInventoryMusicKit) return LocalInventoryTeamBoth;
        if (item.type == LocalInventoryKnife) return LocalInventoryTeamBoth;
        const InventoryCatalogItem* catalog = FindInventoryCatalogItem(
            item.type, item.definitionIndex, item.paintIndex);
        return catalog ? GetInventoryCatalogItemTeam(*catalog)
                       : LocalInventoryTeamNone;
    }
}

struct InventoryIpcController::Impl {
    struct SessionState {
        std::deque<uint64_t> processedRequestIds;
    };

    InventoryIpcServer server;
    std::unordered_map<uint64_t, std::string> connectionSessions;
    std::unordered_map<std::string, SessionState> sessions;
    std::deque<std::string> sessionOrder;
    ULONGLONG lastBridgeActivityMs = 0;
    bool bridgeLeaseActive = false;

    void SetBridgeLease(bool active) {
        if (bridgeLeaseActive == active) return;
        bridgeLeaseActive = active;
        SetInventoryBridgeActive(active);
        WriteInventoryLog(InventoryLogCategory::Transport,
            InventoryLogLevel::Info,
            active
                ? Localized("Backend Bridge activo; aplicador externo suspendido.",
                    "Bridge backend active; external applicator suspended.")
                : Localized("Backend Bridge ausente; aplicador externo disponible.",
                    "Bridge backend absent; external applicator available."));
    }

    void NoteBridgeActivity() {
        lastBridgeActivityMs = GetTickCount64();
        SetBridgeLease(true);
    }

    void RefreshBridgeLease() {
        if (!bridgeLeaseActive) return;
        const ULONGLONG now = GetTickCount64();
        if (now - lastBridgeActivityMs > kInventoryBridgeLeaseMs)
            SetBridgeLease(false);
    }

    void PruneConnectionSessions() {
        const InventoryIpcServerStatus status = server.GetStatus();
        if (!status.clientConnected) {
            connectionSessions.clear();
            return;
        }
        for (auto iterator = connectionSessions.begin();
            iterator != connectionSessions.end();) {
            if (iterator->first != status.connectionId)
                iterator = connectionSessions.erase(iterator);
            else
                ++iterator;
        }
    }

    void SendError(
        uint64_t connectionId, uint64_t requestId,
        const char* code, const char* message) {
        InventoryProtocolError error;
        error.requestId = requestId;
        error.code = code ? code : "unknown_error";
        error.message = message ? message : "";
        (void)server.Send(connectionId, BuildInventoryProtocolErrorMessage(error));
    }

    SessionState& TouchSession(const std::string& sessionId) {
        const auto found = sessions.find(sessionId);
        if (found != sessions.end()) {
            sessionOrder.erase(std::remove(
                sessionOrder.begin(), sessionOrder.end(), sessionId), sessionOrder.end());
            sessionOrder.push_back(sessionId);
            return found->second;
        }

        while (sessions.size() >= kMaxRememberedSessions && !sessionOrder.empty()) {
            sessions.erase(sessionOrder.front());
            sessionOrder.pop_front();
        }
        sessionOrder.push_back(sessionId);
        return sessions.emplace(sessionId, SessionState{}).first->second;
    }

    bool RememberRequest(SessionState& session, uint64_t requestId) {
        if (std::find(session.processedRequestIds.begin(),
            session.processedRequestIds.end(), requestId) !=
            session.processedRequestIds.end())
            return false;
        session.processedRequestIds.push_back(requestId);
        if (session.processedRequestIds.size() > kMaxRememberedRequests)
            session.processedRequestIds.pop_front();
        return true;
    }

    void ProcessFrame(InventoryIpcInboundFrame frame) {
        InventoryProtocolCommand command;
        InventoryProtocolError error;
        if (!ParseInventoryProtocolCommand(frame.payload, command, error)) {
            WriteInventoryLog(InventoryLogCategory::Protocol, InventoryLogLevel::Warning,
                Localized("Mensaje rechazado: %s.", "Message rejected: %s."),
                error.code.c_str());
            (void)server.Send(frame.connectionId,
                BuildInventoryProtocolErrorMessage(error));
            return;
        }

        if (command.type == InventoryProtocolCommandType::Hello) {
            // Panorama, the injected bridge and diagnostics may coexist. A new
            // handshake only replaces the session bound to this connection.
            connectionSessions.erase(frame.connectionId);
            connectionSessions.emplace(frame.connectionId, command.clientSessionId);
            const bool firstHandshake =
                sessions.find(command.clientSessionId) == sessions.end();
            (void)TouchSession(command.clientSessionId);
            if (command.clientSessionId == kInventoryBridgeSession)
                NoteBridgeActivity();
            if (firstHandshake)
                WriteInventoryLog(InventoryLogCategory::Protocol,
                    InventoryLogLevel::Info,
                    Localized("Handshake aceptado para sesion %s.",
                        "Handshake accepted for session %s."),
                    command.clientSessionId.c_str());
            (void)server.Send(frame.connectionId,
                BuildRuntimeInventorySnapshot(command.requestId));
            return;
        }

        const auto connection = connectionSessions.find(frame.connectionId);
        if (connection == connectionSessions.end()) {
            SendError(frame.connectionId, command.requestId, "handshake_required",
                "Debe enviarse client.hello antes de operar con el inventario.");
            return;
        }

        SessionState& session = TouchSession(connection->second);
        if (!RememberRequest(session, command.requestId)) {
            WriteInventoryLog(InventoryLogCategory::Protocol, InventoryLogLevel::Info,
                Localized(
                    "Request ID %llu repetido; se devuelve snapshot sin repetir la accion.",
                    "Repeated request ID %llu; returning a snapshot without repeating the action."),
                static_cast<unsigned long long>(command.requestId));
            (void)server.Send(frame.connectionId,
                BuildRuntimeInventorySnapshot(command.requestId));
            return;
        }

        switch (command.type) {
        case InventoryProtocolCommandType::RequestRefresh:
            (void)server.Send(frame.connectionId,
                BuildRuntimeInventorySnapshot(command.requestId));
            break;

        case InventoryProtocolCommandType::Equip: {
            const LocalInventoryItem* item = FindLocalInventoryItemById(
                g_InventoryChanger, command.localId);
            const int team = item
                ? ResolveLoadoutTeam(*item, command.team)
                : LocalInventoryTeamNone;
            const bool equipped = item && EquipLocalInventoryItemById(
                command.localId, team);
            WriteInventoryLog(InventoryLogCategory::Action,
                equipped ? InventoryLogLevel::Info : InventoryLogLevel::Warning,
                "Equip Local ID %llu: %s.",
                static_cast<unsigned long long>(command.localId),
                equipped ? Localized("aceptado", "accepted")
                         : Localized("rechazado", "rejected"));
            (void)server.Send(frame.connectionId, BuildInventoryActionMessage(
                equipped ? "inventory.loadout_changed" : "inventory.apply_failed",
                command.requestId, command.localId, equipped,
                equipped && item && item->type == LocalInventoryMusicKit
                    ? "Music Kit equipado y solicitado al aplicador externo."
                    : equipped
                        ? "Articulo equipado en el loadout local; espera al host interno para aplicarlo al modelo."
                         : "El articulo no existe, no es valido o aun no puede aplicarse."));
            break;
        }

        case InventoryProtocolCommandType::Unequip: {
            const LocalInventoryItem* item = command.localId != kInvalidLocalItemId
                ? FindLocalInventoryItemById(g_InventoryChanger, command.localId)
                : nullptr;
            const LocalItemId previousId = item
                ? item->localId : g_InventoryChanger.loadout.musicKit;
            const int itemType = item ? item->type : LocalInventoryMusicKit;
            const int team = item
                ? ResolveLoadoutTeam(*item, command.team)
                : LocalInventoryTeamBoth;
            const bool unequipped = UnequipLocalInventoryItemSelection(itemType, team);
            WriteInventoryLog(InventoryLogCategory::Action, InventoryLogLevel::Info,
                "Unequip Local ID %llu: %s.",
                static_cast<unsigned long long>(previousId),
                unequipped ? Localized("aceptado", "accepted")
                           : Localized("rechazado", "rejected"));
            (void)server.Send(frame.connectionId, BuildInventoryActionMessage(
                unequipped ? "inventory.loadout_changed" : "inventory.apply_failed",
                command.requestId, previousId, unequipped,
                unequipped ? "Articulo desequipado del loadout local."
                           : "El articulo no estaba equipado en el equipo solicitado."));
            break;
        }

        case InventoryProtocolCommandType::Add: {
            LocalInventoryItem candidate;
            candidate.occupied = true;
            candidate.type = command.itemType;
            candidate.definitionIndex = command.definitionIndex;
            candidate.paintIndex = command.paintIndex;
            candidate.wear = command.wear;
            candidate.seed = command.seed;
            candidate.statTrak = command.statTrak;
            candidate.statTrakCount = command.statTrakCount;
            candidate.souvenir = command.souvenir;
            const InventoryCatalogItem* catalog = FindInventoryCatalogItem(
                candidate.type, candidate.definitionIndex, candidate.paintIndex);
            if (catalog && catalog->name)
                strncpy_s(candidate.displayName, catalog->name, _TRUNCATE);
            const int slot = catalog ? AddLocalInventoryItem(candidate) : -1;
            const LocalItemId addedId = slot >= 0
                ? g_InventoryChanger.items[slot].localId : kInvalidLocalItemId;
            const bool added = addedId != kInvalidLocalItemId;
            WriteInventoryLog(InventoryLogCategory::Action,
                added ? InventoryLogLevel::Info : InventoryLogLevel::Warning,
                "Add catalog def=%d paint=%d: %s.",
                command.definitionIndex, command.paintIndex,
                added ? Localized("aceptado", "accepted")
                      : Localized("rechazado", "rejected"));
            (void)server.Send(frame.connectionId, BuildInventoryActionMessage(
                added ? "inventory.item_added" : "inventory.apply_failed",
                command.requestId, addedId, added,
                added ? "Articulo validado y agregado a la coleccion local."
                      : "El articulo no existe, es incompatible o la coleccion esta llena."));
            break;
        }

        case InventoryProtocolCommandType::Remove: {
            const int slot = FindLocalInventorySlotById(g_InventoryChanger, command.localId);
            const bool removed = slot >= 0;
            if (removed) RemoveLocalInventoryItem(slot);
            WriteInventoryLog(InventoryLogCategory::Action,
                removed ? InventoryLogLevel::Info : InventoryLogLevel::Warning,
                "Remove Local ID %llu: %s.",
                static_cast<unsigned long long>(command.localId),
                removed ? Localized("aceptado", "accepted")
                        : Localized("rechazado", "rejected"));
            (void)server.Send(frame.connectionId, BuildInventoryActionMessage(
                removed ? "inventory.item_removed" : "inventory.apply_failed",
                command.requestId, command.localId, removed,
                removed ? "Articulo eliminado de la coleccion local."
                        : "No existe un articulo con ese Local ID."));
            break;
        }

        case InventoryProtocolCommandType::Duplicate: {
            const LocalInventoryItem* source = FindLocalInventoryItemById(
                g_InventoryChanger, command.localId);
            LocalItemId duplicatedId = kInvalidLocalItemId;
            if (source && source->validity == LocalInventoryValid) {
                const LocalInventoryItem candidate = *source;
                const int slot = AddLocalInventoryItem(candidate);
                if (slot >= 0) duplicatedId = g_InventoryChanger.items[slot].localId;
            }
            const bool duplicated = duplicatedId != kInvalidLocalItemId;
            WriteInventoryLog(InventoryLogCategory::Action,
                duplicated ? InventoryLogLevel::Info : InventoryLogLevel::Warning,
                "Duplicate Local ID %llu: %s.",
                static_cast<unsigned long long>(command.localId),
                duplicated ? Localized("aceptado", "accepted")
                           : Localized("rechazado", "rejected"));
            (void)server.Send(frame.connectionId, BuildInventoryActionMessage(
                duplicated ? "inventory.item_added" : "inventory.apply_failed",
                command.requestId, duplicated ? duplicatedId : command.localId,
                duplicated,
                duplicated ? "Variante duplicada con un Local ID nuevo."
                           : "El articulo no existe, no es valido o la coleccion esta llena."));
            break;
        }

        case InventoryProtocolCommandType::UpdateStatTrak: {
            const int slot = FindLocalInventorySlotById(
                g_InventoryChanger, command.localId);
            bool updated = false;
            if (slot >= 0) {
                LocalInventoryItem candidate = g_InventoryChanger.items[slot];
                if (candidate.statTrak &&
                    command.statTrakCount >= candidate.statTrakCount) {
                    candidate.statTrakCount = command.statTrakCount;
                    updated = UpdateLocalInventoryItem(slot, candidate);
                }
            }
            WriteInventoryLog(InventoryLogCategory::Action,
                updated ? InventoryLogLevel::Info : InventoryLogLevel::Warning,
                "StatTrak Local ID %llu -> %d: %s.",
                static_cast<unsigned long long>(command.localId),
                command.statTrakCount,
                updated ? Localized("aceptado", "accepted")
                        : Localized("rechazado", "rejected"));
            (void)server.Send(frame.connectionId, BuildInventoryActionMessage(
                updated ? "inventory.item_updated" : "inventory.apply_failed",
                command.requestId, command.localId, updated,
                updated ? "Contador StatTrak sincronizado desde el juego."
                        : "El articulo no admite el contador solicitado."));
            break;
        }

        case InventoryProtocolCommandType::CloseReveal: {
            const bool closed = IsLocalInventoryRevealPending(
                g_InventoryChanger, command.localId);
            if (closed)
                ClearPendingLocalInventoryReveals(g_InventoryChanger);
            (void)server.Send(frame.connectionId, BuildInventoryActionMessage(
                closed ? "inventory.reveal_closed" : "inventory.apply_failed",
                command.requestId, command.localId, closed,
                closed ? "Presentaciones pendientes confirmadas."
                       : "El articulo no coincide con la presentacion pendiente."));
            break;
        }

        case InventoryProtocolCommandType::Inspect:
            (void)server.Send(frame.connectionId, BuildInventoryActionMessage(
                "inventory.apply_failed", command.requestId, command.localId, false,
                "Inspect queda reservado para el prototipo Panorama."));
            break;

        case InventoryProtocolCommandType::Hello:
            break;
        }
    }
};

InventoryIpcController::InventoryIpcController() : impl_(std::make_unique<Impl>()) {}

InventoryIpcController::~InventoryIpcController() {
    Stop();
}

bool InventoryIpcController::Start() {
    return impl_->server.Start();
}

void InventoryIpcController::Stop() {
    SignalInventoryBridgeStop();
    impl_->server.Stop();
    impl_->connectionSessions.clear();
    impl_->sessions.clear();
    impl_->sessionOrder.clear();
    impl_->lastBridgeActivityMs = 0;
    impl_->SetBridgeLease(false);
}

void InventoryIpcController::Pump() {
    impl_->RefreshBridgeLease();
    impl_->PruneConnectionSessions();
    InventoryIpcInboundFrame frame;
    for (int processed = 0;
        processed < kMaxCommandsPerFrame && impl_->server.TryReceive(frame);
        ++processed)
        impl_->ProcessFrame(std::move(frame));
    impl_->PruneConnectionSessions();
    impl_->RefreshBridgeLease();
}

InventoryIpcServerStatus InventoryIpcController::GetStatus() const {
    return impl_->server.GetStatus();
}
