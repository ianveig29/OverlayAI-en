#pragma once

#include "InventoryTypes.h"

#include <cstdint>
#include <string>

constexpr uint32_t kInventoryProtocolVersion = 1;
constexpr uint32_t kInventoryProtocolMaxFrameBytes = 256 * 1024;

enum class InventoryProtocolCommandType {
    Hello,
    RequestRefresh,
    Equip,
    Unequip,
    Add,
    Remove,
    Duplicate,
    UpdateStatTrak,
    Inspect,
    CloseReveal
};

struct InventoryProtocolCommand {
    InventoryProtocolCommandType type = InventoryProtocolCommandType::RequestRefresh;
    uint64_t requestId = 0;
    LocalItemId localId = kInvalidLocalItemId;
    int team = LocalInventoryTeamNone;
    int itemType = LocalInventoryMusicKit;
    int definitionIndex = 0;
    int paintIndex = 0;
    float wear = 0.15f;
    int seed = 0;
    bool statTrak = false;
    int statTrakCount = 0;
    bool souvenir = false;
    std::string clientSessionId;
};

struct InventoryProtocolError {
    uint64_t requestId = 0;
    std::string code;
    std::string message;
};

struct InventoryRuntimeOffsets {
    uint64_t entityList = 0;
    uint64_t localPlayerController = 0;
    uint64_t localPlayerPawn = 0;
    uint64_t inventoryServices = 0;
    uint64_t serviceMusicId = 0;
    uint64_t controllerMusicKitId = 0;
    uint64_t controllerMusicKitMvps = 0;
    uint64_t playerPawnHandle = 0;
    uint64_t controllingBot = 0;
    uint64_t hasFemaleVoice = 0;
    uint64_t teamNumber = 0;
    uint64_t lifeState = 0;
    uint64_t lastSpawnTimeIndex = 0;
    uint64_t gameSceneNode = 0;
    uint64_t modelState = 0;
    uint64_t modelName = 0;
    uint64_t ownerEntity = 0;
    uint64_t hudModelArms = 0;
    uint64_t myWearables = 0;
    uint64_t sceneNodeOwner = 0;
    uint64_t sceneNodeChild = 0;
    uint64_t sceneNodeNextSibling = 0;
    uint64_t weaponServices = 0;
    uint64_t activeWeapon = 0;
    uint64_t subclassId = 0;
    uint64_t viewmodelAttachment = 0;
    uint64_t attributeManager = 0;
    uint64_t item = 0;
    uint64_t itemDefinitionIndex = 0;
    uint64_t entityQuality = 0;
    uint64_t fallbackPaintKit = 0;
    uint64_t fallbackSeed = 0;
    uint64_t fallbackWear = 0;
    uint64_t fallbackStatTrak = 0;
    uint64_t itemId = 0;
    uint64_t itemIdHigh = 0;
    uint64_t itemIdLow = 0;
    uint64_t accountId = 0;
    uint64_t initialized = 0;
    uint64_t needToReApplyGloves = 0;
    uint64_t econGloves = 0;
    uint64_t econGlovesChanged = 0;
};

bool ParseInventoryProtocolCommand(
    const std::string& message, InventoryProtocolCommand& command,
    InventoryProtocolError& error);
std::string BuildInventorySnapshotMessage(
    const InventoryChangerSettings& state, uint64_t requestId,
    const InventoryRuntimeOffsets* runtimeOffsets = nullptr,
    uint64_t musicKitApplyRevision = 0);
std::string BuildInventoryActionMessage(
    const char* messageType, uint64_t requestId, LocalItemId localId,
    bool success, const char* detail);
std::string BuildInventoryProtocolErrorMessage(const InventoryProtocolError& error);
