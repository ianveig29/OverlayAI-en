#include "InventoryProtocol.h"

#include "InventoryCatalog.h"
#include "InventoryValidator.h"
#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>

using Json = nlohmann::json;

namespace {
    bool HasExactKeys(const Json& object, std::initializer_list<const char*> keys) {
        if (!object.is_object() || object.size() != keys.size()) return false;
        for (const char* key : keys) {
            if (!object.contains(key)) return false;
        }
        return true;
    }

    bool ReadUnsigned(const Json& value, uint64_t& result) {
        if (value.is_number_unsigned()) {
            result = value.get<uint64_t>();
            return true;
        }
        if (value.is_number_integer()) {
            const int64_t signedValue = value.get<int64_t>();
            if (signedValue < 0) return false;
            result = static_cast<uint64_t>(signedValue);
            return true;
        }
        return false;
    }

    bool IsValidSessionId(const std::string& value) {
        if (value.empty() || value.size() > 64) return false;
        return std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isalnum(character) || character == '-' || character == '_';
        });
    }

    Json MakeMessage(const char* type, uint64_t requestId, Json payload) {
        return Json{
            {"protocol_version", kInventoryProtocolVersion},
            {"message_type", type ? type : "protocol.error"},
            {"request_id", requestId},
            {"payload", std::move(payload)}
        };
    }

    int GetEffectiveInventoryQuality(
        const LocalInventoryItem& item,
        const InventoryCatalogItem* catalogItem) {
        // StatTrak items use Strange quality independently of their base skin.
        if (item.statTrak) return 9;
        return catalogItem ? catalogItem->quality : 4;
    }

    bool ParseLocalIdPayload(
        const Json& payload, InventoryProtocolCommand& command,
        InventoryProtocolError& error) {
        if (!HasExactKeys(payload, { "local_id" })) {
            error.code = "invalid_payload";
            error.message = "El payload debe contener solamente local_id.";
            return false;
        }
        uint64_t localId = 0;
        if (!ReadUnsigned(payload.at("local_id"), localId) || localId == 0) {
            error.code = "invalid_local_id";
            error.message = "local_id debe ser un entero positivo.";
            return false;
        }
        command.localId = static_cast<LocalItemId>(localId);
        return true;
    }

    bool ReadTeam(const Json& value, int& team) {
        if (!value.is_number_integer()) return false;
        team = value.get<int>();
        return team >= LocalInventoryTeamNone && team <= LocalInventoryTeamBoth;
    }

    bool ParseEquipPayload(
        const Json& payload, InventoryProtocolCommand& command,
        InventoryProtocolError& error) {
        if (HasExactKeys(payload, { "local_id" }))
            return ParseLocalIdPayload(payload, command, error);
        if (!HasExactKeys(payload, { "local_id", "team" })) {
            error.code = "invalid_payload";
            error.message = "inventory.equip requiere local_id y team opcional.";
            return false;
        }
        uint64_t localId = 0;
        if (!ReadUnsigned(payload.at("local_id"), localId) || localId == 0 ||
            !ReadTeam(payload.at("team"), command.team)) {
            error.code = "invalid_loadout_target";
            error.message = "local_id o team no son validos.";
            return false;
        }
        command.localId = static_cast<LocalItemId>(localId);
        return true;
    }

    bool ParseUnequipPayload(
        const Json& payload, InventoryProtocolCommand& command,
        InventoryProtocolError& error) {
        if (payload.empty()) return true;
        return ParseEquipPayload(payload, command, error);
    }

    bool ParseAddPayload(
        const Json& payload, InventoryProtocolCommand& command,
        InventoryProtocolError& error) {
        if (!HasExactKeys(payload, {
            "type", "definition_index", "paint_kit", "wear", "seed",
            "stattrak", "stattrak_count", "souvenir" })) {
            error.code = "invalid_payload";
            error.message = "inventory.add requiere la identidad cosmetica completa.";
            return false;
        }
        if (!payload.at("type").is_number_integer() ||
            !payload.at("definition_index").is_number_integer() ||
            !payload.at("paint_kit").is_number_integer() ||
            !payload.at("wear").is_number() ||
            !payload.at("seed").is_number_integer() ||
            !payload.at("stattrak").is_boolean() ||
            !payload.at("stattrak_count").is_number_integer() ||
            !payload.at("souvenir").is_boolean()) {
            error.code = "invalid_types";
            error.message = "Los campos de inventory.add tienen tipos invalidos.";
            return false;
        }
        command.itemType = payload.at("type").get<int>();
        command.definitionIndex = payload.at("definition_index").get<int>();
        command.paintIndex = payload.at("paint_kit").get<int>();
        command.wear = payload.at("wear").get<float>();
        command.seed = payload.at("seed").get<int>();
        command.statTrak = payload.at("stattrak").get<bool>();
        command.statTrakCount = payload.at("stattrak_count").get<int>();
        command.souvenir = payload.at("souvenir").get<bool>();
        if (command.itemType < 0 || command.itemType >= LocalInventoryItemTypeCount ||
            command.definitionIndex <= 0 || command.paintIndex < 0 ||
            command.wear < 0.0f || command.wear > 1.0f ||
            command.seed < 0 || command.seed > 1000 ||
            command.statTrakCount < 0) {
            error.code = "invalid_item";
            error.message = "La identidad cosmetica esta fuera de rango.";
            return false;
        }
        return true;
    }

    bool ParseStatTrakUpdatePayload(
        const Json& payload, InventoryProtocolCommand& command,
        InventoryProtocolError& error) {
        if (!HasExactKeys(payload, { "local_id", "stattrak_count" }) ||
            !payload.at("stattrak_count").is_number_integer()) {
            error.code = "invalid_payload";
            error.message = "inventory.update_stattrak requiere local_id y stattrak_count.";
            return false;
        }
        uint64_t localId = 0;
        const int count = payload.at("stattrak_count").get<int>();
        if (!ReadUnsigned(payload.at("local_id"), localId) || localId == 0 ||
            count < 0) {
            error.code = "invalid_stattrak_update";
            error.message = "local_id o stattrak_count no son validos.";
            return false;
        }
        command.localId = static_cast<LocalItemId>(localId);
        command.statTrakCount = count;
        return true;
    }
}

bool ParseInventoryProtocolCommand(
    const std::string& message, InventoryProtocolCommand& command,
    InventoryProtocolError& error) {
    command = {};
    error = {};
    if (message.empty() || message.size() > kInventoryProtocolMaxFrameBytes) {
        error.code = "invalid_size";
        error.message = "El mensaje esta vacio o supera el limite.";
        return false;
    }

    const Json root = Json::parse(message, nullptr, false);
    if (root.is_discarded() || !HasExactKeys(
        root, { "protocol_version", "message_type", "request_id", "payload" })) {
        error.code = "invalid_envelope";
        error.message = "El sobre JSON no tiene el formato esperado.";
        return false;
    }

    uint64_t protocolVersion = 0;
    if (!ReadUnsigned(root.at("protocol_version"), protocolVersion) ||
        protocolVersion != kInventoryProtocolVersion) {
        error.code = "unsupported_version";
        error.message = "La version del protocolo no es compatible.";
        return false;
    }
    if (!ReadUnsigned(root.at("request_id"), command.requestId) ||
        command.requestId == 0) {
        error.code = "invalid_request_id";
        error.message = "request_id debe ser un entero positivo.";
        return false;
    }
    error.requestId = command.requestId;

    if (!root.at("message_type").is_string() ||
        root.at("message_type").get_ref<const std::string&>().size() > 64 ||
        !root.at("payload").is_object()) {
        error.code = "invalid_types";
        error.message = "message_type o payload tienen un tipo invalido.";
        return false;
    }

    const std::string& messageType = root.at("message_type").get_ref<const std::string&>();
    const Json& payload = root.at("payload");
    if (messageType == "client.hello") {
        command.type = InventoryProtocolCommandType::Hello;
        if (!HasExactKeys(payload, { "client_session_id" }) ||
            !payload.at("client_session_id").is_string()) {
            error.code = "invalid_payload";
            error.message = "client.hello requiere client_session_id.";
            return false;
        }
        command.clientSessionId = payload.at("client_session_id").get<std::string>();
        if (!IsValidSessionId(command.clientSessionId)) {
            error.code = "invalid_session_id";
            error.message = "client_session_id no es valido.";
            return false;
        }
        return true;
    }
    if (messageType == "inventory.request_refresh") {
        command.type = InventoryProtocolCommandType::RequestRefresh;
        if (!payload.empty()) {
            error.code = "invalid_payload";
            error.message = "inventory.request_refresh no acepta campos.";
            return false;
        }
        return true;
    }
    if (messageType == "inventory.unequip") {
        command.type = InventoryProtocolCommandType::Unequip;
        return ParseUnequipPayload(payload, command, error);
    }

    if (messageType == "inventory.equip") {
        command.type = InventoryProtocolCommandType::Equip;
        return ParseEquipPayload(payload, command, error);
    } else if (messageType == "inventory.add") {
        command.type = InventoryProtocolCommandType::Add;
        return ParseAddPayload(payload, command, error);
    } else if (messageType == "inventory.update_stattrak") {
        command.type = InventoryProtocolCommandType::UpdateStatTrak;
        return ParseStatTrakUpdatePayload(payload, command, error);
    } else if (messageType == "inventory.remove")
        command.type = InventoryProtocolCommandType::Remove;
    else if (messageType == "inventory.duplicate")
        command.type = InventoryProtocolCommandType::Duplicate;
    else if (messageType == "inventory.inspect")
        command.type = InventoryProtocolCommandType::Inspect;
    else if (messageType == "inventory.close_reveal")
        command.type = InventoryProtocolCommandType::CloseReveal;
    else {
        error.code = "unknown_message_type";
        error.message = "El tipo de mensaje no esta soportado.";
        return false;
    }
    return ParseLocalIdPayload(payload, command, error);
}

std::string BuildInventorySnapshotMessage(
    const InventoryChangerSettings& state, uint64_t requestId,
    const InventoryRuntimeOffsets* runtimeOffsets,
    uint64_t musicKitApplyRevision) {
    Json items = Json::array();
    for (const LocalInventoryItem& item : state.items) {
        if (!item.occupied) continue;
        const InventoryCatalogItem* catalogItem = FindInventoryCatalogItem(
            item.type, item.definitionIndex, item.paintIndex);
        items.push_back({
            {"local_id", item.localId},
            {"type", item.type},
            {"definition_index", item.definitionIndex},
            {"paint_kit", item.paintIndex},
            {"wear", item.wear},
            {"seed", item.seed},
            {"stattrak", item.statTrak},
            {"stattrak_count", item.statTrakCount},
            {"souvenir", item.souvenir},
            {"custom_name", item.customName},
            {"display_name", item.displayName},
            {"equipped_team", item.equippedTeam},
            {"acquired_at", item.acquiredAt},
            {"validity", GetLocalInventoryValidityName(item.validity)},
            {"group", catalogItem ? catalogItem->group : ""},
            {"rarity", catalogItem ? catalogItem->rarity : ""},
            {"rarity_rank", catalogItem
                ? GetInventoryRarityRank(catalogItem->rarity) : 0},
            {"quality", GetEffectiveInventoryQuality(item, catalogItem)},
            {"legacy_model", catalogItem ? catalogItem->legacyModel : false},
            {"rarity_color", catalogItem ? catalogItem->rarityColor : 0},
            {"image_url", catalogItem ? catalogItem->imageUrl : ""}
        });
    }

    Json pendingRevealItemIds = Json::array();
    for (int index = 0; index < state.pendingRevealItemCount; ++index)
        pendingRevealItemIds.push_back(state.pendingRevealItemIds[index]);

    Json payload{
        {"inventory_version", state.storageVersion},
        {"next_local_id", state.nextLocalId},
        {"selected_local_id", state.selectedLocalId},
        {"pending_reveal_item_id", state.pendingRevealItemId},
        {"pending_reveal_item_ids", pendingRevealItemIds},
        {"pending_reveal_count", state.pendingRevealItemCount},
        {"queue_reveal_when_unavailable", state.queueRevealWhenUnavailable},
        {"apply_knives_to_controlled_bots",
            state.applyKnivesToControlledBots},
        {"use_debug_panorama_ui", state.useDebugPanoramaUi},
        {"enabled", state.enabled},
        {"music_kit_apply_revision", musicKitApplyRevision},
        {"runtime_offsets", {
            {"entity_list", runtimeOffsets ? runtimeOffsets->entityList : 0},
            {"local_player_controller", runtimeOffsets ? runtimeOffsets->localPlayerController : 0},
            {"local_player_pawn", runtimeOffsets ? runtimeOffsets->localPlayerPawn : 0},
            {"inventory_services", runtimeOffsets ? runtimeOffsets->inventoryServices : 0},
            {"service_music_id", runtimeOffsets ? runtimeOffsets->serviceMusicId : 0},
            {"controller_music_kit_id", runtimeOffsets ? runtimeOffsets->controllerMusicKitId : 0},
            {"controller_music_kit_mvps", runtimeOffsets ? runtimeOffsets->controllerMusicKitMvps : 0},
            {"player_pawn_handle", runtimeOffsets ? runtimeOffsets->playerPawnHandle : 0},
            {"controlling_bot", runtimeOffsets ? runtimeOffsets->controllingBot : 0},
            {"has_female_voice", runtimeOffsets ? runtimeOffsets->hasFemaleVoice : 0},
            {"team_number", runtimeOffsets ? runtimeOffsets->teamNumber : 0},
            {"life_state", runtimeOffsets ? runtimeOffsets->lifeState : 0},
            {"last_spawn_time_index", runtimeOffsets ? runtimeOffsets->lastSpawnTimeIndex : 0},
            {"game_scene_node", runtimeOffsets ? runtimeOffsets->gameSceneNode : 0},
            {"model_state", runtimeOffsets ? runtimeOffsets->modelState : 0},
            {"model_name", runtimeOffsets ? runtimeOffsets->modelName : 0},
            {"owner_entity", runtimeOffsets ? runtimeOffsets->ownerEntity : 0},
            {"hud_model_arms", runtimeOffsets ? runtimeOffsets->hudModelArms : 0},
            {"my_wearables", runtimeOffsets ? runtimeOffsets->myWearables : 0},
            {"scene_node_owner", runtimeOffsets ? runtimeOffsets->sceneNodeOwner : 0},
            {"scene_node_child", runtimeOffsets ? runtimeOffsets->sceneNodeChild : 0},
            {"scene_node_next_sibling", runtimeOffsets ? runtimeOffsets->sceneNodeNextSibling : 0},
            {"weapon_services", runtimeOffsets ? runtimeOffsets->weaponServices : 0},
            {"active_weapon", runtimeOffsets ? runtimeOffsets->activeWeapon : 0},
            {"subclass_id", runtimeOffsets ? runtimeOffsets->subclassId : 0},
            {"viewmodel_attachment", runtimeOffsets ? runtimeOffsets->viewmodelAttachment : 0},
            {"attribute_manager", runtimeOffsets ? runtimeOffsets->attributeManager : 0},
            {"item", runtimeOffsets ? runtimeOffsets->item : 0},
            {"item_definition_index", runtimeOffsets ? runtimeOffsets->itemDefinitionIndex : 0},
            {"entity_quality", runtimeOffsets ? runtimeOffsets->entityQuality : 0},
            {"fallback_paint_kit", runtimeOffsets ? runtimeOffsets->fallbackPaintKit : 0},
            {"fallback_seed", runtimeOffsets ? runtimeOffsets->fallbackSeed : 0},
            {"fallback_wear", runtimeOffsets ? runtimeOffsets->fallbackWear : 0},
            {"fallback_stattrak", runtimeOffsets ? runtimeOffsets->fallbackStatTrak : 0},
            {"item_id", runtimeOffsets ? runtimeOffsets->itemId : 0},
            {"item_id_high", runtimeOffsets ? runtimeOffsets->itemIdHigh : 0},
            {"item_id_low", runtimeOffsets ? runtimeOffsets->itemIdLow : 0},
            {"account_id", runtimeOffsets ? runtimeOffsets->accountId : 0},
            {"initialized", runtimeOffsets ? runtimeOffsets->initialized : 0},
            {"need_to_reapply_gloves", runtimeOffsets ? runtimeOffsets->needToReApplyGloves : 0},
            {"econ_gloves", runtimeOffsets ? runtimeOffsets->econGloves : 0},
            {"econ_gloves_changed", runtimeOffsets ? runtimeOffsets->econGlovesChanged : 0}
        }},
        {"loadout", {
            {"music_kit", state.loadout.musicKit},
            {"terrorist_knife", state.loadout.terroristKnife},
            {"counter_terrorist_knife", state.loadout.counterTerroristKnife},
            {"terrorist_gloves", state.loadout.terroristGloves},
            {"counter_terrorist_gloves", state.loadout.counterTerroristGloves},
            {"terrorist_agent", state.loadout.terroristAgent},
            {"counter_terrorist_agent", state.loadout.counterTerroristAgent}
        }},
        {"items", std::move(items)}
    };
    return MakeMessage("inventory.snapshot", requestId, std::move(payload)).dump();
}

std::string BuildInventoryActionMessage(
    const char* messageType, uint64_t requestId, LocalItemId localId,
    bool success, const char* detail) {
    return MakeMessage(messageType, requestId, {
        {"local_id", localId},
        {"success", success},
        {"detail", detail ? detail : ""}
    }).dump();
}

std::string BuildInventoryProtocolErrorMessage(const InventoryProtocolError& error) {
    return MakeMessage("protocol.error", error.requestId, {
        {"code", error.code.empty() ? "unknown_error" : error.code},
        {"message", error.message}
    }).dump();
}
