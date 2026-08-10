#include <windows.h>
#include <strsafe.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <atomic>

#include "BridgeLogging.h"
#include "BuildCompatibility.h"
#include "PanoramaDiagnostics.h"
#include "PanoramaMount.h"
#include "InventorySocache.h"

namespace {
    constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\OverlayAI.Inventory.v1";
    constexpr wchar_t kStopEventName[] = L"Local\\OverlayAI.InventoryBridge.Stop";
    constexpr wchar_t kReadyEventName[] = L"Local\\OverlayAI.InventoryBridge.Ready";
    constexpr wchar_t kMutexName[] = L"Local\\OverlayAI.InventoryBridge.Singleton";
    constexpr DWORD kMaxFrameBytes = 256 * 1024;
    constexpr DWORD kSnapshotRefreshMs = 100;
    constexpr ULONGLONG kModelUpdateIntervalMs = 100;
    constexpr int kIpcFailuresBeforeRestore = 30;
    constexpr int kTerroristTeam = 2;
    constexpr int kCounterTerroristTeam = 3;
    constexpr int kKnifeLoadoutSlot = 0;
    constexpr int kGloveLoadoutSlot = 41;
    constexpr int kMusicKitLoadoutSlot = 54;
    // Collection-only objects never enter a loadout. Slot 63 is an internal
    // ownership tag used solely to prune generated SOCache entries safely.
    constexpr int kMiscCollectionSlot = 63;
    constexpr int kWeaponSkinCollectionSlot = 62;
    constexpr int kMusicKitItemDefinition = 1314;
    constexpr int32_t kUnusualItemQuality = 3;
    constexpr int32_t kStrangeItemQuality = 9;
    constexpr int kMaxPublishedKnifeItems = 256;
    constexpr int kMaxPublishedMusicKitItems = 256;
    constexpr int kMaxPublishedGloveItems = 256;
    constexpr int kMaxPublishedMiscItems = 256;
    constexpr int kMaxPublishedWeaponSkinItems = 256;
    constexpr int kMaxPendingRevealItems = 256;
    constexpr SIZE_T kFrameStageIndex = 36;
    constexpr int kAgentFrameStage = 6;

    HANDLE g_stopEvent = nullptr;
    using FrameStageFn = void(__fastcall*)(void*, int);
    using SetModelFn = void* (__fastcall*)(void*, const char*);
    using SetBodyGroupFn = void(__fastcall*)(void*, uint64_t, uint64_t);
    using SetMeshGroupMaskFn = void(__fastcall*)(void*, uint64_t);
    using UpdateSubclassFn = void(__fastcall*)(void*);
    using RefreshWeaponModulesFn = bool(__fastcall*)(void*, void*);
    using ClearWeaponCustomMaterialsFn = void(__fastcall*)(void*, bool);
    using FindHudElementFn = void* (__fastcall*)(const char*);
    using ClearHudWeaponIconFn = int(__fastcall*)(void*, int, int);
    using FireEventClientSideFn = bool(__fastcall*)(void*, void*);
    using ResolveGloveEntityFn = void* (__fastcall*)(void*);
    using ResolveGloveOwnerFn = void* (__fastcall*)(void*);
    using SpawnGloveModelFn = uint32_t* (__fastcall*)(void*, uint32_t*);
    using AttachGloveModelFn = bool(__fastcall*)(void*, void*);
    using RegisterGloveRenderFn = void(__fastcall*)(void*, void*);
    using ProcessGloveModelFn = void(__fastcall*)(void*);
    using UpdateGlovePawnFn = void(__fastcall*)(void*);
    FrameStageFn g_originalFrameStage = nullptr;
    SetModelFn g_setModel = nullptr;
    SetBodyGroupFn g_setBodyGroup = nullptr;
    SetMeshGroupMaskFn g_setMeshGroupMask = nullptr;
    UpdateSubclassFn g_updateSubclass = nullptr;
    RefreshWeaponModulesFn g_refreshWeaponModules = nullptr;
    ClearWeaponCustomMaterialsFn g_clearWeaponCustomMaterials = nullptr;
    FindHudElementFn g_findHudElement = nullptr;
    ClearHudWeaponIconFn g_clearHudWeaponIcon = nullptr;
    FireEventClientSideFn g_originalFireEventClientSide = nullptr;
    ResolveGloveEntityFn g_resolveGloveEntity = nullptr;
    ResolveGloveOwnerFn g_resolveGloveOwner = nullptr;
    SpawnGloveModelFn g_spawnGloveModel = nullptr;
    uintptr_t g_spawnGloveCallsite = 0;
    void* g_spawnGloveRelay = nullptr;
    uint64_t g_spawnGloveOriginalBytes = 0;
    volatile LONG g_spawnGloveCalls = 0;
    volatile LONG g_spawnGloveLastHandle = -1;
    volatile LONG64 g_spawnGloveLastEntity = 0;
    AttachGloveModelFn g_attachGloveModel = nullptr;
    uintptr_t g_attachGloveCallsite = 0;
    void* g_attachGloveRelay = nullptr;
    int32_t g_attachGloveOriginalDisplacement = 0;
    volatile LONG g_attachGloveCalls = 0;
    volatile LONG g_attachGloveLastResult = 0;
    volatile LONG64 g_attachGloveLastOwner = 0;
    volatile LONG64 g_attachGloveLastEntity = 0;
    RegisterGloveRenderFn g_registerGloveRender = nullptr;
    void** g_registerGloveRenderSlot = nullptr;
    volatile LONG g_registerGloveRenderCalls = 0;
    volatile LONG64 g_registerGloveRenderEntity = 0;
    volatile LONG64 g_registerGloveRenderBefore = 0;
    volatile LONG64 g_registerGloveRenderAfter = 0;
    ProcessGloveModelFn g_processGloveModel = nullptr;
    uintptr_t g_processGloveCallsite = 0;
    void* g_processGloveRelay = nullptr;
    int32_t g_processGloveOriginalDisplacement = 0;
    volatile LONG g_processGloveCalls = 0;
    volatile LONG64 g_processGloveAfterVtable = 0;
    volatile LONG64 g_processGloveAfterResolved = 0;
    UpdateGlovePawnFn g_updateGlovePawn = nullptr;
    uintptr_t g_updateGloveCallsite = 0;
    void* g_updateGloveRelay = nullptr;
    int32_t g_updateGloveOriginalDisplacement = 0;
    volatile LONG g_updateGloveObservedSpawn = 0;
    volatile LONG64 g_updateGloveBeforeResolved = 0;
    volatile LONG64 g_updateGloveAfterResolved = 0;
    volatile LONG64 g_updateGloveAfterVtable = 0;
    void** g_fireEventClientSideSlot = nullptr;
    thread_local bool g_handlingKillFeedPreHook = false;
    void** g_frameStageSlot = nullptr;
    volatile LONG g_frameStageSixCalls = 0;
    bool g_weaponSkinSocacheAllowed = true;



    struct AgentModel {
        int definitionIndex;
        const char* modelPath;
    };

    struct KnifeModel {
        int definitionIndex;
        uint32_t subclassId;
        const char* modelPath;
        const char* eventName;
    };

    constexpr AgentModel kAgentModels[] = {
#include "AgentModels.inc"
    };

    constexpr KnifeModel kKnifeModels[] = {
#include "KnifeModels.inc"
    };

    struct InventorySnapshotSelection {
        struct MusicKitCollectionItem {
            uint64_t localId = 0;
            int musicKitId = 0;
            bool statTrak = false;
            int statTrakCount = 0;
        };

        struct KnifeCollectionItem {
            uint64_t localId = 0;
            int definitionIndex = 0;
            int paintKit = 0;
            int seed = 0;
            float wear = 0.15f;
            bool statTrak = false;
            int statTrakCount = 0;
        };

        struct WeaponSkinCollectionItem {
            uint64_t localId = 0;
            int definitionIndex = 0;
            int paintKit = 0;
            int seed = 0;
            float wear = 0.15f;
            bool statTrak = false;
            int statTrakCount = 0;
            uint8_t quality = 4;
            uint8_t rarity = 0;
        };

        struct GloveCollectionItem {
            uint64_t localId = 0;
            int definitionIndex = 0;
            int paintKit = 0;
            int seed = 0;
            float wear = 0.15f;
            bool statTrak = false;
            int statTrakCount = 0;
        };

        struct MiscCollectionItem {
            uint64_t localId = 0;
            int itemType = 0;
            int definitionIndex = 0;
            int variantAttributeDefinition = 0;
            uint32_t variantAttributeValue = 0;
            uint8_t quality = 4;
            uint8_t rarity = 0;
        };

        bool enabled = false;
        bool applyKnivesToControlledBots = false;
        uint64_t pendingRevealLocalId = 0;
        uint64_t pendingRevealLocalIds[kMaxPendingRevealItems]{};
        int pendingRevealItemCount = 0;
        uint64_t musicKit = 0;
        uint64_t terroristAgent = 0;
        uint64_t counterTerroristAgent = 0;
        uint64_t terroristKnife = 0;
        uint64_t counterTerroristKnife = 0;
        uint64_t terroristGloves = 0;
        uint64_t counterTerroristGloves = 0;
        int musicKitDefinition = 0;
        int terroristDefinition = 0;
        int counterTerroristDefinition = 0;
        int terroristKnifeDefinition = 0;
        int counterTerroristKnifeDefinition = 0;
        int terroristKnifePaintKit = 0;
        int counterTerroristKnifePaintKit = 0;
        int terroristKnifeSeed = 0;
        int counterTerroristKnifeSeed = 0;
        float terroristKnifeWear = 0.15f;
        float counterTerroristKnifeWear = 0.15f;
        bool terroristKnifeStatTrak = false;
        bool counterTerroristKnifeStatTrak = false;
        int terroristKnifeStatTrakCount = 0;
        int counterTerroristKnifeStatTrakCount = 0;
        int terroristGlovesDefinition = 0;
        int counterTerroristGlovesDefinition = 0;
        int terroristGlovesPaintKit = 0;
        int counterTerroristGlovesPaintKit = 0;
        int terroristGlovesSeed = 0;
        int counterTerroristGlovesSeed = 0;
        float terroristGlovesWear = 0.15f;
        float counterTerroristGlovesWear = 0.15f;
        bool terroristGlovesStatTrak = false;
        bool counterTerroristGlovesStatTrak = false;
        int terroristGlovesStatTrakCount = 0;
        int counterTerroristGlovesStatTrakCount = 0;
        MusicKitCollectionItem musicKitItems[
            kMaxPublishedMusicKitItems]{};
        int musicKitItemCount = 0;
        uint64_t musicKitCollectionHash = 0;
        uint64_t musicKitApplyRevision = 0;
        KnifeCollectionItem knifeItems[kMaxPublishedKnifeItems]{};
        int knifeItemCount = 0;
        uint64_t knifeCollectionHash = 0;
        WeaponSkinCollectionItem weaponSkinItems[
            kMaxPublishedWeaponSkinItems]{};
        int weaponSkinItemCount = 0;
        uint64_t weaponSkinCollectionHash = 0;
        GloveCollectionItem gloveItems[kMaxPublishedGloveItems]{};
        int gloveItemCount = 0;
        uint64_t gloveCollectionHash = 0;
        MiscCollectionItem miscItems[kMaxPublishedMiscItems]{};
        int miscItemCount = 0;
        uint64_t miscCollectionHash = 0;
        uintptr_t entityListOffset = 0;
        uintptr_t localPlayerControllerOffset = 0;
        uintptr_t localPlayerPawnOffset = 0;
        uintptr_t inventoryServicesOffset = 0;
        uintptr_t serviceMusicIdOffset = 0;
        uintptr_t controllerMusicKitIdOffset = 0;
        uintptr_t controllerMusicKitMvpsOffset = 0;
        uintptr_t playerPawnHandleOffset = 0;
        uintptr_t controllingBotOffset = 0;
        uintptr_t hasFemaleVoiceOffset = 0;
        uintptr_t teamNumberOffset = 0;
        uintptr_t lifeStateOffset = 0;
        uintptr_t lastSpawnTimeIndexOffset = 0;
        uintptr_t gameSceneNodeOffset = 0;
        uintptr_t modelStateOffset = 0;
        uintptr_t modelNameOffset = 0;
        uintptr_t ownerEntityOffset = 0;
        uintptr_t hudModelArmsOffset = 0;
        uintptr_t myWearablesOffset = 0;
        uintptr_t sceneNodeOwnerOffset = 0;
        uintptr_t sceneNodeChildOffset = 0;
        uintptr_t sceneNodeNextSiblingOffset = 0;
        uintptr_t weaponServicesOffset = 0;
        uintptr_t activeWeaponOffset = 0;
        uintptr_t subclassIdOffset = 0;
        uintptr_t viewmodelAttachmentOffset = 0;
        uintptr_t attributeManagerOffset = 0;
        uintptr_t itemOffset = 0;
        uintptr_t itemDefinitionIndexOffset = 0;
        uintptr_t entityQualityOffset = 0;
        uintptr_t fallbackPaintKitOffset = 0;
        uintptr_t fallbackSeedOffset = 0;
        uintptr_t fallbackWearOffset = 0;
        uintptr_t fallbackStatTrakOffset = 0;
        uintptr_t itemIdOffset = 0;
        uintptr_t itemIdHighOffset = 0;
        uintptr_t itemIdLowOffset = 0;
        uintptr_t accountIdOffset = 0;
        uintptr_t initializedOffset = 0;
        uintptr_t needToReApplyGlovesOffset = 0;
        uintptr_t econGlovesOffset = 0;
        uintptr_t econGlovesChangedOffset = 0;
    };

    SRWLOCK g_selectionLock = SRWLOCK_INIT;
    InventorySnapshotSelection g_latestSelection{};
    bool g_hasLatestSelection = false;

    struct AppliedAgentState {
        uintptr_t pawn = 0;
        int team = 0;
        int definitionIndex = 0;
        ULONGLONG nextUpdateAt = 0;
        bool applied = false;
        bool modelApplied = false;
        bool voiceApplied = false;
        bool originalFemaleVoice = false;
        bool targetFemaleVoice = false;
        char originalModel[260]{};
        char targetModel[260]{};
    };

    AppliedAgentState g_appliedAgent{};
    struct AppliedMusicKitState {
        uintptr_t controller = 0;
        uintptr_t inventoryServices = 0;
        uintptr_t localPlayerControllerOffset = 0;
        uintptr_t inventoryServicesOffset = 0;
        uintptr_t controllerMusicKitIdOffset = 0;
        uintptr_t controllerMusicKitMvpsOffset = 0;
        uintptr_t serviceMusicIdOffset = 0;
        int originalControllerMusicKitId = 0;
        int originalControllerMusicKitMvps = 0;
        uint16_t originalServiceMusicId = 0;
        int targetDefinition = 0;
        int targetMvps = 0;
        uint64_t applyRevision = 0;
        bool hasOriginal = false;
        bool applied = false;
    };
    AppliedMusicKitState g_appliedMusicKit{};
    struct AppliedKnifeState {
        uintptr_t pawn = 0;
        uintptr_t entityStride = 0;
        uintptr_t weapon = 0;
        uintptr_t viewmodel = 0;
        uint32_t weaponHandle = 0;
        int definitionIndex = 0;
        uint16_t originalDefinition = 0;
        uint32_t originalSubclass = 0;
        int32_t originalQuality = 0;
        int originalPaintKit = 0;
        int originalSeed = 0;
        float originalWear = 0.0f;
        int originalStatTrak = -1;
        uint64_t originalItemId = 0;
        uint32_t originalItemIdHigh = 0;
        uint32_t originalItemIdLow = 0;
        uint32_t originalAccountId = 0;
        uint8_t originalInitialized = 0;
        uint64_t generatedItemId = 0;
        uint32_t generatedAccountId = 0;
        int targetPaintKit = 0;
        int targetSeed = 0;
        float targetWear = 0.15f;
        int targetStatTrak = -1;
        float spawnTimeIndex = 0.0f;
        ULONGLONG nextUpdateAt = 0;
        ULONGLONG hudRefreshAt = 0;
        ULONGLONG nextCompositeRefreshAt = 0;
        bool subclassApplied = false;
        bool qualityApplied = false;
        bool finishApplied = false;
        bool itemIdHighApplied = false;
        bool weaponApplied = false;
        bool viewmodelApplied = false;
        bool activeLastCheck = false;
        bool hudRefreshed = false;
        bool appliedToControlledPawn = false;
        bool weaponModulesRefreshed = false;
        int hudRefreshAttempts = 0;
        int compositeRefreshesRemaining = 0;
        char originalWeaponModel[260]{};
        char originalViewmodelModel[260]{};
        char targetModel[260]{};
    };

    AppliedKnifeState g_appliedKnife{};
    struct AppliedGloveState {
        uintptr_t pawn = 0;
        uint64_t localId = 0;
        int team = 0;
        int definitionIndex = 0;
        int paintKit = 0;
        int seed = 0;
        float wear = 0.15f;
        bool statTrak = false;
        int statTrakCount = 0;
        uint64_t generatedItemId = 0;
        uint16_t originalDefinition = 0;
        int32_t originalQuality = 0;
        uint64_t originalItemId = 0;
        uint32_t originalItemIdHigh = 0;
        uint32_t originalItemIdLow = 0;
        uint32_t originalAccountId = 0;
        uint8_t originalInitialized = 0;
        uint8_t originalChanged = 0;
        bool originalReapply = false;
        float spawnTimeIndex = 0.0f;
        ULONGLONG nextUpdateAt = 0;
        int reapplyFrames = 0;
        bool bodyGroupApplied = false;
        bool applied = false;
    };

    AppliedGloveState g_appliedGloves{};
    enum class GloveLifecycleState {
        Disabled,
        WaitingForPawn,
        WaitingForWearable,
        WaitingForStableContext,
        Applying,
        Applied,
        EntityLost,
        PendingReapply,
        Restoring
    };

    struct GloveContextIdentity {
        uint32_t pawnHandle = 0;
        uint32_t wearableHandle = 0;
        uint32_t hudArmsHandle = 0;
        float pawnSpawnTime = 0.0f;
        int team = 0;
        uintptr_t pawnAddress = 0;
        uintptr_t wearableAddress = 0;
        uintptr_t itemViewAddress = 0;
        uint32_t ownerHandle = 0;
        uintptr_t ownerAddress = 0;
        uintptr_t sceneNodeAddress = 0;
        uintptr_t sceneNodeOwner = 0;
        uintptr_t sceneNodeParent = 0;
        uintptr_t sceneNodeParentOwner = 0;
        uintptr_t hudArmsAddress = 0;
        uintptr_t hudSceneNodeAddress = 0;
        uint64_t gloveControlFingerprint = 0;
        uintptr_t gloveControlCachedResource = 0;
        uintptr_t gloveControlPawn = 0;
        uint32_t gloveControlHandle = 0;
        uint8_t gloveControlReady = 0;
        uint64_t hudChildrenFingerprint = 0;
        int hudChildrenCount = 0;
        int hudBodyGroupCount = -1;
        int hudBodyGroup0 = -1;
        int hudBodyGroup1 = -1;
        uint16_t definitionIndex = 0;
        uint64_t itemId = 0;
        uint8_t initialized = 0;
        uint8_t changedCounter = 0;
    };

    GloveLifecycleState g_gloveLifecycleState =
        GloveLifecycleState::WaitingForPawn;
    GloveContextIdentity g_lastGloveContext{};
    GloveContextIdentity g_nativeReleasedGloveContext{};
    bool g_nativeGloveReleaseObserved = false;
    uintptr_t g_pendingKnifeWeapon = 0;
    uint32_t g_pendingKnifeHandle = 0;
    ULONGLONG g_pendingKnifeSince = 0;
    ULONGLONG g_fastKnifeReapplyUntil = 0;
    enum class NativeLoadoutCommandType {
        None,
        Equip,
        Unequip
    };
    struct NativeLoadoutCommand {
        NativeLoadoutCommandType type = NativeLoadoutCommandType::None;
        uint64_t localId = 0;
        int team = 0;
    };
    SRWLOCK g_nativeLoadoutCommandLock = SRWLOCK_INIT;
    // Index 0 is the global Music Kit loadout. Indexes 1 and 2 are the
    // protocol T/CT knife loadouts.
    NativeLoadoutCommand g_pendingNativeLoadoutCommands[3]{};
    struct StatTrakCommand {
        uint64_t localId = 0;
        int count = 0;
    };
    struct PendingMusicKitMvpObservation {
        uint64_t localId = 0;
        int baseCount = 0;
        ULONGLONG observeAfter = 0;
    };
    constexpr int kMaxPendingStatTrakCommands = 8;
    SRWLOCK g_statTrakCommandLock = SRWLOCK_INIT;
    StatTrakCommand g_pendingStatTrakCommands[
        kMaxPendingStatTrakCommands]{};
    SRWLOCK g_musicKitMvpObservationLock = SRWLOCK_INIT;
    PendingMusicKitMvpObservation g_pendingMusicKitMvpObservation{};
    volatile LONG g_inventoryCollectionSyncing = 0;
    volatile LONG g_inventoryLoadoutReapplyRequested = 0;
    volatile LONG g_nativeLoadoutObservationEpoch = 0;
    std::atomic<ULONGLONG> g_nativeLoadoutObserveAfter{ 0 };
    constexpr ULONGLONG kLoadoutSettleMs = 3500;
    constexpr ULONGLONG kSocacheRetryMs = 1000;
    constexpr ULONGLONG kRetryLogIntervalMs = 10000;
    volatile LONG g_agentModelApplied = 0;
    volatile LONG g_knifeModelApplied = 0;
    volatile LONG g_gloveModelApplied = 0;
    volatile LONG g_nativeGloveLoadoutApplied = 0;
    void RunAgentModelControl();
    void RunInventoryCollectionControl();
    void RunWeaponSkinCollectionControl();
    void RunGloveCollectionControl();
    void RunMiscCollectionControl();
    void RunMusicKitCollectionControl();
    void RunMusicKitRuntimeControl();
    void RestoreMusicKitRuntimeControl();
    void RunPendingRevealAcknowledgementControl();
    void RunNativeInventoryLoadoutControl();
    void RunKnifeModelControl();
    void RunGloveModelControl();
    void LogKnifeDryRun(const char* json);

    struct StringToken {
        uint32_t hash = 0;
    };

    struct EventBuffer {
        char padding[8]{};
        const char* name = nullptr;
    };

    class GameEventListener {
    public:
        virtual ~GameEventListener() = default;
        virtual void FireGameEvent(void* event) = 0;
    };

    class InventoryEventListener final : public GameEventListener {
    public:
        void FireGameEvent(void* event) override;
    };

    InventoryEventListener g_inventoryEventListener{};
    void* g_eventManager = nullptr;
    bool g_killFeedListenerInstalled = false;

    const char* FindJsonValue(const char* json, const char* key) {
        if (!json || !key) return nullptr;
        char token[96]{};
        if (FAILED(StringCchPrintfA(
            token, _countof(token), "\"%s\":", key)))
            return nullptr;
        const char* found = strstr(json, token);
        return found ? found + lstrlenA(token) : nullptr;
    }

    bool ReadJsonUnsigned(
        const char* json, const char* key, uint64_t& value) {
        const char* cursor = FindJsonValue(json, key);
        if (!cursor) return false;
        uint64_t parsed = 0;
        bool hasDigit = false;
        while (*cursor >= '0' && *cursor <= '9') {
            hasDigit = true;
            parsed = parsed * 10 + static_cast<uint64_t>(*cursor - '0');
            ++cursor;
        }
        if (!hasDigit) return false;
        value = parsed;
        return true;
    }

    bool ReadJsonBool(const char* json, const char* key, bool& value) {
        const char* cursor = FindJsonValue(json, key);
        if (!cursor) return false;
        if (strncmp(cursor, "true", 4) == 0) {
            value = true;
            return true;
        }
        if (strncmp(cursor, "false", 5) == 0) {
            value = false;
            return true;
        }
        return false;
    }

    bool ReadJsonFloat(const char* json, const char* key, float& value) {
        const char* cursor = FindJsonValue(json, key);
        if (!cursor) return false;
        char* end = nullptr;
        const float parsed = strtof(cursor, &end);
        if (!end || end == cursor) return false;
        value = parsed;
        return true;
    }

    bool FindInventoryItemDetails(
        const char* json, uint64_t localId, uint64_t expectedType,
        int& definitionIndex, int* paintKit = nullptr, int* seed = nullptr,
        float* wear = nullptr, bool* statTrak = nullptr,
        int* statTrakCount = nullptr) {
        if (!json || localId == 0) return false;
        constexpr char localIdToken[] = "\"local_id\":";
        const char* cursor = json;
        while ((cursor = strstr(cursor, localIdToken)) != nullptr) {
            const char* valueStart = cursor + sizeof(localIdToken) - 1;
            uint64_t parsedId = 0;
            bool hasDigit = false;
            const char* valueCursor = valueStart;
            while (*valueCursor >= '0' && *valueCursor <= '9') {
                hasDigit = true;
                parsedId = parsedId * 10 +
                    static_cast<uint64_t>(*valueCursor - '0');
                ++valueCursor;
            }
            if (hasDigit && parsedId == localId) {
                const char* itemStart = cursor;
                while (itemStart > json && *itemStart != '{') --itemStart;
                const char* itemEnd = strchr(valueCursor, '}');
                const char* definition = strstr(
                    itemStart, "\"definition_index\":");
                const char* type = strstr(itemStart, "\"type\":");
                const char* paint = strstr(itemStart, "\"paint_kit\":");
                const char* itemSeed = strstr(itemStart, "\"seed\":");
                const char* itemWear = strstr(itemStart, "\"wear\":");
                const char* itemStatTrak = strstr(itemStart, "\"stattrak\":");
                const char* itemStatTrakCount = strstr(
                    itemStart, "\"stattrak_count\":");
                if (*itemStart != '{' || !itemEnd || !definition ||
                    definition >= itemEnd || !type || type >= itemEnd)
                    return false;

                uint64_t parsedType = 0;
                uint64_t parsedDefinition = 0;
                if (!ReadJsonUnsigned(type, "type", parsedType) ||
                    parsedType != expectedType ||
                    !ReadJsonUnsigned(
                        definition, "definition_index", parsedDefinition) ||
                    parsedDefinition > 0x7FFFFFFF)
                    return false;
                definitionIndex = static_cast<int>(parsedDefinition);
                uint64_t parsedValue = 0;
                if (paintKit && paint && paint < itemEnd &&
                    ReadJsonUnsigned(paint, "paint_kit", parsedValue) &&
                    parsedValue <= 0x7FFFFFFF)
                    *paintKit = static_cast<int>(parsedValue);
                parsedValue = 0;
                if (seed && itemSeed && itemSeed < itemEnd &&
                    ReadJsonUnsigned(itemSeed, "seed", parsedValue) &&
                    parsedValue <= 0x7FFFFFFF)
                    *seed = static_cast<int>(parsedValue);
                if (wear && itemWear && itemWear < itemEnd)
                    (void)ReadJsonFloat(itemWear, "wear", *wear);
                if (statTrak && itemStatTrak && itemStatTrak < itemEnd)
                    (void)ReadJsonBool(itemStatTrak, "stattrak", *statTrak);
                parsedValue = 0;
                if (statTrakCount && itemStatTrakCount &&
                    itemStatTrakCount < itemEnd &&
                    ReadJsonUnsigned(itemStatTrakCount, "stattrak_count",
                        parsedValue) && parsedValue <= 0x7FFFFFFF)
                    *statTrakCount = static_cast<int>(parsedValue);
                return true;
            }
            cursor = valueCursor;
        }
        return false;
    }

    bool FindAgentDefinition(
        const char* json, uint64_t localId, int& definitionIndex) {
        return FindInventoryItemDetails(
            json, localId, 4, definitionIndex);
    }

    void ParseMusicKitCollection(
        const char* json, InventorySnapshotSelection& selection) {
        selection.musicKitItemCount = 0;
        selection.musicKitCollectionHash = 14695981039346656037ull;
        if (!json) return;
        const char* items = strstr(json, "\"items\":[");
        if (!items) return;
        const char* itemsEnd = strstr(items, "],\"loadout\"");
        if (!itemsEnd) itemsEnd = json + strlen(json);

        constexpr char localIdToken[] = "\"local_id\":";
        const char* cursor = items;
        while (selection.musicKitItemCount <
                kMaxPublishedMusicKitItems &&
            (cursor = strstr(cursor, localIdToken)) != nullptr &&
            cursor < itemsEnd) {
            const char* itemStart = cursor;
            while (itemStart > items && *itemStart != '{') --itemStart;
            const char* itemEnd = strchr(cursor, '}');
            if (*itemStart != '{' || !itemEnd || itemEnd > itemsEnd) break;

            uint64_t localId = 0;
            uint64_t type = 0;
            uint64_t definition = 0;
            const char* typeField = strstr(itemStart, "\"type\":");
            const char* definitionField = strstr(
                itemStart, "\"definition_index\":");
            if (typeField && typeField < itemEnd && definitionField &&
                definitionField < itemEnd &&
                ReadJsonUnsigned(cursor, "local_id", localId) &&
                ReadJsonUnsigned(typeField, "type", type) && type == 0 &&
                ReadJsonUnsigned(definitionField, "definition_index",
                    definition) && definition > 0 &&
                definition <= 0xFFFF) {
                auto& item = selection.musicKitItems[
                    selection.musicKitItemCount++];
                item.localId = localId;
                item.musicKitId = static_cast<int>(definition);
                const char* field = strstr(itemStart, "\"stattrak\":");
                if (field && field < itemEnd)
                    (void)ReadJsonBool(field, "stattrak", item.statTrak);
                uint64_t value = 0;
                field = strstr(itemStart, "\"stattrak_count\":");
                if (field && field < itemEnd && ReadJsonUnsigned(
                        field, "stattrak_count", value))
                    item.statTrakCount = static_cast<int>(value);

                const uint64_t parts[] = {
                    item.localId,
                    static_cast<uint64_t>(item.musicKitId),
                    static_cast<uint64_t>(item.statTrak),
                    static_cast<uint64_t>(item.statTrakCount)
                };
                for (const uint64_t part : parts) {
                    selection.musicKitCollectionHash ^= part;
                    selection.musicKitCollectionHash *= 1099511628211ull;
                }
            }
            cursor = itemEnd + 1;
        }
    }

    void ParseKnifeCollection(
        const char* json, InventorySnapshotSelection& selection) {
        selection.knifeItemCount = 0;
        selection.knifeCollectionHash = 14695981039346656037ull;
        if (!json) return;
        const char* items = strstr(json, "\"items\":[");
        if (!items) return;
        const char* itemsEnd = strstr(items, "],\"loadout\"");
        if (!itemsEnd) itemsEnd = json + strlen(json);

        constexpr char localIdToken[] = "\"local_id\":";
        const char* cursor = items;
        while (selection.knifeItemCount < kMaxPublishedKnifeItems &&
            (cursor = strstr(cursor, localIdToken)) != nullptr &&
            cursor < itemsEnd) {
            const char* itemStart = cursor;
            while (itemStart > items && *itemStart != '{') --itemStart;
            const char* itemEnd = strchr(cursor, '}');
            if (*itemStart != '{' || !itemEnd || itemEnd > itemsEnd) break;

            uint64_t localId = 0;
            uint64_t type = 0;
            uint64_t definition = 0;
            const char* typeField = strstr(itemStart, "\"type\":");
            const char* definitionField = strstr(
                itemStart, "\"definition_index\":");
            if (typeField && typeField < itemEnd && definitionField &&
                definitionField < itemEnd &&
                ReadJsonUnsigned(cursor, "local_id", localId) &&
                ReadJsonUnsigned(typeField, "type", type) && type == 2 &&
                ReadJsonUnsigned(definitionField, "definition_index",
                    definition) && definition > 0 &&
                definition <= 0xFFFF) {
                auto& item = selection.knifeItems[
                    selection.knifeItemCount++];
                item.localId = localId;
                item.definitionIndex = static_cast<int>(definition);
                uint64_t value = 0;
                const char* field = strstr(itemStart, "\"paint_kit\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "paint_kit", value))
                    item.paintKit = static_cast<int>(value);
                value = 0;
                field = strstr(itemStart, "\"seed\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "seed", value))
                    item.seed = static_cast<int>(value);
                field = strstr(itemStart, "\"wear\":");
                if (field && field < itemEnd)
                    (void)ReadJsonFloat(field, "wear", item.wear);
                field = strstr(itemStart, "\"stattrak\":");
                if (field && field < itemEnd)
                    (void)ReadJsonBool(field, "stattrak", item.statTrak);
                value = 0;
                field = strstr(itemStart, "\"stattrak_count\":");
                if (field && field < itemEnd && ReadJsonUnsigned(
                        field, "stattrak_count", value))
                    item.statTrakCount = static_cast<int>(value);

                const uint64_t parts[] = {
                    item.localId,
                    static_cast<uint64_t>(item.definitionIndex),
                    static_cast<uint64_t>(item.paintKit),
                    static_cast<uint64_t>(item.seed),
                    static_cast<uint64_t>(item.statTrak),
                    static_cast<uint64_t>(item.statTrakCount)
                };
                for (const uint64_t part : parts) {
                    selection.knifeCollectionHash ^= part;
                    selection.knifeCollectionHash *= 1099511628211ull;
                }
                uint32_t wearBits = 0;
                CopyMemory(&wearBits, &item.wear, sizeof(wearBits));
                selection.knifeCollectionHash ^= wearBits;
                selection.knifeCollectionHash *= 1099511628211ull;
            }
            cursor = itemEnd + 1;
        }
    }

    void ParseWeaponSkinCollection(
        const char* json, InventorySnapshotSelection& selection) {
        selection.weaponSkinItemCount = 0;
        selection.weaponSkinCollectionHash = 14695981039346656037ull;
        if (!json) return;
        const char* items = strstr(json, "\"items\":[");
        if (!items) return;
        const char* itemsEnd = strstr(items, "],\"loadout\"");
        if (!itemsEnd) itemsEnd = json + strlen(json);

        constexpr char localIdToken[] = "\"local_id\":";
        const char* cursor = items;
        while (selection.weaponSkinItemCount <
                kMaxPublishedWeaponSkinItems &&
            (cursor = strstr(cursor, localIdToken)) != nullptr &&
            cursor < itemsEnd) {
            const char* itemStart = cursor;
            while (itemStart > items && *itemStart != '{') --itemStart;
            const char* itemEnd = strchr(cursor, '}');
            if (*itemStart != '{' || !itemEnd || itemEnd > itemsEnd) break;

            uint64_t localId = 0;
            uint64_t type = 0;
            uint64_t definition = 0;
            const char* typeField = strstr(itemStart, "\"type\":");
            const char* definitionField = strstr(
                itemStart, "\"definition_index\":");
            if (typeField && typeField < itemEnd && definitionField &&
                definitionField < itemEnd &&
                ReadJsonUnsigned(cursor, "local_id", localId) &&
                ReadJsonUnsigned(typeField, "type", type) && type == 1 &&
                ReadJsonUnsigned(definitionField, "definition_index",
                    definition) && definition > 0 && definition <= 0xFFFF) {
                auto& item = selection.weaponSkinItems[
                    selection.weaponSkinItemCount++];
                item.localId = localId;
                item.definitionIndex = static_cast<int>(definition);
                uint64_t value = 0;
                const char* field = strstr(itemStart, "\"paint_kit\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "paint_kit", value))
                    item.paintKit = static_cast<int>(value);
                value = 0;
                field = strstr(itemStart, "\"seed\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "seed", value))
                    item.seed = static_cast<int>(value);
                field = strstr(itemStart, "\"wear\":");
                if (field && field < itemEnd)
                    (void)ReadJsonFloat(field, "wear", item.wear);
                field = strstr(itemStart, "\"stattrak\":");
                if (field && field < itemEnd)
                    (void)ReadJsonBool(field, "stattrak", item.statTrak);
                value = 0;
                field = strstr(itemStart, "\"stattrak_count\":");
                if (field && field < itemEnd && ReadJsonUnsigned(
                        field, "stattrak_count", value))
                    item.statTrakCount = static_cast<int>(value);
                value = 0;
                field = strstr(itemStart, "\"quality\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "quality", value))
                    item.quality = static_cast<uint8_t>(value & 0xF);
                value = 0;
                field = strstr(itemStart, "\"rarity_rank\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "rarity_rank", value))
                    item.rarity = static_cast<uint8_t>(value & 0xF);

                const uint64_t parts[] = {
                    item.localId,
                    static_cast<uint64_t>(item.definitionIndex),
                    static_cast<uint64_t>(item.paintKit),
                    static_cast<uint64_t>(item.seed),
                    static_cast<uint64_t>(item.statTrak),
                    static_cast<uint64_t>(item.statTrakCount),
                    static_cast<uint64_t>(item.quality),
                    static_cast<uint64_t>(item.rarity)
                };
                for (const uint64_t part : parts) {
                    selection.weaponSkinCollectionHash ^= part;
                    selection.weaponSkinCollectionHash *= 1099511628211ull;
                }
                uint32_t wearBits = 0;
                CopyMemory(&wearBits, &item.wear, sizeof(wearBits));
                selection.weaponSkinCollectionHash ^= wearBits;
                selection.weaponSkinCollectionHash *= 1099511628211ull;
            }
            cursor = itemEnd + 1;
        }
    }

    void ParseGloveCollection(
        const char* json, InventorySnapshotSelection& selection) {
        selection.gloveItemCount = 0;
        selection.gloveCollectionHash = 14695981039346656037ull;
        if (!json) return;
        const char* items = strstr(json, "\"items\":[");
        if (!items) return;
        const char* itemsEnd = strstr(items, "],\"loadout\"");
        if (!itemsEnd) itemsEnd = json + strlen(json);

        constexpr char localIdToken[] = "\"local_id\":";
        const char* cursor = items;
        while (selection.gloveItemCount < kMaxPublishedGloveItems &&
            (cursor = strstr(cursor, localIdToken)) != nullptr &&
            cursor < itemsEnd) {
            const char* itemStart = cursor;
            while (itemStart > items && *itemStart != '{') --itemStart;
            const char* itemEnd = strchr(cursor, '}');
            if (*itemStart != '{' || !itemEnd || itemEnd > itemsEnd) break;

            uint64_t localId = 0;
            uint64_t type = 0;
            uint64_t definition = 0;
            const char* typeField = strstr(itemStart, "\"type\":");
            const char* definitionField = strstr(
                itemStart, "\"definition_index\":");
            if (typeField && typeField < itemEnd && definitionField &&
                definitionField < itemEnd &&
                ReadJsonUnsigned(cursor, "local_id", localId) &&
                ReadJsonUnsigned(typeField, "type", type) && type == 3 &&
                ReadJsonUnsigned(definitionField, "definition_index",
                    definition) && definition > 0 && definition <= 0xFFFF) {
                auto& item = selection.gloveItems[
                    selection.gloveItemCount++];
                item.localId = localId;
                item.definitionIndex = static_cast<int>(definition);
                uint64_t value = 0;
                const char* field = strstr(itemStart, "\"paint_kit\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "paint_kit", value))
                    item.paintKit = static_cast<int>(value);
                value = 0;
                field = strstr(itemStart, "\"seed\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "seed", value))
                    item.seed = static_cast<int>(value);
                field = strstr(itemStart, "\"wear\":");
                if (field && field < itemEnd)
                    (void)ReadJsonFloat(field, "wear", item.wear);
                field = strstr(itemStart, "\"stattrak\":");
                if (field && field < itemEnd)
                    (void)ReadJsonBool(field, "stattrak", item.statTrak);
                value = 0;
                field = strstr(itemStart, "\"stattrak_count\":");
                if (field && field < itemEnd && ReadJsonUnsigned(
                        field, "stattrak_count", value))
                    item.statTrakCount = static_cast<int>(value);

                const uint64_t parts[] = {
                    item.localId,
                    static_cast<uint64_t>(item.definitionIndex),
                    static_cast<uint64_t>(item.paintKit),
                    static_cast<uint64_t>(item.seed),
                    static_cast<uint64_t>(item.statTrak),
                    static_cast<uint64_t>(item.statTrakCount)
                };
                for (const uint64_t part : parts) {
                    selection.gloveCollectionHash ^= part;
                    selection.gloveCollectionHash *= 1099511628211ull;
                }
                uint32_t wearBits = 0;
                CopyMemory(&wearBits, &item.wear, sizeof(wearBits));
                selection.gloveCollectionHash ^= wearBits;
                selection.gloveCollectionHash *= 1099511628211ull;
            }
            cursor = itemEnd + 1;
        }
    }

    void ParseMiscCollection(
        const char* json, InventorySnapshotSelection& selection) {
        selection.miscItemCount = 0;
        selection.miscCollectionHash = 14695981039346656037ull;
        if (!json) return;
        const char* items = strstr(json, "\"items\":[");
        if (!items) return;
        const char* itemsEnd = strstr(items, "],\"loadout\"");
        if (!itemsEnd) itemsEnd = json + strlen(json);

        constexpr char localIdToken[] = "\"local_id\":";
        const char* cursor = items;
        while (selection.miscItemCount < kMaxPublishedMiscItems &&
            (cursor = strstr(cursor, localIdToken)) != nullptr &&
            cursor < itemsEnd) {
            const char* itemStart = cursor;
            while (itemStart > items && *itemStart != '{') --itemStart;
            const char* itemEnd = strchr(cursor, '}');
            if (*itemStart != '{' || !itemEnd || itemEnd > itemsEnd) break;

            uint64_t localId = 0;
            uint64_t type = 0;
            uint64_t definition = 0;
            const char* typeField = strstr(itemStart, "\"type\":");
            const char* definitionField = strstr(
                itemStart, "\"definition_index\":");
            const bool supportedType = typeField && typeField < itemEnd &&
                ReadJsonUnsigned(typeField, "type", type) &&
                type >= 5 && type <= 8;
            if (supportedType && definitionField &&
                definitionField < itemEnd &&
                ReadJsonUnsigned(cursor, "local_id", localId) &&
                ReadJsonUnsigned(definitionField, "definition_index",
                    definition) && definition > 0 && definition <= 0xFFFF) {
                auto& item = selection.miscItems[
                    selection.miscItemCount++];
                item.localId = localId;
                item.itemType = static_cast<int>(type);
                item.definitionIndex = static_cast<int>(definition);

                uint64_t value = 0;
                const char* field = strstr(itemStart, "\"quality\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "quality", value))
                    item.quality = static_cast<uint8_t>(value & 0xF);

                value = 0;
                field = strstr(itemStart, "\"rarity_rank\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "rarity_rank", value))
                    item.rarity = static_cast<uint8_t>(value & 0xF);

                if (item.itemType == 8) {
                    field = strstr(itemStart, "\"paint_kit\":");
                    value = 0;
                    if (field && field < itemEnd &&
                        ReadJsonUnsigned(field, "paint_kit", value) &&
                        value > 0 && value <= 0xFFFFFFFFull) {
                        item.variantAttributeDefinition = 113;
                        item.variantAttributeValue =
                            static_cast<uint32_t>(value);
                    } else {
                        --selection.miscItemCount;
                        cursor = itemEnd + 1;
                        continue;
                    }
                }

                const uint64_t parts[] = {
                    item.localId,
                    static_cast<uint64_t>(item.itemType),
                    static_cast<uint64_t>(item.definitionIndex),
                    static_cast<uint64_t>(
                        item.variantAttributeDefinition),
                    static_cast<uint64_t>(item.variantAttributeValue),
                    static_cast<uint64_t>(item.quality),
                    static_cast<uint64_t>(item.rarity)
                };
                for (const uint64_t part : parts) {
                    selection.miscCollectionHash ^= part;
                    selection.miscCollectionHash *= 1099511628211ull;
                }
            }
            cursor = itemEnd + 1;
        }
    }

    const char* FindAgentModel(int definitionIndex) {
        for (const AgentModel& agent : kAgentModels) {
            if (agent.definitionIndex == definitionIndex)
                return agent.modelPath;
        }
        return nullptr;
    }

    struct MusicKitLoadoutProbe {
        bool found = false;
        int team = 0;
        int slot = -1;
        uint64_t itemId = 0;
        bool generated = false;
        uint64_t localId = 0;
    };

    const KnifeModel* FindKnifeModel(int definitionIndex) {
        for (const KnifeModel& knife : kKnifeModels) {
            if (knife.definitionIndex == definitionIndex)
                return &knife;
        }
        return nullptr;
    }

    bool IsReadableMemory(const void* address, SIZE_T size) {
        if (!address || size == 0) return false;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(address, &info, sizeof(info)) ||
            info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD readable = PAGE_READONLY | PAGE_READWRITE |
            PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
            PAGE_EXECUTE_WRITECOPY;
        if ((info.Protect & readable) == 0) return false;
        const uintptr_t start = reinterpret_cast<uintptr_t>(address);
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) +
            info.RegionSize;
        return start <= regionEnd && size <= regionEnd - start;
    }

    template <typename T>
    bool SafeRead(uintptr_t address, T& value) {
        if (!IsReadableMemory(reinterpret_cast<const void*>(address), sizeof(T)))
            return false;
        CopyMemory(&value, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    }

    bool IsWritableMemory(void* address, SIZE_T size) {
        if (!address || size == 0) return false;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(address, &info, sizeof(info)) ||
            info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((info.Protect & writable) == 0) return false;
        const uintptr_t start = reinterpret_cast<uintptr_t>(address);
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) +
            info.RegionSize;
        return start <= regionEnd && size <= regionEnd - start;
    }

    template <typename T>
    bool SafeWrite(uintptr_t address, const T& value) {
        if (!IsWritableMemory(reinterpret_cast<void*>(address), sizeof(T)))
            return false;
        CopyMemory(reinterpret_cast<void*>(address), &value, sizeof(T));
        return true;
    }

    template <typename Function>
    Function GetVirtualFunction(void* object, SIZE_T index) {
        uintptr_t vtable = 0;
        uintptr_t function = 0;
        if (!object || !SafeRead(reinterpret_cast<uintptr_t>(object), vtable) ||
            !vtable || !SafeRead(vtable + index * sizeof(uintptr_t), function) ||
            !function)
            return nullptr;
        return reinterpret_cast<Function>(function);
    }

    uint32_t MurmurHash2(const void* key, int length, uint32_t seed) {
        constexpr uint32_t multiplier = 0x5BD1E995u;
        constexpr int shift = 24;
        uint32_t hash = seed ^ static_cast<uint32_t>(length);
        const unsigned char* data = static_cast<const unsigned char*>(key);
        while (length >= 4) {
            uint32_t part = 0;
            CopyMemory(&part, data, sizeof(part));
            part *= multiplier;
            part ^= part >> shift;
            part *= multiplier;
            hash *= multiplier;
            hash ^= part;
            data += 4;
            length -= 4;
        }
        switch (length) {
        case 3:
            hash ^= static_cast<uint32_t>(data[2]) << 16;
            [[fallthrough]];
        case 2:
            hash ^= static_cast<uint32_t>(data[1]) << 8;
            [[fallthrough]];
        case 1:
            hash ^= data[0];
            hash *= multiplier;
            break;
        default:
            break;
        }
        hash ^= hash >> 13;
        hash *= multiplier;
        hash ^= hash >> 15;
        return hash;
    }

    StringToken MakeStringToken(const char* text) {
        constexpr uint32_t seed = 0x31415926u;
        char lowered[64]{};
        SIZE_T length = 0;
        if (!text) return {};
        while (text[length] && length + 1 < _countof(lowered)) {
            const char value = text[length];
            lowered[length] = value >= 'A' && value <= 'Z'
                ? static_cast<char>(value + ('a' - 'A')) : value;
            ++length;
        }
        return { MurmurHash2(lowered, static_cast<int>(length), seed) };
    }

    bool SafeReadString(uintptr_t address, char* output, SIZE_T capacity) {
        if (!address || !output || capacity < 2) return false;
        SIZE_T index = 0;
        for (; index + 1 < capacity; ++index) {
            char character = '\0';
            if (!SafeRead(address + index, character)) return false;
            output[index] = character;
            if (character == '\0') return index != 0;
        }
        output[capacity - 1] = '\0';
        return true;
    }

    bool ParseInventorySelection(
        const char* json, InventorySnapshotSelection& selection) {
        uint64_t value = 0;
        const char* pendingField = json
            ? strstr(json, "\"pending_reveal_item_ids\":[") : nullptr;
        if (pendingField) {
            const char* cursor = strchr(pendingField, '[');
            const char* end = cursor ? strchr(cursor, ']') : nullptr;
            if (cursor && end) {
                ++cursor;
                while (cursor < end && selection.pendingRevealItemCount <
                        kMaxPendingRevealItems) {
                    while (cursor < end && (*cursor == ' ' || *cursor == ','))
                        ++cursor;
                    if (cursor >= end || *cursor < '0' || *cursor > '9') break;
                    char* valueEnd = nullptr;
                    const unsigned long long parsed = strtoull(
                        cursor, &valueEnd, 10);
                    if (!valueEnd || valueEnd == cursor || valueEnd > end)
                        break;
                    if (parsed != 0)
                        selection.pendingRevealLocalIds[
                            selection.pendingRevealItemCount++] = parsed;
                    cursor = valueEnd;
                }
            }
        }
        if (selection.pendingRevealItemCount == 0 &&
            ReadJsonUnsigned(json, "pending_reveal_item_id", value) &&
            value != 0) {
            selection.pendingRevealLocalIds[0] = value;
            selection.pendingRevealItemCount = 1;
        }
        selection.pendingRevealLocalId = selection.pendingRevealItemCount > 0
            ? selection.pendingRevealLocalIds[0] : 0;
        ParseMusicKitCollection(json, selection);
        ParseWeaponSkinCollection(json, selection);
        ParseKnifeCollection(json, selection);
        ParseGloveCollection(json, selection);
        ParseMiscCollection(json, selection);
        for (int pendingIndex = 0;
            pendingIndex < selection.pendingRevealItemCount; ++pendingIndex) {
            const uint64_t pendingId =
                selection.pendingRevealLocalIds[pendingIndex];
            for (int index = 0; index < selection.musicKitItemCount; ++index) {
                if (selection.musicKitItems[index].localId != pendingId)
                    continue;
                selection.musicKitCollectionHash ^= pendingId;
                selection.musicKitCollectionHash *= 1099511628211ull;
                break;
            }
            for (int index = 0; index < selection.knifeItemCount; ++index) {
                if (selection.knifeItems[index].localId != pendingId)
                    continue;
                selection.knifeCollectionHash ^= pendingId;
                selection.knifeCollectionHash *= 1099511628211ull;
                break;
            }
            for (int index = 0; index < selection.gloveItemCount; ++index) {
                if (selection.gloveItems[index].localId != pendingId)
                    continue;
                selection.gloveCollectionHash ^= pendingId;
                selection.gloveCollectionHash *= 1099511628211ull;
                break;
            }
            for (int index = 0; index < selection.miscItemCount; ++index) {
                if (selection.miscItems[index].localId != pendingId)
                    continue;
                selection.miscCollectionHash ^= pendingId;
                selection.miscCollectionHash *= 1099511628211ull;
                break;
            }
        }
        (void)ReadJsonBool(json, "enabled", selection.enabled);
        if (ReadJsonUnsigned(json, "music_kit_apply_revision", value))
            selection.musicKitApplyRevision = value;
        (void)ReadJsonBool(json, "apply_knives_to_controlled_bots",
            selection.applyKnivesToControlledBots);
        if (ReadJsonUnsigned(json, "music_kit", value))
            selection.musicKit = value;
        if (ReadJsonUnsigned(json, "terrorist_agent", value))
            selection.terroristAgent = value;
        if (ReadJsonUnsigned(json, "counter_terrorist_agent", value))
            selection.counterTerroristAgent = value;
        if (ReadJsonUnsigned(json, "terrorist_knife", value))
            selection.terroristKnife = value;
        if (ReadJsonUnsigned(json, "counter_terrorist_knife", value))
            selection.counterTerroristKnife = value;
        if (ReadJsonUnsigned(json, "terrorist_gloves", value))
            selection.terroristGloves = value;
        if (ReadJsonUnsigned(json, "counter_terrorist_gloves", value))
            selection.counterTerroristGloves = value;
        if (ReadJsonUnsigned(json, "entity_list", value))
            selection.entityListOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "local_player_controller", value))
            selection.localPlayerControllerOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "local_player_pawn", value))
            selection.localPlayerPawnOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "inventory_services", value))
            selection.inventoryServicesOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "service_music_id", value))
            selection.serviceMusicIdOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "controller_music_kit_id", value))
            selection.controllerMusicKitIdOffset =
                static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "controller_music_kit_mvps", value))
            selection.controllerMusicKitMvpsOffset =
                static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "player_pawn_handle", value))
            selection.playerPawnHandleOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "controlling_bot", value))
            selection.controllingBotOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "has_female_voice", value))
            selection.hasFemaleVoiceOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "team_number", value))
            selection.teamNumberOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "life_state", value))
            selection.lifeStateOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "last_spawn_time_index", value))
            selection.lastSpawnTimeIndexOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "game_scene_node", value))
            selection.gameSceneNodeOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "model_state", value))
            selection.modelStateOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "model_name", value))
            selection.modelNameOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "owner_entity", value))
            selection.ownerEntityOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "hud_model_arms", value))
            selection.hudModelArmsOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "my_wearables", value))
            selection.myWearablesOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "scene_node_owner", value))
            selection.sceneNodeOwnerOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "scene_node_child", value))
            selection.sceneNodeChildOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "scene_node_next_sibling", value))
            selection.sceneNodeNextSiblingOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "weapon_services", value))
            selection.weaponServicesOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "active_weapon", value))
            selection.activeWeaponOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "subclass_id", value))
            selection.subclassIdOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "viewmodel_attachment", value))
            selection.viewmodelAttachmentOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "attribute_manager", value))
            selection.attributeManagerOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "item", value))
            selection.itemOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "item_definition_index", value))
            selection.itemDefinitionIndexOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "entity_quality", value))
            selection.entityQualityOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "fallback_paint_kit", value))
            selection.fallbackPaintKitOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "fallback_seed", value))
            selection.fallbackSeedOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "fallback_wear", value))
            selection.fallbackWearOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "fallback_stattrak", value))
            selection.fallbackStatTrakOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "item_id", value))
            selection.itemIdOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "item_id_high", value))
            selection.itemIdHighOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "item_id_low", value))
            selection.itemIdLowOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "account_id", value))
            selection.accountIdOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "initialized", value))
            selection.initializedOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "need_to_reapply_gloves", value))
            selection.needToReApplyGlovesOffset =
                static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "econ_gloves", value))
            selection.econGlovesOffset = static_cast<uintptr_t>(value);
        if (ReadJsonUnsigned(json, "econ_gloves_changed", value))
            selection.econGlovesChangedOffset = static_cast<uintptr_t>(value);

        if (selection.terroristAgent != 0)
            (void)FindAgentDefinition(json, selection.terroristAgent,
                selection.terroristDefinition);
        if (selection.counterTerroristAgent != 0)
            (void)FindAgentDefinition(json, selection.counterTerroristAgent,
                selection.counterTerroristDefinition);
        if (selection.musicKit != 0)
            (void)FindInventoryItemDetails(json, selection.musicKit, 0,
                selection.musicKitDefinition);
        if (selection.terroristKnife != 0)
            (void)FindInventoryItemDetails(json, selection.terroristKnife, 2,
                selection.terroristKnifeDefinition,
                &selection.terroristKnifePaintKit,
                &selection.terroristKnifeSeed,
                &selection.terroristKnifeWear,
                &selection.terroristKnifeStatTrak,
                &selection.terroristKnifeStatTrakCount);
        if (selection.counterTerroristKnife != 0)
            (void)FindInventoryItemDetails(json,
                selection.counterTerroristKnife, 2,
                selection.counterTerroristKnifeDefinition,
                &selection.counterTerroristKnifePaintKit,
                &selection.counterTerroristKnifeSeed,
                &selection.counterTerroristKnifeWear,
                &selection.counterTerroristKnifeStatTrak,
                &selection.counterTerroristKnifeStatTrakCount);
        if (selection.terroristGloves != 0)
            (void)FindInventoryItemDetails(json, selection.terroristGloves, 3,
                selection.terroristGlovesDefinition,
                &selection.terroristGlovesPaintKit,
                &selection.terroristGlovesSeed,
                &selection.terroristGlovesWear,
                &selection.terroristGlovesStatTrak,
                &selection.terroristGlovesStatTrakCount);
        if (selection.counterTerroristGloves != 0)
            (void)FindInventoryItemDetails(json,
                selection.counterTerroristGloves, 3,
                selection.counterTerroristGlovesDefinition,
                &selection.counterTerroristGlovesPaintKit,
                &selection.counterTerroristGlovesSeed,
                &selection.counterTerroristGlovesWear,
                &selection.counterTerroristGlovesStatTrak,
                &selection.counterTerroristGlovesStatTrakCount);
        return selection.localPlayerPawnOffset != 0 &&
            selection.teamNumberOffset != 0;
    }

    bool IsPendingRevealItem(
        const InventorySnapshotSelection& selection, uint64_t localId) {
        for (int index = 0; index < selection.pendingRevealItemCount; ++index) {
            if (selection.pendingRevealLocalIds[index] == localId) return true;
        }
        return false;
    }

    bool ProbeMusicKitLoadout(
        const InventorySnapshotSelection& selection, uint64_t targetLocalId,
        uint64_t targetItemId, MusicKitLoadoutProbe& probe) {
        probe = {};
        if (!selection.itemIdOffset ||
            (targetLocalId == 0 && targetItemId == 0))
            return false;

        constexpr int teams[] = {
            0, kTerroristTeam, kCounterTerroristTeam
        };
        for (const int team : teams) {
            for (int slot = 0; slot <= 63; ++slot) {
                InventorySocacheLoadoutSelection nativeSelection{};
                if (!ReadInventorySocacheLoadoutSelection(team, slot,
                        selection.itemIdOffset, nativeSelection) ||
                    nativeSelection.itemId == 0)
                    continue;
                const bool matchesItemId = targetItemId != 0 &&
                    nativeSelection.itemId == targetItemId;
                const bool matchesLocalId = targetLocalId != 0 &&
                    nativeSelection.generated &&
                    nativeSelection.localId == targetLocalId;
                if (!matchesItemId && !matchesLocalId) continue;
                probe.found = true;
                probe.team = team;
                probe.slot = slot;
                probe.itemId = nativeSelection.itemId;
                probe.generated = nativeSelection.generated;
                probe.localId = nativeSelection.localId;
                return true;
            }
        }
        return false;
    }

    void LogMusicKitDiagnostics(
        const InventorySnapshotSelection& selection) {
        uint64_t generatedItemId = 0;
        const bool generatedResolved = selection.musicKit != 0 &&
            ResolveInventorySocacheGeneratedItemId(
                selection.musicKit, generatedItemId);

        MusicKitLoadoutProbe probe{};
        const bool nativeFound = ProbeMusicKitLoadout(selection,
            selection.musicKit, generatedItemId, probe);

        uint64_t fingerprintParts[] = {
            selection.enabled ? 1ull : 0ull,
            selection.musicKit,
            static_cast<uint64_t>(selection.musicKitDefinition),
            generatedResolved ? generatedItemId : 0ull,
            nativeFound ? static_cast<uint64_t>(probe.team) : 0ull,
            nativeFound ? static_cast<uint64_t>(probe.slot + 1) : 0ull,
            nativeFound ? probe.itemId : 0ull,
            nativeFound && probe.generated ? 1ull : 0ull,
            nativeFound ? probe.localId : 0ull,
            selection.pendingRevealLocalId,
            static_cast<uint64_t>(selection.pendingRevealItemCount)
        };
        static uint64_t lastFingerprint = 0;
        const uint64_t fingerprint = BridgeCompatibility::HashBytes(
            fingerprintParts, sizeof(fingerprintParts));
        if (fingerprint == lastFingerprint) return;
        lastFingerprint = fingerprint;

        char message[320]{};
        StringCchPrintfA(message, _countof(message),
            "Music Kit diag: enabled=%s selected=%llu def=%d "
            "socache=%s item=%llu native=%s team=%d slot=%d item=%llu "
            "generated=%s local=%llu panorama=%llu pending=%llu count=%d.",
            selection.enabled ? "yes" : "no",
            static_cast<unsigned long long>(selection.musicKit),
            selection.musicKitDefinition,
            generatedResolved ? "yes" : "no",
            static_cast<unsigned long long>(generatedItemId),
            nativeFound ? "yes" : "no",
            probe.team,
            probe.slot,
            static_cast<unsigned long long>(probe.itemId),
            probe.generated ? "yes" : "no",
            static_cast<unsigned long long>(probe.localId),
            static_cast<unsigned long long>(selection.musicKit),
            static_cast<unsigned long long>(selection.pendingRevealLocalId),
            selection.pendingRevealItemCount);
        AppendLog(message);
    }

    void PublishInventorySelection(const char* json) {
        InventorySnapshotSelection selection;
        const bool valid = ParseInventorySelection(json, selection);
        AcquireSRWLockExclusive(&g_selectionLock);
        g_latestSelection = selection;
        g_hasLatestSelection = valid;
        ReleaseSRWLockExclusive(&g_selectionLock);

        static bool lastEnabled = false;
        static bool lastApplyKnivesToControlledBots = false;
        static int lastTerroristKnifeDefinition = -1;
        static int lastCounterTerroristKnifeDefinition = -1;
        if (selection.enabled != lastEnabled ||
            selection.applyKnivesToControlledBots !=
                lastApplyKnivesToControlledBots ||
            selection.terroristKnifeDefinition !=
                lastTerroristKnifeDefinition ||
            selection.counterTerroristKnifeDefinition !=
                lastCounterTerroristKnifeDefinition) {
            char message[256]{};
            StringCchPrintfA(message, _countof(message),
                "Knife selection: enabled=%s controlled_bots=%s "
                "t=%llu(def=%d,mapped=%s) "
                "ct=%llu(def=%d,mapped=%s).",
                selection.enabled ? "yes" : "no",
                selection.applyKnivesToControlledBots ? "yes" : "no",
                static_cast<unsigned long long>(selection.terroristKnife),
                selection.terroristKnifeDefinition,
                FindKnifeModel(selection.terroristKnifeDefinition)
                    ? "yes" : "no",
                static_cast<unsigned long long>(
                    selection.counterTerroristKnife),
                selection.counterTerroristKnifeDefinition,
                FindKnifeModel(selection.counterTerroristKnifeDefinition)
                    ? "yes" : "no");
            AppendLog(message);
            if (selection.enabled &&
                (selection.terroristKnifeDefinition != 0 ||
                    selection.counterTerroristKnifeDefinition != 0))
                LogKnifeDryRun(json);
            lastEnabled = selection.enabled;
            lastApplyKnivesToControlledBots =
                selection.applyKnivesToControlledBots;
            lastTerroristKnifeDefinition =
                selection.terroristKnifeDefinition;
            lastCounterTerroristKnifeDefinition =
                selection.counterTerroristKnifeDefinition;
        }

        LogMusicKitDiagnostics(selection);

        static uint64_t lastOffsetsHash = 0;
        const uintptr_t offsets[] = {
            selection.entityListOffset,
            selection.localPlayerControllerOffset,
            selection.localPlayerPawnOffset,
            selection.inventoryServicesOffset,
            selection.serviceMusicIdOffset,
            selection.controllerMusicKitIdOffset,
            selection.playerPawnHandleOffset,
            selection.controllingBotOffset,
            selection.teamNumberOffset,
            selection.lifeStateOffset,
            selection.lastSpawnTimeIndexOffset,
            selection.gameSceneNodeOffset,
            selection.modelStateOffset,
            selection.modelNameOffset,
            selection.ownerEntityOffset,
            selection.hudModelArmsOffset,
            selection.myWearablesOffset,
            selection.sceneNodeOwnerOffset,
            selection.sceneNodeChildOffset,
            selection.sceneNodeNextSiblingOffset,
            selection.weaponServicesOffset,
            selection.activeWeaponOffset,
            selection.subclassIdOffset,
            selection.viewmodelAttachmentOffset,
            selection.attributeManagerOffset,
            selection.itemOffset,
            selection.itemDefinitionIndexOffset,
            selection.entityQualityOffset,
            selection.fallbackPaintKitOffset,
            selection.fallbackSeedOffset,
            selection.fallbackWearOffset,
            selection.fallbackStatTrakOffset,
            selection.hasFemaleVoiceOffset,
            selection.itemIdOffset,
            selection.itemIdHighOffset,
            selection.itemIdLowOffset,
            selection.accountIdOffset,
            selection.initializedOffset,
            selection.needToReApplyGlovesOffset,
            selection.econGlovesOffset,
            selection.econGlovesChangedOffset
        };
        const uint64_t offsetsHash = BridgeCompatibility::HashBytes(
            offsets, sizeof(offsets));
        if (offsetsHash != lastOffsetsHash) {
            char message[160]{};
            StringCchPrintfA(message, _countof(message),
                "Compatibility offsets: valid=%s fnv1a=0x%016llX.",
                valid ? "yes" : "no",
                static_cast<unsigned long long>(offsetsHash));
            AppendLog(message);
            lastOffsetsHash = offsetsHash;
        }
    }

    void DisablePublishedInventorySelection() {
        AcquireSRWLockExclusive(&g_selectionLock);
        if (g_hasLatestSelection) g_latestSelection.enabled = false;
        ReleaseSRWLockExclusive(&g_selectionLock);
    }

    bool ReadPublishedInventorySelection(
        InventorySnapshotSelection& selection) {
        AcquireSRWLockShared(&g_selectionLock);
        const bool available = g_hasLatestSelection;
        if (available) selection = g_latestSelection;
        ReleaseSRWLockShared(&g_selectionLock);
        return available;
    }

    uintptr_t ReadEntityByIndex(
        uintptr_t entityList, int index, uintptr_t stride) {
        if (!entityList || index <= 0 || index >= 0x8000 ||
            (stride != 0x70 && stride != 0x78))
            return 0;
        uintptr_t chunk = 0;
        if (!SafeRead(entityList +
            8 * static_cast<uintptr_t>((index & 0x7FFF) >> 9) + 16,
            chunk) || !chunk)
            return 0;
        uintptr_t entity = 0;
        (void)SafeRead(chunk + stride *
            static_cast<uintptr_t>(index & 0x1FF), entity);
        return entity;
    }

    bool ResolveAccountPawn(
        HMODULE client, const InventorySnapshotSelection& selection,
        uintptr_t& controller, uintptr_t& pawn, uintptr_t& stride) {
        controller = 0;
        pawn = 0;
        stride = 0;
        if (!client || !selection.entityListOffset ||
            !selection.localPlayerControllerOffset ||
            !selection.playerPawnHandleOffset)
            return false;

        const uintptr_t clientBase = reinterpret_cast<uintptr_t>(client);
        uintptr_t entityList = 0;
        if (!SafeRead(clientBase + selection.entityListOffset, entityList) ||
            !entityList ||
            !SafeRead(clientBase + selection.localPlayerControllerOffset,
                controller) || !controller)
            return false;

        constexpr uintptr_t kCandidateStrides[] = { 0x70, 0x78 };
        for (const uintptr_t candidateStride : kCandidateStrides) {
            for (int index = 1; index <= 64; ++index) {
                if (ReadEntityByIndex(entityList, index, candidateStride) ==
                    controller) {
                    stride = candidateStride;
                    break;
                }
            }
            if (stride) break;
        }
        if (!stride) return false;

        uint32_t pawnHandle = 0;
        if (!SafeRead(controller + selection.playerPawnHandleOffset,
            pawnHandle) || !pawnHandle || pawnHandle == 0xFFFFFFFF)
            return false;
        pawn = ReadEntityByIndex(
            entityList, static_cast<int>(pawnHandle & 0x7FFF), stride);
        return pawn != 0;
    }

    bool ResolvePlayableKnifePawn(
        HMODULE client, const InventorySnapshotSelection& selection,
        uintptr_t& controller, uintptr_t& pawn, uintptr_t& stride,
        bool& usedActivePawn) {
        usedActivePawn = false;
        const bool accountResolved = ResolveAccountPawn(
            client, selection, controller, pawn, stride);

        bool controllingBot = false;
        if (controller && selection.controllingBotOffset)
            (void)SafeRead(controller + selection.controllingBotOffset,
                controllingBot);

        if (!controllingBot)
            return accountResolved;
        if (!selection.applyKnivesToControlledBots)
            return false;

        uintptr_t activePawn = 0;
        if (client && selection.localPlayerPawnOffset)
            (void)SafeRead(reinterpret_cast<uintptr_t>(client) +
                selection.localPlayerPawnOffset, activePawn);

        // Only m_bControllingBot authorizes the global-pawn fallback. This
        // avoids touching arbitrary spectator targets while the account dies.
        if (activePawn && stride) {
            usedActivePawn = true;
            pawn = activePawn;
            return true;
        }
        return false;
    }

    void LogAgentDryRun(const char* json) {
        InventorySnapshotSelection selection;
        if (!ParseInventorySelection(json, selection)) {
            AppendLog("Agent dry-run: snapshot sin offsets runtime validos.");
            return;
        }

        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t activePawn = 0;
        if (!client || !SafeRead(
            reinterpret_cast<uintptr_t>(client) +
                selection.localPlayerPawnOffset,
            activePawn)) {
            AppendLog("Agent dry-run: pawn local no disponible.");
            return;
        }

        uintptr_t accountController = 0;
        uintptr_t accountPawn = 0;
        uintptr_t entityStride = 0;
        const bool accountResolved = ResolveAccountPawn(
            client, selection, accountController, accountPawn, entityStride);
        const uintptr_t pawn = accountResolved ? accountPawn : activePawn;
        if (!pawn) {
            AppendLog("Agent dry-run: pawn de cuenta no disponible.");
            return;
        }

        int team = 0;
        uint8_t lifeState = 0xFF;
        float spawnTimeIndex = 0.0f;
        if (!SafeRead(pawn + selection.teamNumberOffset, team)) {
            AppendLog("Agent dry-run: no se pudo leer el equipo local.");
            return;
        }
        if (selection.lastSpawnTimeIndexOffset != 0)
            (void)SafeRead(
                pawn + selection.lastSpawnTimeIndexOffset, spawnTimeIndex);
        if (selection.lifeStateOffset != 0)
            (void)SafeRead(pawn + selection.lifeStateOffset, lifeState);

        const int definitionIndex = team == kTerroristTeam
            ? selection.terroristDefinition
            : team == kCounterTerroristTeam
                ? selection.counterTerroristDefinition : 0;
        const char* model = FindAgentModel(definitionIndex);
        char currentModel[260]{};
        uintptr_t sceneNode = 0;
        uintptr_t modelName = 0;
        if (selection.gameSceneNodeOffset != 0 &&
            selection.modelStateOffset != 0 && selection.modelNameOffset != 0 &&
            SafeRead(pawn + selection.gameSceneNodeOffset, sceneNode) && sceneNode &&
            SafeRead(sceneNode + selection.modelStateOffset +
                selection.modelNameOffset, modelName) && modelName) {
            (void)SafeReadString(modelName, currentModel, _countof(currentModel));
        }

        char message[960]{};
        StringCchPrintfA(message, _countof(message),
            "Agent dry-run: enabled=%s source=%s controller=0x%llX "
            "active=0x%llX pawn=0x%llX stride=0x%llX team=%d life=%u "
            "spawn=%.3f def=%d current=%s target=%s",
            selection.enabled ? "true" : "false",
            accountResolved ? "account" : "active-fallback",
            static_cast<unsigned long long>(accountController),
            static_cast<unsigned long long>(activePawn),
            static_cast<unsigned long long>(pawn),
            static_cast<unsigned long long>(entityStride), team,
            static_cast<unsigned>(lifeState), spawnTimeIndex, definitionIndex,
            currentModel[0] ? currentModel : "<unreadable>",
            model ? model : "<none>");
        static char lastMessage[960]{};
        if (strcmp(message, lastMessage) != 0) {
            AppendLog(message);
            StringCchCopyA(lastMessage, _countof(lastMessage), message);
        }
    }

    uint64_t HashText(const char* text) {
        constexpr uint64_t kOffsetBasis = 14695981039346656037ull;
        constexpr uint64_t kPrime = 1099511628211ull;
        uint64_t hash = kOffsetBasis;
        if (!text) return hash;
        while (*text) {
            hash ^= static_cast<unsigned char>(*text++);
            hash *= kPrime;
        }
        return hash;
    }

    bool WriteExact(HANDLE pipe, const void* source, DWORD size) {
        const auto* bytes = static_cast<const unsigned char*>(source);
        DWORD completed = 0;
        while (completed < size) {
            DWORD written = 0;
            if (!WriteFile(pipe, bytes + completed, size - completed,
                &written, nullptr) || written == 0)
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
            if (!ReadFile(pipe, bytes + completed, size - completed,
                &read, nullptr) || read == 0)
                return false;
            completed += read;
        }
        return true;
    }

    using CreateInterfaceFn = void* (*)(const char*, int*);

    void* GetInterfaceInstance(
        const wchar_t* moduleName, const char* interfaceName) {
        HMODULE module = GetModuleHandleW(moduleName);
        if (!module) return nullptr;
        auto factory = reinterpret_cast<CreateInterfaceFn>(
            GetProcAddress(module, "CreateInterface"));
        if (!factory) return nullptr;
        int result = -1;
        return factory(interfaceName, &result);
    }

    bool ProbeInterface(
        const wchar_t* moduleName, const char* interfaceName,
        const char* successText, const char* failureText,
        void** instanceOut = nullptr) {
        void* instance = GetInterfaceInstance(moduleName, interfaceName);
        if (instanceOut) *instanceOut = instance;
        AppendLog(instance ? successText : failureText);
        return instance != nullptr;
    }

    bool IsExecutableAddress(const void* address) {
        MEMORY_BASIC_INFORMATION info{};
        if (!address || !VirtualQuery(address, &info, sizeof(info)) ||
            info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (info.Protect & executable) != 0;
    }

    void __fastcall FrameStageDiagnosticHook(void* self, int stage) {
        FrameStageFn original = g_originalFrameStage;
        if (original) original(self, stage);
        if (stage == kAgentFrameStage) {
            InterlockedIncrement(&g_frameStageSixCalls);
            RunAgentModelControl();
            RunMusicKitCollectionControl();
            RunMusicKitRuntimeControl();
            RunWeaponSkinCollectionControl();
            RunInventoryCollectionControl();
            RunGloveCollectionControl();
            RunMiscCollectionControl();
            RunPendingRevealAcknowledgementControl();
            RunNativeInventoryLoadoutControl();
            RunKnifeModelControl();
            RunGloveModelControl();
            RunPanoramaMountFrame();
        }
    }

    bool SetVtableEntry(void** slot, void* replacement, void** previous) {
        if (!slot || !replacement) return false;
        DWORD oldProtection = 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE,
            &oldProtection))
            return false;
        void* oldValue = InterlockedExchangePointer(
            reinterpret_cast<PVOID volatile*>(slot), replacement);
        DWORD ignored = 0;
        const BOOL restored = VirtualProtect(
            slot, sizeof(*slot), oldProtection, &ignored);
        if (previous) *previous = oldValue;
        return restored != FALSE;
    }

    void* AllocateExecutableNear(uintptr_t target, SIZE_T size) {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        const uintptr_t granularity = info.dwAllocationGranularity;
        constexpr uintptr_t kMaximumDistance = 0x70000000;
        const uintptr_t alignedTarget = target & ~(granularity - 1);
        for (uintptr_t distance = granularity;
            distance < kMaximumDistance; distance += granularity) {
            const uintptr_t candidates[] = {
                alignedTarget >= distance ? alignedTarget - distance : 0,
                alignedTarget + distance
            };
            for (const uintptr_t candidate : candidates) {
                if (!candidate) continue;
                const int64_t relative = static_cast<int64_t>(candidate) -
                    static_cast<int64_t>(target + 5);
                if (relative < INT32_MIN || relative > INT32_MAX) continue;
                void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate),
                    size, MEM_RESERVE | MEM_COMMIT,
                    PAGE_EXECUTE_READWRITE);
                if (memory) return memory;
            }
        }
        return nullptr;
    }

    void __fastcall RegisterGloveRenderDiagnosticHook(
        void* renderComponent, void* registration) {
        const uintptr_t entity = reinterpret_cast<uintptr_t>(renderComponent) -
            0x608;
        uintptr_t before = 0;
        uintptr_t after = 0;
        (void)SafeRead(entity, before);
        RegisterGloveRenderFn original = g_registerGloveRender;
        if (original) original(renderComponent, registration);
        (void)SafeRead(entity, after);
        InterlockedIncrement(&g_registerGloveRenderCalls);
        InterlockedExchange64(&g_registerGloveRenderEntity,
            static_cast<LONG64>(entity));
        InterlockedExchange64(&g_registerGloveRenderBefore,
            static_cast<LONG64>(before));
        InterlockedExchange64(&g_registerGloveRenderAfter,
            static_cast<LONG64>(after));
    }

    void InstallGloveRenderDiagnostic(uintptr_t entity) {
        if (!entity || g_registerGloveRenderSlot) return;
        uintptr_t vtable = 0;
        void* original = nullptr;
        if (!SafeRead(entity + 0x608, vtable) || !vtable ||
            !SafeRead(vtable + sizeof(void*), original) || !original)
            return;
        void* previous = nullptr;
        auto** slot = reinterpret_cast<void**>(vtable + sizeof(void*));
        if (!SetVtableEntry(slot,
                reinterpret_cast<void*>(&RegisterGloveRenderDiagnosticHook),
                &previous))
            return;
        g_registerGloveRender = reinterpret_cast<RegisterGloveRenderFn>(previous);
        g_registerGloveRenderSlot = slot;
    }

    void RemoveGloveRenderDiagnostic() {
        if (!g_registerGloveRenderSlot || !g_registerGloveRender) return;
        void* current = nullptr;
        if (SafeRead(reinterpret_cast<uintptr_t>(g_registerGloveRenderSlot),
                current) && current == reinterpret_cast<void*>(
                    &RegisterGloveRenderDiagnosticHook)) {
            (void)SetVtableEntry(g_registerGloveRenderSlot,
                reinterpret_cast<void*>(g_registerGloveRender), nullptr);
        }
        g_registerGloveRenderSlot = nullptr;
        g_registerGloveRender = nullptr;
        AppendLog("Glove render diagnostic: retirado.");
    }

    void __fastcall ProcessGloveModelDiagnosticHook(void* gloveControl) {
        const LONG spawnCallsBefore = InterlockedCompareExchange(
            &g_spawnGloveCalls, 0, 0);
        ProcessGloveModelFn original = g_processGloveModel;
        if (original) original(gloveControl);
        const LONG spawnCallsAfter = InterlockedCompareExchange(
            &g_spawnGloveCalls, 0, 0);
        if (spawnCallsAfter == spawnCallsBefore) return;
        const uintptr_t entity = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_spawnGloveLastEntity, 0, 0));
        uintptr_t vtable = 0;
        uintptr_t resolved = 0;
        (void)SafeRead(entity, vtable);
        const uint32_t handle = static_cast<uint32_t>(
            InterlockedCompareExchange(&g_spawnGloveLastHandle, 0, 0));
        if (g_resolveGloveEntity && handle != 0xFFFFFFFF) {
            uint32_t handleCopy = handle;
            __try {
                resolved = reinterpret_cast<uintptr_t>(
                    g_resolveGloveEntity(&handleCopy));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                resolved = 0;
            }
        }
        InterlockedIncrement(&g_processGloveCalls);
        InterlockedExchange64(&g_processGloveAfterVtable,
            static_cast<LONG64>(vtable));
        InterlockedExchange64(&g_processGloveAfterResolved,
            static_cast<LONG64>(resolved));
    }

    bool InstallGloveProcessDiagnostic(
        uintptr_t callsite, ProcessGloveModelFn original) {
        if (!callsite || !original || ((callsite + 1) & 3) != 0 ||
            g_processGloveCallsite)
            return false;
        void* relay = AllocateExecutableNear(callsite, 0x1000);
        if (!relay) return false;
        unsigned char relayCode[12] = { 0x48, 0xB8 };
        const uintptr_t hook = reinterpret_cast<uintptr_t>(
            &ProcessGloveModelDiagnosticHook);
        CopyMemory(relayCode + 2, &hook, sizeof(hook));
        relayCode[10] = 0xFF;
        relayCode[11] = 0xE0;
        CopyMemory(relay, relayCode, sizeof(relayCode));
        FlushInstructionCache(GetCurrentProcess(), relay, sizeof(relayCode));
        const int64_t relative64 = reinterpret_cast<uintptr_t>(relay) -
            (callsite + 5);
        if (relative64 < INT32_MIN || relative64 > INT32_MAX) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        int32_t originalDisplacement = 0;
        if (!SafeRead(callsite + 1, originalDisplacement)) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        DWORD oldProtection = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(callsite + 1),
                sizeof(int32_t), PAGE_EXECUTE_READWRITE, &oldProtection)) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        g_processGloveModel = original;
        InterlockedExchange(reinterpret_cast<volatile LONG*>(callsite + 1),
            static_cast<LONG>(relative64));
        FlushInstructionCache(GetCurrentProcess(),
            reinterpret_cast<void*>(callsite), 5);
        DWORD ignored = 0;
        (void)VirtualProtect(reinterpret_cast<void*>(callsite + 1),
            sizeof(int32_t), oldProtection, &ignored);
        g_processGloveCallsite = callsite;
        g_processGloveRelay = relay;
        g_processGloveOriginalDisplacement = originalDisplacement;
        AppendLog("Glove process diagnostic: instalado.");
        return true;
    }

    void RemoveGloveProcessDiagnostic() {
        if (!g_processGloveCallsite) return;
        DWORD oldProtection = 0;
        if (VirtualProtect(reinterpret_cast<void*>(g_processGloveCallsite + 1),
                sizeof(int32_t), PAGE_EXECUTE_READWRITE, &oldProtection)) {
            InterlockedExchange(reinterpret_cast<volatile LONG*>(
                    g_processGloveCallsite + 1),
                g_processGloveOriginalDisplacement);
            FlushInstructionCache(GetCurrentProcess(),
                reinterpret_cast<void*>(g_processGloveCallsite), 5);
            DWORD ignored = 0;
            (void)VirtualProtect(reinterpret_cast<void*>(
                    g_processGloveCallsite + 1), sizeof(int32_t),
                oldProtection, &ignored);
        }
        Sleep(20);
        if (g_processGloveRelay)
            VirtualFree(g_processGloveRelay, 0, MEM_RELEASE);
        g_processGloveRelay = nullptr;
        g_processGloveCallsite = 0;
        g_processGloveOriginalDisplacement = 0;
        g_processGloveModel = nullptr;
        AppendLog("Glove process diagnostic: retirado.");
    }

    uintptr_t ResolveLastSpawnedGlove() {
        if (!g_resolveGloveEntity) return 0;
        const uint32_t handle = static_cast<uint32_t>(
            InterlockedCompareExchange(&g_spawnGloveLastHandle, 0, 0));
        if (!handle || handle == 0xFFFFFFFF) return 0;
        uint32_t handleCopy = handle;
        __try {
            return reinterpret_cast<uintptr_t>(
                g_resolveGloveEntity(&handleCopy));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    void __fastcall UpdateGlovePawnDiagnosticHook(void* pawn) {
        const LONG spawnCalls = InterlockedCompareExchange(
            &g_spawnGloveCalls, 0, 0);
        const LONG observedSpawn = InterlockedCompareExchange(
            &g_updateGloveObservedSpawn, 0, 0);
        const bool capture = spawnCalls != observedSpawn;
        const uintptr_t before = capture ? ResolveLastSpawnedGlove() : 0;

        UpdateGlovePawnFn original = g_updateGlovePawn;
        if (original) original(pawn);
        if (!capture) return;

        const uintptr_t after = ResolveLastSpawnedGlove();
        const uintptr_t entity = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_spawnGloveLastEntity, 0, 0));
        uintptr_t afterVtable = 0;
        (void)SafeRead(entity, afterVtable);
        InterlockedExchange64(&g_updateGloveBeforeResolved,
            static_cast<LONG64>(before));
        InterlockedExchange64(&g_updateGloveAfterResolved,
            static_cast<LONG64>(after));
        InterlockedExchange64(&g_updateGloveAfterVtable,
            static_cast<LONG64>(afterVtable));
        InterlockedExchange(&g_updateGloveObservedSpawn, spawnCalls);
    }

    bool InstallGlovePawnUpdateDiagnostic(
        uintptr_t callsite, UpdateGlovePawnFn original) {
        if (!callsite || !original || ((callsite + 1) & 3) != 0 ||
            g_updateGloveCallsite)
            return false;
        void* relay = AllocateExecutableNear(callsite, 0x1000);
        if (!relay) return false;
        unsigned char relayCode[12] = { 0x48, 0xB8 };
        const uintptr_t hook = reinterpret_cast<uintptr_t>(
            &UpdateGlovePawnDiagnosticHook);
        CopyMemory(relayCode + 2, &hook, sizeof(hook));
        relayCode[10] = 0xFF;
        relayCode[11] = 0xE0;
        CopyMemory(relay, relayCode, sizeof(relayCode));
        FlushInstructionCache(GetCurrentProcess(), relay, sizeof(relayCode));
        const int64_t relative64 = reinterpret_cast<uintptr_t>(relay) -
            (callsite + 5);
        if (relative64 < INT32_MIN || relative64 > INT32_MAX) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        int32_t originalDisplacement = 0;
        if (!SafeRead(callsite + 1, originalDisplacement)) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        DWORD oldProtection = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(callsite + 1),
                sizeof(int32_t), PAGE_EXECUTE_READWRITE, &oldProtection)) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        g_updateGlovePawn = original;
        InterlockedExchange(reinterpret_cast<volatile LONG*>(callsite + 1),
            static_cast<LONG>(relative64));
        FlushInstructionCache(GetCurrentProcess(),
            reinterpret_cast<void*>(callsite), 5);
        DWORD ignored = 0;
        (void)VirtualProtect(reinterpret_cast<void*>(callsite + 1),
            sizeof(int32_t), oldProtection, &ignored);
        g_updateGloveCallsite = callsite;
        g_updateGloveRelay = relay;
        g_updateGloveOriginalDisplacement = originalDisplacement;
        AppendLog("Glove pawn-update diagnostic: instalado.");
        return true;
    }

    void RemoveGlovePawnUpdateDiagnostic() {
        if (!g_updateGloveCallsite) return;
        DWORD oldProtection = 0;
        if (VirtualProtect(reinterpret_cast<void*>(g_updateGloveCallsite + 1),
                sizeof(int32_t), PAGE_EXECUTE_READWRITE, &oldProtection)) {
            InterlockedExchange(reinterpret_cast<volatile LONG*>(
                    g_updateGloveCallsite + 1),
                g_updateGloveOriginalDisplacement);
            FlushInstructionCache(GetCurrentProcess(),
                reinterpret_cast<void*>(g_updateGloveCallsite), 5);
            DWORD ignored = 0;
            (void)VirtualProtect(reinterpret_cast<void*>(
                    g_updateGloveCallsite + 1), sizeof(int32_t),
                oldProtection, &ignored);
        }
        Sleep(20);
        if (g_updateGloveRelay)
            VirtualFree(g_updateGloveRelay, 0, MEM_RELEASE);
        g_updateGloveRelay = nullptr;
        g_updateGloveCallsite = 0;
        g_updateGloveOriginalDisplacement = 0;
        g_updateGlovePawn = nullptr;
        AppendLog("Glove pawn-update diagnostic: retirado.");
    }

    uint32_t* __fastcall SpawnGloveModelDiagnosticHook(
        void* builder, uint32_t* outputHandle) {
        SpawnGloveModelFn original = g_spawnGloveModel;
        uint32_t* result = original ? original(builder, outputHandle) : nullptr;
        const uint32_t handle = outputHandle ? *outputHandle : 0xFFFFFFFF;
        uintptr_t entity = 0;
        if (g_resolveGloveEntity && outputHandle) {
            __try {
                entity = reinterpret_cast<uintptr_t>(
                    g_resolveGloveEntity(outputHandle));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                entity = 0;
            }
        }
        InterlockedIncrement(&g_spawnGloveCalls);
        InterlockedExchange(&g_spawnGloveLastHandle,
            static_cast<LONG>(handle));
        InterlockedExchange64(&g_spawnGloveLastEntity,
            static_cast<LONG64>(entity));
        InstallGloveRenderDiagnostic(entity);
        return result;
    }

    bool InstallGloveSpawnDiagnostic(
        uintptr_t callsite, SpawnGloveModelFn original) {
        if (!callsite || !original || (callsite & 7) != 0 ||
            g_spawnGloveCallsite)
            return false;
        void* relay = AllocateExecutableNear(callsite, 0x1000);
        if (!relay) return false;
        unsigned char relayCode[12] = { 0x48, 0xB8 };
        const uintptr_t hook = reinterpret_cast<uintptr_t>(
            &SpawnGloveModelDiagnosticHook);
        CopyMemory(relayCode + 2, &hook, sizeof(hook));
        relayCode[10] = 0xFF;
        relayCode[11] = 0xE0;
        CopyMemory(relay, relayCode, sizeof(relayCode));
        FlushInstructionCache(GetCurrentProcess(), relay, sizeof(relayCode));

        uint64_t originalBytes = 0;
        if (!SafeRead(callsite, originalBytes) ||
            static_cast<unsigned char>(originalBytes) != 0xE8) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        const int64_t relative64 = reinterpret_cast<uintptr_t>(relay) -
            (callsite + 5);
        if (relative64 < INT32_MIN || relative64 > INT32_MAX) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        uint64_t replacement = originalBytes;
        const int32_t relative = static_cast<int32_t>(relative64);
        CopyMemory(reinterpret_cast<unsigned char*>(&replacement) + 1,
            &relative, sizeof(relative));
        DWORD oldProtection = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(callsite), sizeof(uint64_t),
                PAGE_EXECUTE_READWRITE, &oldProtection)) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        g_spawnGloveModel = original;
        InterlockedExchange64(reinterpret_cast<volatile LONG64*>(callsite),
            static_cast<LONG64>(replacement));
        FlushInstructionCache(GetCurrentProcess(),
            reinterpret_cast<void*>(callsite), sizeof(uint64_t));
        DWORD ignored = 0;
        (void)VirtualProtect(reinterpret_cast<void*>(callsite),
            sizeof(uint64_t), oldProtection, &ignored);
        g_spawnGloveCallsite = callsite;
        g_spawnGloveRelay = relay;
        g_spawnGloveOriginalBytes = originalBytes;
        AppendLog("Glove spawn diagnostic: instalado.");
        return true;
    }

    void RemoveGloveSpawnDiagnostic() {
        if (!g_spawnGloveCallsite) return;
        DWORD oldProtection = 0;
        if (VirtualProtect(reinterpret_cast<void*>(g_spawnGloveCallsite),
                sizeof(uint64_t), PAGE_EXECUTE_READWRITE, &oldProtection)) {
            InterlockedExchange64(
                reinterpret_cast<volatile LONG64*>(g_spawnGloveCallsite),
                static_cast<LONG64>(g_spawnGloveOriginalBytes));
            FlushInstructionCache(GetCurrentProcess(),
                reinterpret_cast<void*>(g_spawnGloveCallsite),
                sizeof(uint64_t));
            DWORD ignored = 0;
            (void)VirtualProtect(reinterpret_cast<void*>(g_spawnGloveCallsite),
                sizeof(uint64_t), oldProtection, &ignored);
        }
        Sleep(20);
        if (g_spawnGloveRelay)
            VirtualFree(g_spawnGloveRelay, 0, MEM_RELEASE);
        g_spawnGloveRelay = nullptr;
        g_spawnGloveCallsite = 0;
        g_spawnGloveOriginalBytes = 0;
        g_spawnGloveModel = nullptr;
        AppendLog("Glove spawn diagnostic: retirado.");
    }

    bool __fastcall AttachGloveModelDiagnosticHook(
        void* owner, void* entity) {
        AttachGloveModelFn original = g_attachGloveModel;
        const bool result = original ? original(owner, entity) : false;
        InterlockedIncrement(&g_attachGloveCalls);
        InterlockedExchange(&g_attachGloveLastResult, result ? 1 : -1);
        InterlockedExchange64(&g_attachGloveLastOwner,
            reinterpret_cast<LONG64>(owner));
        InterlockedExchange64(&g_attachGloveLastEntity,
            reinterpret_cast<LONG64>(entity));
        return result;
    }

    bool InstallGloveAttachDiagnostic(
        uintptr_t callsite, AttachGloveModelFn original) {
        if (!callsite || !original || ((callsite + 1) & 3) != 0 ||
            g_attachGloveCallsite)
            return false;
        void* relay = AllocateExecutableNear(callsite, 0x1000);
        if (!relay) return false;
        unsigned char relayCode[12] = { 0x48, 0xB8 };
        const uintptr_t hook = reinterpret_cast<uintptr_t>(
            &AttachGloveModelDiagnosticHook);
        CopyMemory(relayCode + 2, &hook, sizeof(hook));
        relayCode[10] = 0xFF;
        relayCode[11] = 0xE0;
        CopyMemory(relay, relayCode, sizeof(relayCode));
        FlushInstructionCache(GetCurrentProcess(), relay, sizeof(relayCode));

        unsigned char opcode = 0;
        int32_t originalDisplacement = 0;
        if (!SafeRead(callsite, opcode) || opcode != 0xE8 ||
            !SafeRead(callsite + 1, originalDisplacement)) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        const int64_t relative64 = reinterpret_cast<uintptr_t>(relay) -
            (callsite + 5);
        if (relative64 < INT32_MIN || relative64 > INT32_MAX) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        DWORD oldProtection = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(callsite + 1),
                sizeof(int32_t), PAGE_EXECUTE_READWRITE, &oldProtection)) {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        g_attachGloveModel = original;
        InterlockedExchange(reinterpret_cast<volatile LONG*>(callsite + 1),
            static_cast<LONG>(relative64));
        FlushInstructionCache(GetCurrentProcess(),
            reinterpret_cast<void*>(callsite), 5);
        DWORD ignored = 0;
        (void)VirtualProtect(reinterpret_cast<void*>(callsite + 1),
            sizeof(int32_t), oldProtection, &ignored);
        g_attachGloveCallsite = callsite;
        g_attachGloveRelay = relay;
        g_attachGloveOriginalDisplacement = originalDisplacement;
        AppendLog("Glove attach diagnostic: instalado.");
        return true;
    }

    void RemoveGloveAttachDiagnostic() {
        if (!g_attachGloveCallsite) return;
        DWORD oldProtection = 0;
        if (VirtualProtect(reinterpret_cast<void*>(g_attachGloveCallsite + 1),
                sizeof(int32_t), PAGE_EXECUTE_READWRITE, &oldProtection)) {
            InterlockedExchange(reinterpret_cast<volatile LONG*>(
                    g_attachGloveCallsite + 1),
                g_attachGloveOriginalDisplacement);
            FlushInstructionCache(GetCurrentProcess(),
                reinterpret_cast<void*>(g_attachGloveCallsite), 5);
            DWORD ignored = 0;
            (void)VirtualProtect(reinterpret_cast<void*>(
                    g_attachGloveCallsite + 1), sizeof(int32_t),
                oldProtection, &ignored);
        }
        Sleep(20);
        if (g_attachGloveRelay)
            VirtualFree(g_attachGloveRelay, 0, MEM_RELEASE);
        g_attachGloveRelay = nullptr;
        g_attachGloveCallsite = 0;
        g_attachGloveOriginalDisplacement = 0;
        g_attachGloveModel = nullptr;
        AppendLog("Glove attach diagnostic: retirado.");
    }

    bool InstallFrameStageDiagnostic(void* clientInterface) {
        void** vtable = nullptr;
        if (!clientInterface || !SafeRead(
            reinterpret_cast<uintptr_t>(clientInterface), vtable) || !vtable)
            return false;
        void* original = nullptr;
        if (!SafeRead(reinterpret_cast<uintptr_t>(
            vtable + kFrameStageIndex), original) ||
            !IsExecutableAddress(original))
            return false;

        g_originalFrameStage = reinterpret_cast<FrameStageFn>(original);
        g_frameStageSlot = vtable + kFrameStageIndex;
        void* replaced = nullptr;
        if (!SetVtableEntry(g_frameStageSlot,
            reinterpret_cast<void*>(&FrameStageDiagnosticHook), &replaced) ||
            replaced != original) {
            g_originalFrameStage = nullptr;
            g_frameStageSlot = nullptr;
            return false;
        }
        AppendLog("FrameStage agent hook: instalado (indice 36).");
        return true;
    }

    void RemoveFrameStageDiagnostic() {
        if (!g_frameStageSlot || !g_originalFrameStage) return;
        void* current = nullptr;
        if (SafeRead(reinterpret_cast<uintptr_t>(g_frameStageSlot), current) &&
            current == reinterpret_cast<void*>(&FrameStageDiagnosticHook)) {
            (void)SetVtableEntry(g_frameStageSlot,
                reinterpret_cast<void*>(g_originalFrameStage), nullptr);
        }
        char message[160]{};
        StringCchPrintfA(message, _countof(message),
            "FrameStage agent hook: retirado, stage6=%ld.",
            InterlockedCompareExchange(&g_frameStageSixCalls, 0, 0));
        AppendLog(message);
        g_frameStageSlot = nullptr;
        g_originalFrameStage = nullptr;
    }

    struct PatternResult {
        DWORD count = 0;
        uintptr_t first = 0;
    };

    PatternResult ScanExecutableSections(
        HMODULE module, const unsigned char* bytes, const char* mask) {
        PatternResult result;
        if (!module || !bytes || !mask) return result;
        const SIZE_T patternLength = static_cast<SIZE_T>(lstrlenA(mask));
        if (patternLength == 0) return result;

        const auto* base = reinterpret_cast<const unsigned char*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return result;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return result;

        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (WORD sectionIndex = 0; sectionIndex < nt->FileHeader.NumberOfSections;
            ++sectionIndex, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const SIZE_T sectionSize = section->Misc.VirtualSize;
            if (sectionSize < patternLength) continue;
            const unsigned char* sectionStart = base + section->VirtualAddress;
            for (SIZE_T offset = 0; offset <= sectionSize - patternLength; ++offset) {
                bool match = true;
                for (SIZE_T byteIndex = 0; byteIndex < patternLength; ++byteIndex) {
                    if (mask[byteIndex] == 'x' &&
                        sectionStart[offset + byteIndex] != bytes[byteIndex]) {
                        match = false;
                        break;
                    }
                }
                if (!match) continue;
                ++result.count;
                if (!result.first)
                    result.first = reinterpret_cast<uintptr_t>(
                        sectionStart + offset);
            }
        }
        return result;
    }

    void LogPatternResult(
        const char* name, HMODULE module, const PatternResult& result) {
        char message[192]{};
        const uintptr_t relative = result.first && module
            ? result.first - reinterpret_cast<uintptr_t>(module) : 0;
        StringCchPrintfA(message, _countof(message),
            "%s: count=%lu first_rva=0x%llX",
            name, static_cast<unsigned long>(result.count),
            static_cast<unsigned long long>(relative));
        AppendLog(message);
    }

    void ProbeCriticalPatterns() {
        HMODULE client = GetModuleHandleW(L"client.dll");
        if (!client) {
            AppendLog("Firmas client.dll: modulo no disponible.");
            return;
        }

        constexpr unsigned char setModel[] = {
            0x40, 0x53, 0x48, 0x83, 0xEC, 0x00, 0x48, 0x8B, 0xD9, 0x4C,
            0x8B, 0xC2, 0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48,
            0x8D, 0x54, 0x24
        };
        constexpr unsigned char updateSubclass[] = {
            0x4C, 0x8B, 0xDC, 0x53, 0x48, 0x81, 0xEC, 0x90,
            0x01, 0x00, 0x00, 0x48, 0x8B, 0x41, 0x10, 0x48,
            0x8B, 0xD9, 0x8B, 0x50, 0x30, 0xC1, 0xEA, 0x04,
            0xF6, 0xC2, 0x01
        };
        constexpr unsigned char refreshWeaponModules[] = {
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57,
            0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
            0x48, 0x8D, 0x6C, 0x24, 0x00, 0x48, 0x81, 0xEC,
            0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xDA, 0x4C,
            0x8B, 0xE9, 0x48, 0x85, 0xC9
        };
        constexpr unsigned char clearWeaponCustomMaterials[] = {
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
            0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
            0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20,
            0x44, 0x0F, 0xB6, 0xF2, 0x48, 0x8B, 0xF9, 0xE8,
            0x00, 0x00, 0x00, 0x00, 0x45, 0x33, 0xFF, 0x48,
            0x8B, 0xF0, 0x48, 0x85, 0xC0
        };
        constexpr unsigned char setBodyGroup[] = {
            0x85, 0xD2, 0x0F, 0x88, 0x00, 0x00, 0x00, 0x00,
            0x53, 0x55, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x63,
            0xDA, 0x48, 0x8B, 0xE9, 0x3B, 0x99, 0xD0, 0x08,
            0x00, 0x00
        };
        constexpr unsigned char setMeshGroupMask[] = {
            0x48, 0x89, 0x5C, 0x24, 0x00, 0x48, 0x89, 0x74,
            0x24, 0x00, 0x57, 0x48, 0x83, 0xEC, 0x00, 0x48,
            0x8D, 0x99, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B,
            0x71
        };
        constexpr unsigned char inventoryManager[] = {
            0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC3,
            0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
            0x8B, 0x91, 0x00, 0x00, 0x00, 0x00, 0xB8
        };
        constexpr unsigned char findSoCache[] = {
            0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF0,
            0x48, 0x85, 0xC0, 0x74, 0x0E, 0x4C, 0x8B, 0xC3
        };
        constexpr unsigned char createTypeCache[] = {
            0x40, 0x53, 0x48, 0x83, 0xEC, 0x00, 0x4C, 0x8B,
            0x49, 0x00, 0x44, 0x8B, 0xD2
        };
        constexpr unsigned char findHudElement[] = {
            0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
            0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9,
            0x48, 0x85, 0xC0, 0x74, 0x79
        };
        constexpr unsigned char clearHudWeaponIconCall[] = {
            0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xF8, 0xC6,
            0x84, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        constexpr unsigned char resolveGloveEntityCall[] = {
            0x48, 0x8D, 0x4C, 0x24, 0x60, 0xE8, 0x00, 0x00,
            0x00, 0x00, 0x8B, 0x08, 0x89, 0x0E, 0x48, 0x8B,
            0xCE, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B,
            0xD8, 0x48, 0x85, 0xC0, 0x75, 0x0F
        };
        constexpr unsigned char resolveGloveOwnerCall[] = {
            0x48, 0x8B, 0xCF, 0xE8, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x8B, 0xCF, 0xE8, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x8B, 0xC8, 0x48, 0x8B, 0xD3, 0xE8, 0x00,
            0x00, 0x00, 0x00, 0x48, 0x8B, 0x85, 0x00, 0x03,
            0x00, 0x00
        };

        const PatternResult setModelResult = ScanExecutableSections(
            client, setModel, "xxxxx?xxxxxxxxx????xxxx");
        LogPatternResult("SetModel", client, setModelResult);
        g_setModel = setModelResult.count == 1
            ? reinterpret_cast<SetModelFn>(setModelResult.first) : nullptr;
        const PatternResult updateSubclassResult = ScanExecutableSections(
            client, updateSubclass, "xxxxxxxxxxxxxxxxxxxxxxxxxxx");
        LogPatternResult("UpdateSubclass", client, updateSubclassResult);
        g_updateSubclass = updateSubclassResult.count == 1
            ? reinterpret_cast<UpdateSubclassFn>(updateSubclassResult.first)
            : nullptr;
        const PatternResult refreshWeaponModulesResult =
            ScanExecutableSections(client, refreshWeaponModules,
                "xxxxxxxxxxxxxxxxxxxx?xxx????xxxxxxxxx");
        LogPatternResult("RefreshWeaponModules", client,
            refreshWeaponModulesResult);
        g_refreshWeaponModules = refreshWeaponModulesResult.count == 1
            ? reinterpret_cast<RefreshWeaponModulesFn>(
                refreshWeaponModulesResult.first) : nullptr;
        const PatternResult clearWeaponCustomMaterialsResult =
            ScanExecutableSections(client, clearWeaponCustomMaterials,
                "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxx");
        LogPatternResult("ClearWeaponCustomMaterials", client,
            clearWeaponCustomMaterialsResult);
        g_clearWeaponCustomMaterials =
            clearWeaponCustomMaterialsResult.count == 1
            ? reinterpret_cast<ClearWeaponCustomMaterialsFn>(
                clearWeaponCustomMaterialsResult.first) : nullptr;
        const PatternResult setBodyGroupResult = ScanExecutableSections(
            client, setBodyGroup, "xxxx????xxxxxxxxxxxxxxxxxx");
        LogPatternResult("SetBodyGroup", client, setBodyGroupResult);
        g_setBodyGroup = setBodyGroupResult.count == 1
            ? reinterpret_cast<SetBodyGroupFn>(setBodyGroupResult.first)
            : nullptr;
        const PatternResult setMeshGroupMaskResult = ScanExecutableSections(
            client, setMeshGroupMask, "xxxx?xxxx?xxxx?xxx????xxx");
        LogPatternResult("SetMeshGroupMask", client,
            setMeshGroupMaskResult);
        g_setMeshGroupMask = setMeshGroupMaskResult.count == 1
            ? reinterpret_cast<SetMeshGroupMaskFn>(
                setMeshGroupMaskResult.first) : nullptr;
        const PatternResult findHudResult = ScanExecutableSections(
            client, findHudElement, "xxxxxxxxx????xxxxxxxx");
        LogPatternResult("FindHudElement", client, findHudResult);
        g_findHudElement = findHudResult.count == 1
            ? reinterpret_cast<FindHudElementFn>(findHudResult.first) : nullptr;
        const PatternResult clearHudCallResult = ScanExecutableSections(
            client, clearHudWeaponIconCall, "x????xxxxx?????");
        LogPatternResult("ClearHudWeaponIcon callsite", client,
            clearHudCallResult);
        if (clearHudCallResult.count == 1) {
            int32_t displacement = 0;
            if (SafeRead(clearHudCallResult.first + 1, displacement))
                g_clearHudWeaponIcon = reinterpret_cast<ClearHudWeaponIconFn>(
                    clearHudCallResult.first + 5 + displacement);
        }
        const PatternResult resolveGloveEntityCallResult =
            ScanExecutableSections(client, resolveGloveEntityCall,
                "xxxxxx????xxxxxxxx????xxxxxxxx");
        LogPatternResult("ResolveGloveEntity callsite", client,
            resolveGloveEntityCallResult);
        if (resolveGloveEntityCallResult.count == 1) {
            int32_t displacement = 0;
            constexpr uintptr_t kResolveCallOffset = 17;
            if (SafeRead(resolveGloveEntityCallResult.first +
                    kResolveCallOffset + 1, displacement)) {
                g_resolveGloveEntity = reinterpret_cast<ResolveGloveEntityFn>(
                    resolveGloveEntityCallResult.first +
                    kResolveCallOffset + 5 + displacement);
            }
        }
        const PatternResult resolveGloveOwnerCallResult =
            ScanExecutableSections(client, resolveGloveOwnerCall,
                "xxxx????xxxx????xxxxxxx????xxxxxxx");
        LogPatternResult("ResolveGloveOwner callsite", client,
            resolveGloveOwnerCallResult);
        if (resolveGloveOwnerCallResult.count == 1) {
            int32_t displacement = 0;
            constexpr uintptr_t kOwnerCallOffset = 11;
            if (SafeRead(resolveGloveOwnerCallResult.first +
                    kOwnerCallOffset + 1, displacement)) {
                g_resolveGloveOwner = reinterpret_cast<ResolveGloveOwnerFn>(
                    resolveGloveOwnerCallResult.first + kOwnerCallOffset +
                    5 + displacement);
            }
        }
        const unsigned char processGloveModelCall[] = {
            0x48, 0x8D, 0x8B, 0x18, 0x15, 0x00, 0x00, 0xE8,
            0, 0, 0, 0, 0x48, 0x8B, 0xCB, 0xE8, 0, 0, 0, 0
        };
        const PatternResult processGloveModelCallResult =
            ScanExecutableSections(client, processGloveModelCall,
                "xxxxxxxx????xxxx????");
        LogPatternResult("ProcessGloveModel callsite", client,
            processGloveModelCallResult);
        AppendLog("Glove invasive diagnostics: disabled; lifecycle observer active.");
        LogPatternResult("InventoryManager", client, ScanExecutableSections(
            client, inventoryManager, "xxx????xxxxxxxxxxx????x"));
        LogPatternResult("FindSOCache callsite", client, ScanExecutableSections(
            client, findSoCache, "x????xxxxxxxxxxx"));
        LogPatternResult("CreateBaseTypeCache", client, ScanExecutableSections(
            client, createTypeCache, "xxxxx?xxx?xxx"));
    }

    bool ReadPawnModel(
        uintptr_t pawn, const InventorySnapshotSelection& selection,
        char* output, SIZE_T capacity) {
        if (!pawn || !output || capacity < 2 ||
            !selection.gameSceneNodeOffset || !selection.modelStateOffset ||
            !selection.modelNameOffset)
            return false;
        output[0] = '\0';
        uintptr_t sceneNode = 0;
        uintptr_t modelName = 0;
        return SafeRead(pawn + selection.gameSceneNodeOffset, sceneNode) &&
            sceneNode && SafeRead(sceneNode + selection.modelStateOffset +
                selection.modelNameOffset, modelName) && modelName &&
            SafeReadString(modelName, output, capacity);
    }

    uintptr_t ResolveGloveAttachment(
        HMODULE client, uintptr_t pawn, uintptr_t stride,
        const InventorySnapshotSelection& selection,
        uint32_t* observedHandle = nullptr) {
        constexpr uintptr_t kGloveModelHandleOffset = 0x1518;
        uint32_t handle = 0;
        uintptr_t entityList = 0;
        if (observedHandle) *observedHandle = 0;
        if (!client || !pawn || !stride || !selection.entityListOffset ||
            !SafeRead(pawn + kGloveModelHandleOffset, handle) || !handle ||
            handle == 0xFFFFFFFF ||
            !SafeRead(reinterpret_cast<uintptr_t>(client) +
                selection.entityListOffset, entityList) || !entityList)
            return 0;
        if (observedHandle) *observedHandle = handle;
        return ReadEntityByIndex(entityList,
            static_cast<int>(handle & 0x7FFF), stride);
    }

    uintptr_t ResolveHudArmsEntity(
        HMODULE client, uintptr_t pawn, uintptr_t stride,
        const InventorySnapshotSelection& selection,
        uint32_t* observedHandle = nullptr) {
        uint32_t handle = 0;
        uintptr_t entityList = 0;
        if (observedHandle) *observedHandle = 0;
        if (!client || !pawn || !stride || !selection.entityListOffset ||
            !selection.hudModelArmsOffset ||
            !SafeRead(pawn + selection.hudModelArmsOffset, handle) ||
            !handle || handle == 0xFFFFFFFF ||
            !SafeRead(reinterpret_cast<uintptr_t>(client) +
                selection.entityListOffset, entityList) || !entityList)
            return 0;
        if (observedHandle) *observedHandle = handle;
        return ReadEntityByIndex(entityList,
            static_cast<int>(handle & 0x7FFF), stride);
    }

    bool ReadModelBodyGroup(
        uintptr_t entity, const InventorySnapshotSelection& selection,
        int index, int& value) {
        value = -1;
        if (!entity || index < 0 || !selection.gameSceneNodeOffset ||
            !selection.modelStateOffset)
            return false;
        uintptr_t sceneNode = 0;
        uintptr_t choices = 0;
        int count = 0;
        const uintptr_t bodyGroupsOffset = selection.modelStateOffset + 0x258;
        return SafeRead(entity + selection.gameSceneNodeOffset, sceneNode) &&
            sceneNode && SafeRead(sceneNode + bodyGroupsOffset, count) &&
            count > index && count <= 32 &&
            SafeRead(sceneNode + bodyGroupsOffset + 8, choices) && choices &&
            SafeRead(choices + sizeof(int) * static_cast<uintptr_t>(index),
                value);
    }

    uintptr_t ResolveGloveAttachmentNative(uintptr_t pawn) {
        if (!pawn || !g_resolveGloveEntity) return 0;
        constexpr uintptr_t kGloveModelControlOffset = 0x1518;
        __try {
            return reinterpret_cast<uintptr_t>(g_resolveGloveEntity(
                reinterpret_cast<void*>(pawn + kGloveModelControlOffset)));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    uintptr_t ResolveGloveOwnerNative(uintptr_t pawn) {
        if (!pawn || !g_resolveGloveOwner) return 0;
        __try {
            return reinterpret_cast<uintptr_t>(g_resolveGloveOwner(
                reinterpret_cast<void*>(pawn)));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    const char* GloveLifecycleStateName(GloveLifecycleState state) {
        switch (state) {
        case GloveLifecycleState::Disabled: return "Disabled";
        case GloveLifecycleState::WaitingForPawn: return "WaitingForPawn";
        case GloveLifecycleState::WaitingForWearable:
            return "WaitingForWearable";
        case GloveLifecycleState::WaitingForStableContext:
            return "WaitingForStableContext";
        case GloveLifecycleState::Applying: return "Applying";
        case GloveLifecycleState::Applied: return "Applied";
        case GloveLifecycleState::EntityLost: return "EntityLost";
        case GloveLifecycleState::PendingReapply: return "PendingReapply";
        case GloveLifecycleState::Restoring: return "Restoring";
        }
        return "Unknown";
    }

    uint64_t HashGloveDiagnosticValue(uint64_t hash, uint64_t value) {
        constexpr uint64_t kFnvPrime = 1099511628211ull;
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= static_cast<uint8_t>(value >> (byte * 8));
            hash *= kFnvPrime;
        }
        return hash;
    }

    uint64_t HashGloveDiagnosticMemory(uintptr_t address, SIZE_T size) {
        if (!IsReadableMemory(reinterpret_cast<const void*>(address), size))
            return 0;
        uint64_t hash = 14695981039346656037ull;
        const auto* bytes = reinterpret_cast<const uint8_t*>(address);
        for (SIZE_T index = 0; index < size; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    bool SameGloveCoreContext(
        const GloveContextIdentity& left,
        const GloveContextIdentity& right) {
        const float spawnDelta = left.pawnSpawnTime - right.pawnSpawnTime;
        return left.pawnHandle == right.pawnHandle &&
            left.pawnAddress == right.pawnAddress &&
            spawnDelta < 0.001f && spawnDelta > -0.001f &&
            left.team == right.team &&
            left.itemViewAddress == right.itemViewAddress &&
            left.definitionIndex == right.definitionIndex &&
            left.itemId == right.itemId &&
            left.initialized == right.initialized &&
            left.hudArmsHandle == right.hudArmsHandle &&
            left.hudArmsAddress == right.hudArmsAddress &&
            left.hudSceneNodeAddress == right.hudSceneNodeAddress;
    }

    void LogGloveVisualDiagnostics(
        HMODULE client, uintptr_t pawn, uintptr_t stride,
        const InventorySnapshotSelection& selection) {
        if (!client || !pawn || !stride || !selection.entityListOffset)
            return;

        uintptr_t entityList = 0;
        (void)SafeRead(reinterpret_cast<uintptr_t>(client) +
            selection.entityListOffset, entityList);

        int wearableCount = -1;
        uintptr_t wearableData = 0;
        uint32_t wearableHandle = 0;
        uintptr_t wearable = 0;
        uint16_t wearableDefinition = 0;
        int wearablePaint = 0;
        char wearableModel[260]{};
        if (selection.myWearablesOffset) {
            (void)SafeRead(pawn + selection.myWearablesOffset,
                wearableCount);
            (void)SafeRead(pawn + selection.myWearablesOffset + 8,
                wearableData);
            if (wearableCount > 0 && wearableCount <= 32 && wearableData &&
                SafeRead(wearableData, wearableHandle) && entityList) {
                wearable = ReadEntityByIndex(entityList,
                    static_cast<int>(wearableHandle & 0x7FFF), stride);
                if (wearable) {
                    const uintptr_t itemView = wearable +
                        selection.attributeManagerOffset +
                        selection.itemOffset;
                    (void)SafeRead(itemView +
                        selection.itemDefinitionIndexOffset,
                        wearableDefinition);
                    (void)SafeRead(wearable +
                        selection.fallbackPaintKitOffset, wearablePaint);
                    (void)ReadPawnModel(wearable, selection, wearableModel,
                        _countof(wearableModel));
                }
            }
        }

        uint32_t hudHandle = 0;
        uintptr_t hudArms = 0;
        char hudModel[260]{};
        int bodyGroupCount = -1;
        int bodyGroup0 = -1;
        int bodyGroup1 = -1;
        int hudChildrenCount = 0;
        uint64_t hudChildrenFingerprint = 14695981039346656037ull;
        char hudChildrenSummary[768]{};
        bool needReapply = false;
        (void)SafeRead(pawn + selection.needToReApplyGlovesOffset,
            needReapply);
        uint32_t attachmentHandle = 0;
        const uintptr_t attachment = ResolveGloveAttachment(
            client, pawn, stride, selection, &attachmentHandle);
        const uintptr_t nativeAttachment = ResolveGloveAttachmentNative(pawn);
        const uintptr_t nativeOwner = ResolveGloveOwnerNative(pawn);
        const LONG spawnCalls = InterlockedCompareExchange(
            &g_spawnGloveCalls, 0, 0);
        const uint32_t spawnHandle = static_cast<uint32_t>(
            InterlockedCompareExchange(&g_spawnGloveLastHandle, 0, 0));
        const uintptr_t spawnEntity = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_spawnGloveLastEntity, 0, 0));
        const LONG attachCalls = InterlockedCompareExchange(
            &g_attachGloveCalls, 0, 0);
        const LONG attachResult = InterlockedCompareExchange(
            &g_attachGloveLastResult, 0, 0);
        const uintptr_t attachOwner = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_attachGloveLastOwner, 0, 0));
        const uintptr_t attachEntity = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_attachGloveLastEntity, 0, 0));
        const LONG renderCalls = InterlockedCompareExchange(
            &g_registerGloveRenderCalls, 0, 0);
        const uintptr_t renderEntity = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_registerGloveRenderEntity, 0, 0));
        const uintptr_t renderBefore = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_registerGloveRenderBefore, 0, 0));
        const uintptr_t renderAfter = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_registerGloveRenderAfter, 0, 0));
        const LONG processCalls = InterlockedCompareExchange(
            &g_processGloveCalls, 0, 0);
        const uintptr_t processVtable = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_processGloveAfterVtable, 0, 0));
        const uintptr_t processResolved = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_processGloveAfterResolved, 0, 0));
        const LONG updateObserved = InterlockedCompareExchange(
            &g_updateGloveObservedSpawn, 0, 0);
        const uintptr_t updateBefore = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_updateGloveBeforeResolved, 0, 0));
        const uintptr_t updateAfter = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_updateGloveAfterResolved, 0, 0));
        const uintptr_t updateVtable = static_cast<uintptr_t>(
            InterlockedCompareExchange64(&g_updateGloveAfterVtable, 0, 0));
        char attachmentModel[260]{};
        const uintptr_t modelAttachment = nativeAttachment
            ? nativeAttachment : attachment;
        if (modelAttachment)
            (void)ReadPawnModel(modelAttachment, selection, attachmentModel,
                _countof(attachmentModel));
        if (selection.hudModelArmsOffset && entityList &&
            SafeRead(pawn + selection.hudModelArmsOffset, hudHandle) &&
            hudHandle && hudHandle != 0xFFFFFFFF) {
            hudArms = ReadEntityByIndex(entityList,
                static_cast<int>(hudHandle & 0x7FFF), stride);
            if (hudArms) {
                (void)ReadPawnModel(hudArms, selection, hudModel,
                    _countof(hudModel));
                uintptr_t sceneNode = 0;
                uintptr_t choices = 0;
                if (SafeRead(hudArms + selection.gameSceneNodeOffset,
                        sceneNode) && sceneNode) {
                    const uintptr_t bodyGroups = sceneNode +
                        selection.modelStateOffset + 0x258;
                    (void)SafeRead(bodyGroups, bodyGroupCount);
                    (void)SafeRead(bodyGroups + 8, choices);
                    if (bodyGroupCount > 0 && bodyGroupCount <= 32 && choices)
                        (void)SafeRead(choices, bodyGroup0);
                    if (bodyGroupCount > 1 && bodyGroupCount <= 32 && choices)
                        (void)SafeRead(choices + sizeof(int), bodyGroup1);
                }
            }
        }

        if (hudArms &&
            selection.sceneNodeChildOffset &&
            selection.sceneNodeNextSiblingOffset &&
            selection.sceneNodeOwnerOffset) {
            uintptr_t hudSceneNode = 0;
            uintptr_t child = 0;
            if (SafeRead(hudArms + selection.gameSceneNodeOffset,
                    hudSceneNode) && hudSceneNode)
                (void)SafeRead(hudSceneNode + selection.sceneNodeChildOffset,
                    child);

            while (child && hudChildrenCount < 32) {
                uintptr_t childOwner = 0;
                uint32_t ownerHandle = 0;
                uintptr_t ownerAddress = 0;
                char childModel[260]{};
                (void)SafeRead(child + selection.sceneNodeOwnerOffset,
                    childOwner);
                if (childOwner) {
                    (void)ReadPawnModel(childOwner, selection, childModel,
                        _countof(childModel));
                    if (selection.ownerEntityOffset && entityList &&
                        SafeRead(childOwner + selection.ownerEntityOffset,
                            ownerHandle) && ownerHandle &&
                        ownerHandle != 0xFFFFFFFF) {
                        ownerAddress = ReadEntityByIndex(entityList,
                            static_cast<int>(ownerHandle & 0x7FFF), stride);
                    }
                }

                hudChildrenFingerprint = HashGloveDiagnosticValue(
                    hudChildrenFingerprint, child);
                hudChildrenFingerprint = HashGloveDiagnosticValue(
                    hudChildrenFingerprint, childOwner);
                hudChildrenFingerprint = HashGloveDiagnosticValue(
                    hudChildrenFingerprint, ownerHandle);
                hudChildrenFingerprint = HashGloveDiagnosticValue(
                    hudChildrenFingerprint, ownerAddress);
                for (const unsigned char* cursor =
                        reinterpret_cast<const unsigned char*>(childModel);
                    *cursor; ++cursor) {
                    hudChildrenFingerprint ^= *cursor;
                    hudChildrenFingerprint *= 1099511628211ull;
                }

                if (hudChildrenCount < 8) {
                    char childSummary[320]{};
                    StringCchPrintfA(childSummary, _countof(childSummary),
                        "%s%d:0x%llX/h=0x%X/owner=0x%llX/model=%s",
                        hudChildrenCount ? ";" : "", hudChildrenCount,
                        static_cast<unsigned long long>(childOwner),
                        ownerHandle,
                        static_cast<unsigned long long>(ownerAddress),
                        childModel[0] ? childModel : "<none>");
                    StringCchCatA(hudChildrenSummary,
                        _countof(hudChildrenSummary), childSummary);
                }
                ++hudChildrenCount;

                uintptr_t next = 0;
                if (!SafeRead(child + selection.sceneNodeNextSiblingOffset,
                        next) || next == child)
                    break;
                child = next;
            }
        }
        if (hudChildrenCount == 0)
            hudChildrenFingerprint = 0;

        GloveContextIdentity context{};
        context.wearableHandle = attachmentHandle;
        context.hudArmsHandle = hudHandle;
        context.pawnAddress = pawn;
        context.wearableAddress = modelAttachment;
        context.itemViewAddress = pawn + selection.econGlovesOffset;
        context.hudArmsAddress = hudArms;
        if (hudArms)
            (void)SafeRead(hudArms + selection.gameSceneNodeOffset,
                context.hudSceneNodeAddress);
        context.hudChildrenCount = hudChildrenCount;
        context.hudChildrenFingerprint = hudChildrenFingerprint;
        context.hudBodyGroupCount = bodyGroupCount;
        context.hudBodyGroup0 = bodyGroup0;
        context.hudBodyGroup1 = bodyGroup1;
        constexpr uintptr_t kGloveModelControlOffset = 0x1518;
        const uintptr_t gloveControl = pawn + kGloveModelControlOffset;
        context.gloveControlFingerprint = HashGloveDiagnosticMemory(
            gloveControl + 8, 0x98);
        (void)SafeRead(gloveControl, context.gloveControlHandle);
        (void)SafeRead(gloveControl + 0x80, context.gloveControlReady);
        (void)SafeRead(gloveControl + 0xA0,
            context.gloveControlCachedResource);
        (void)SafeRead(gloveControl + 0xA8, context.gloveControlPawn);
        (void)SafeRead(pawn + selection.teamNumberOffset, context.team);
        (void)SafeRead(pawn + selection.lastSpawnTimeIndexOffset,
            context.pawnSpawnTime);
        (void)SafeRead(context.itemViewAddress +
            selection.itemDefinitionIndexOffset, context.definitionIndex);
        (void)SafeRead(context.itemViewAddress + selection.itemIdOffset,
            context.itemId);
        (void)SafeRead(context.itemViewAddress + selection.initializedOffset,
            context.initialized);
        (void)SafeRead(pawn + selection.econGlovesChangedOffset,
            context.changedCounter);

        uintptr_t controller = 0;
        if (SafeRead(reinterpret_cast<uintptr_t>(client) +
                selection.localPlayerControllerOffset, controller) &&
            controller) {
            (void)SafeRead(controller + selection.playerPawnHandleOffset,
                context.pawnHandle);
        }
        if (modelAttachment) {
            if (SafeRead(modelAttachment + selection.ownerEntityOffset,
                    context.ownerHandle) && context.ownerHandle &&
                context.ownerHandle != 0xFFFFFFFF)
                context.ownerAddress = ReadEntityByIndex(entityList,
                    static_cast<int>(context.ownerHandle & 0x7FFF), stride);
            if (SafeRead(modelAttachment + selection.gameSceneNodeOffset,
                    context.sceneNodeAddress) && context.sceneNodeAddress) {
                (void)SafeRead(context.sceneNodeAddress +
                    selection.sceneNodeOwnerOffset, context.sceneNodeOwner);
                constexpr uintptr_t kSceneNodeParentOffset = 0x38;
                (void)SafeRead(context.sceneNodeAddress +
                    kSceneNodeParentOffset, context.sceneNodeParent);
                if (context.sceneNodeParent)
                    (void)SafeRead(context.sceneNodeParent +
                        selection.sceneNodeOwnerOffset,
                        context.sceneNodeParentOwner);
            }
        }

        const bool coreContextStable = SameGloveCoreContext(
            g_lastGloveContext, context);
        const bool entityLost = g_lastGloveContext.wearableAddress != 0 &&
            context.wearableAddress == 0;
        const bool expectedNativeRelease = entityLost && coreContextStable &&
            g_lastGloveContext.wearableHandle == context.wearableHandle &&
            !needReapply;
        if (expectedNativeRelease) {
            g_nativeReleasedGloveContext = context;
            g_nativeGloveReleaseObserved = true;
        } else if (modelAttachment || needReapply ||
            (g_nativeGloveReleaseObserved && !SameGloveCoreContext(
                g_nativeReleasedGloveContext, context))) {
            g_nativeReleasedGloveContext = {};
            g_nativeGloveReleaseObserved = false;
        }
        const bool nativeReleaseLatched = g_nativeGloveReleaseObserved &&
            SameGloveCoreContext(g_nativeReleasedGloveContext, context) &&
            g_nativeReleasedGloveContext.wearableHandle ==
                context.wearableHandle &&
            !needReapply;
        GloveLifecycleState observedState = GloveLifecycleState::WaitingForPawn;
        if (!selection.enabled)
            observedState = GloveLifecycleState::Disabled;
        else if (g_appliedGloves.reapplyFrames > 0)
            observedState = GloveLifecycleState::Applying;
        else if (modelAttachment)
            observedState = GloveLifecycleState::WaitingForStableContext;
        else if (nativeReleaseLatched)
            observedState = GloveLifecycleState::WaitingForStableContext;
        else if (needReapply)
            observedState = GloveLifecycleState::PendingReapply;
        else if (attachmentHandle && attachmentHandle != 0xFFFFFFFF)
            observedState = GloveLifecycleState::EntityLost;
        else
            observedState = GloveLifecycleState::WaitingForWearable;

        const bool stateChanged = observedState != g_gloveLifecycleState;
        if (stateChanged || entityLost) {
            char transition[1536]{};
            StringCchPrintfA(transition, _countof(transition),
                "Glove lifecycle: %s -> %s lost=%s native_release=%s "
                "pawn=0x%X/0x%llX same=%s spawn=%.6f->%.6f team=%d->%d "
                "wearable=0x%X/0x%llX -> 0x%X/0x%llX "
                "owner=0x%X/0x%llX->0x%X/0x%llX "
                "scene=0x%llX->0x%llX parent=0x%llX/owner=0x%llX "
                "->0x%llX/owner=0x%llX hud=0x%llX/scene=0x%llX "
                "item=0x%llX def=%u id=%llu init=%u changed=%u "
                "hud_handle=0x%X children=%d/fp=0x%llX "
                "bodygroups=%d[%d,%d] control=0x%X/ready=%u/"
                "cached=0x%llX/pawn=0x%llX/fp=0x%llX.",
                GloveLifecycleStateName(g_gloveLifecycleState),
                GloveLifecycleStateName(observedState),
                entityLost ? "yes" : "no",
                expectedNativeRelease ? "expected" : "no",
                context.pawnHandle,
                static_cast<unsigned long long>(context.pawnAddress),
                context.pawnAddress == g_lastGloveContext.pawnAddress
                    ? "yes" : "no",
                g_lastGloveContext.pawnSpawnTime, context.pawnSpawnTime,
                g_lastGloveContext.team, context.team,
                g_lastGloveContext.wearableHandle,
                static_cast<unsigned long long>(
                    g_lastGloveContext.wearableAddress),
                context.wearableHandle,
                static_cast<unsigned long long>(context.wearableAddress),
                g_lastGloveContext.ownerHandle,
                static_cast<unsigned long long>(
                    g_lastGloveContext.ownerAddress),
                context.ownerHandle,
                static_cast<unsigned long long>(context.ownerAddress),
                static_cast<unsigned long long>(
                    g_lastGloveContext.sceneNodeAddress),
                static_cast<unsigned long long>(context.sceneNodeAddress),
                static_cast<unsigned long long>(
                    g_lastGloveContext.sceneNodeParent),
                static_cast<unsigned long long>(
                    g_lastGloveContext.sceneNodeParentOwner),
                static_cast<unsigned long long>(context.sceneNodeParent),
                static_cast<unsigned long long>(
                    context.sceneNodeParentOwner),
                static_cast<unsigned long long>(context.hudArmsAddress),
                static_cast<unsigned long long>(context.hudSceneNodeAddress),
                static_cast<unsigned long long>(context.itemViewAddress),
                static_cast<unsigned>(context.definitionIndex),
                static_cast<unsigned long long>(context.itemId),
                static_cast<unsigned>(context.initialized),
                static_cast<unsigned>(context.changedCounter),
                context.hudArmsHandle, context.hudChildrenCount,
                static_cast<unsigned long long>(
                    context.hudChildrenFingerprint),
                context.hudBodyGroupCount, context.hudBodyGroup0,
                context.hudBodyGroup1, context.gloveControlHandle,
                static_cast<unsigned>(context.gloveControlReady),
                static_cast<unsigned long long>(
                    context.gloveControlCachedResource),
                static_cast<unsigned long long>(context.gloveControlPawn),
                static_cast<unsigned long long>(
                    context.gloveControlFingerprint));
            AppendLog(transition);
        }
        g_gloveLifecycleState = observedState;
        g_lastGloveContext = context;

        char message[2048]{};
        StringCchPrintfA(message, _countof(message),
            "Glove visual: pawn=0x%llX wearables=%d data=0x%llX "
            "handle=0x%X entity=0x%llX def=%u paint=%d model=%s "
            "reapply=%s owner=0x%llX spawn=%ld/0x%X/0x%llX "
            "attach=%ld/%ld/0x%llX/0x%llX "
            "render_call=%ld/0x%llX/0x%llX->0x%llX "
            "process_after=%ld/0x%llX/0x%llX "
            "pawn_update=%ld/0x%llX->0x%llX/vt=0x%llX "
            "attachment=0x%X/table=0x%llX/native=0x%llX attachment_model=%s "
            "hud_handle=0x%X hud=0x%llX hud_model=%s bodygroups=%d[%d,%d] "
            "children=%d/fp=0x%llX {%s} control=0x%X/ready=%u/"
            "cached=0x%llX/pawn=0x%llX/fp=0x%llX.",
            static_cast<unsigned long long>(pawn), wearableCount,
            static_cast<unsigned long long>(wearableData), wearableHandle,
            static_cast<unsigned long long>(wearable),
            static_cast<unsigned>(wearableDefinition), wearablePaint,
            wearableModel[0] ? wearableModel : "<none>",
            needReapply ? "yes" : "no",
            static_cast<unsigned long long>(nativeOwner), spawnCalls,
            spawnHandle, static_cast<unsigned long long>(spawnEntity),
            attachCalls, attachResult,
            static_cast<unsigned long long>(attachOwner),
            static_cast<unsigned long long>(attachEntity),
            renderCalls, static_cast<unsigned long long>(renderEntity),
            static_cast<unsigned long long>(renderBefore),
            static_cast<unsigned long long>(renderAfter),
            processCalls, static_cast<unsigned long long>(processVtable),
            static_cast<unsigned long long>(processResolved),
            updateObserved, static_cast<unsigned long long>(updateBefore),
            static_cast<unsigned long long>(updateAfter),
            static_cast<unsigned long long>(updateVtable),
            attachmentHandle,
            static_cast<unsigned long long>(attachment),
            static_cast<unsigned long long>(nativeAttachment),
            attachmentModel[0] ? attachmentModel : "<none>", hudHandle,
            static_cast<unsigned long long>(hudArms),
            hudModel[0] ? hudModel : "<none>", bodyGroupCount,
            bodyGroup0, bodyGroup1, hudChildrenCount,
            static_cast<unsigned long long>(hudChildrenFingerprint),
            hudChildrenSummary[0] ? hudChildrenSummary : "<none>",
            context.gloveControlHandle,
            static_cast<unsigned>(context.gloveControlReady),
            static_cast<unsigned long long>(
                context.gloveControlCachedResource),
            static_cast<unsigned long long>(context.gloveControlPawn),
            static_cast<unsigned long long>(
                context.gloveControlFingerprint));
        static char lastMessage[2048]{};
        if (strcmp(message, lastMessage) != 0) {
            AppendLog(message);
            StringCchCopyA(lastMessage, _countof(lastMessage), message);
        }
    }

    bool IsKnifeDefinition(int definitionIndex) {
        return definitionIndex == 42 || definitionIndex == 59 ||
            (definitionIndex >= 500 && definitionIndex <= 526);
    }

    const char* FindDefaultKnifeModel(int definitionIndex) {
        if (definitionIndex == 42)
            return "weapons/models/knife/knife_default_ct/weapon_knife_default_ct.vmdl";
        if (definitionIndex == 59)
            return "weapons/models/knife/knife_default_t/weapon_knife_default_t.vmdl";
        return nullptr;
    }

    void LogKnifeDryRun(const char* json) {
        InventorySnapshotSelection selection;
        if (!ParseInventorySelection(json, selection)) return;

        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t controller = 0;
        uintptr_t pawn = 0;
        uintptr_t stride = 0;
        bool usedActivePawn = false;
        if (!ResolvePlayableKnifePawn(client, selection, controller, pawn,
            stride, usedActivePawn) || !pawn) {
            AppendLog("Knife dry-run: pawn activo/de cuenta no disponible.");
            return;
        }

        int team = 0;
        uint8_t lifeState = 0xFF;
        if (!SafeRead(pawn + selection.teamNumberOffset, team)) return;
        if (selection.lifeStateOffset)
            (void)SafeRead(pawn + selection.lifeStateOffset, lifeState);

        const int targetDefinition = team == kTerroristTeam
            ? selection.terroristKnifeDefinition
            : team == kCounterTerroristTeam
                ? selection.counterTerroristKnifeDefinition : 0;
        const int targetPaintKit = team == kTerroristTeam
            ? selection.terroristKnifePaintKit
            : team == kCounterTerroristTeam
                ? selection.counterTerroristKnifePaintKit : 0;
        const int targetSeed = team == kTerroristTeam
            ? selection.terroristKnifeSeed
            : team == kCounterTerroristTeam
                ? selection.counterTerroristKnifeSeed : 0;

        uintptr_t entityList = 0;
        uintptr_t weaponServices = 0;
        uint32_t activeWeaponHandle = 0;
        uintptr_t activeWeapon = 0;
        if (client && selection.entityListOffset &&
            selection.weaponServicesOffset && selection.activeWeaponOffset &&
            SafeRead(reinterpret_cast<uintptr_t>(client) +
                selection.entityListOffset, entityList) && entityList &&
            SafeRead(pawn + selection.weaponServicesOffset, weaponServices) &&
            weaponServices &&
            SafeRead(weaponServices + selection.activeWeaponOffset,
                activeWeaponHandle) && activeWeaponHandle != 0 &&
            activeWeaponHandle != 0xFFFFFFFF) {
            activeWeapon = ReadEntityByIndex(entityList,
                static_cast<int>(activeWeaponHandle & 0x7FFF), stride);
        }

        uint16_t currentDefinition = 0;
        uint32_t currentSubclass = 0;
        int currentPaintKit = 0;
        int currentSeed = 0;
        float currentWear = 0.0f;
        char currentModel[260]{};
        uint32_t viewmodelHandle = 0;
        uintptr_t viewmodel = 0;
        char viewmodelModel[260]{};
        uint32_t hudArmsHandle = 0;
        uintptr_t hudArms = 0;
        int hudChildrenVisited = 0;
        if (activeWeapon) {
            const uintptr_t itemView = activeWeapon +
                selection.attributeManagerOffset + selection.itemOffset;
            if (selection.itemDefinitionIndexOffset)
                (void)SafeRead(itemView +
                    selection.itemDefinitionIndexOffset, currentDefinition);
            if (selection.subclassIdOffset)
                (void)SafeRead(activeWeapon + selection.subclassIdOffset,
                    currentSubclass);
            if (selection.fallbackPaintKitOffset)
                (void)SafeRead(activeWeapon +
                    selection.fallbackPaintKitOffset, currentPaintKit);
            if (selection.fallbackSeedOffset)
                (void)SafeRead(activeWeapon +
                    selection.fallbackSeedOffset, currentSeed);
            if (selection.fallbackWearOffset)
                (void)SafeRead(activeWeapon +
                    selection.fallbackWearOffset, currentWear);
            (void)ReadPawnModel(activeWeapon, selection, currentModel,
                _countof(currentModel));
            if (selection.viewmodelAttachmentOffset &&
                SafeRead(activeWeapon + selection.viewmodelAttachmentOffset,
                    viewmodelHandle) && viewmodelHandle != 0 &&
                viewmodelHandle != 0xFFFFFFFF) {
                viewmodel = ReadEntityByIndex(entityList,
                    static_cast<int>(viewmodelHandle & 0x7FFF), stride);
                if (viewmodel)
                    (void)ReadPawnModel(viewmodel, selection, viewmodelModel,
                        _countof(viewmodelModel));
            }
        }

        if (!viewmodel && activeWeapon && selection.hudModelArmsOffset &&
            selection.gameSceneNodeOffset && selection.sceneNodeOwnerOffset &&
            selection.sceneNodeChildOffset &&
            selection.sceneNodeNextSiblingOffset &&
            selection.ownerEntityOffset &&
            SafeRead(pawn + selection.hudModelArmsOffset, hudArmsHandle) &&
            hudArmsHandle != 0 && hudArmsHandle != 0xFFFFFFFF) {
            hudArms = ReadEntityByIndex(entityList,
                static_cast<int>(hudArmsHandle & 0x7FFF), stride);
            uintptr_t hudSceneNode = 0;
            uintptr_t child = 0;
            if (hudArms && SafeRead(hudArms +
                selection.gameSceneNodeOffset, hudSceneNode) && hudSceneNode)
                (void)SafeRead(hudSceneNode +
                    selection.sceneNodeChildOffset, child);

            while (child && hudChildrenVisited < 32) {
                ++hudChildrenVisited;
                uintptr_t childOwner = 0;
                uint32_t ownerHandle = 0;
                uintptr_t ownerWeapon = 0;
                if (SafeRead(child + selection.sceneNodeOwnerOffset,
                    childOwner) && childOwner &&
                    SafeRead(childOwner + selection.ownerEntityOffset,
                        ownerHandle) && ownerHandle != 0 &&
                    ownerHandle != 0xFFFFFFFF) {
                    ownerWeapon = ReadEntityByIndex(entityList,
                        static_cast<int>(ownerHandle & 0x7FFF), stride);
                }
                if (ownerWeapon == activeWeapon) {
                    viewmodel = childOwner;
                    (void)ReadPawnModel(viewmodel, selection, viewmodelModel,
                        _countof(viewmodelModel));
                    break;
                }

                uintptr_t next = 0;
                if (!SafeRead(child + selection.sceneNodeNextSiblingOffset,
                    next) || next == child)
                    break;
                child = next;
            }
        }

        char message[1536]{};
        StringCchPrintfA(message, _countof(message),
            "Knife dry-run: controller=0x%llX pawn=0x%llX stride=0x%llX "
            "team=%d life=%u services=0x%llX handle=0x%X weapon=0x%llX "
            "current_def=%u subclass=%u knife=%s paint=%d seed=%d wear=%.6f model=%s "
            "view_handle=0x%X viewmodel=0x%llX view_model=%s "
            "hud_handle=0x%X hud=0x%llX children=%d "
            "target_def=%d paint=%d seed=%d",
            static_cast<unsigned long long>(controller),
            static_cast<unsigned long long>(pawn),
            static_cast<unsigned long long>(stride), team,
            static_cast<unsigned>(lifeState),
            static_cast<unsigned long long>(weaponServices),
            activeWeaponHandle,
            static_cast<unsigned long long>(activeWeapon),
            static_cast<unsigned>(currentDefinition), currentSubclass,
            IsKnifeDefinition(currentDefinition) ? "yes" : "no",
            currentPaintKit, currentSeed, currentWear,
            currentModel[0] ? currentModel : "<unreadable>",
            viewmodelHandle, static_cast<unsigned long long>(viewmodel),
            viewmodelModel[0] ? viewmodelModel : "<unreadable>",
            hudArmsHandle, static_cast<unsigned long long>(hudArms),
            hudChildrenVisited,
            targetDefinition, targetPaintKit, targetSeed);
        if (usedActivePawn)
            StringCchCatA(message, _countof(message), " pawn_source=active");
        static char lastMessage[1536]{};
        if (strcmp(message, lastMessage) != 0) {
            AppendLog(message);
            StringCchCopyA(lastMessage, _countof(lastMessage), message);
        }
    }

    void LogModelControlResult(
        const char* action, uintptr_t pawn, const char* requested,
        const char* observed) {
        char message[720]{};
        StringCchPrintfA(message, _countof(message),
            "Agent model: %s pawn=0x%llX requested=%s observed=%s",
            action ? action : "unknown",
            static_cast<unsigned long long>(pawn),
            requested ? requested : "<none>",
            observed && observed[0] ? observed : "<unreadable>");
        AppendLog(message);
    }

    void ClearAppliedAgentState() {
        g_appliedAgent = {};
        InterlockedExchange(&g_agentModelApplied, 0);
    }

    bool AgentUsesFemaleVoice(int definitionIndex) {
        switch (definitionIndex) {
        case 4711: // Cmdr. Mae 'Dead Cold' Jamison
        case 4712: // 1st Lieutenant Farlow
        case 4727: // Safecracker Voltzmann
        case 4730: // Getaway Sally
        case 4756: // Lieutenant 'Tree Hugger' Farlow
        case 4757: // Cmdr. Davida 'Goggles' Fernandez
        case 4777: // Vypa Sista of the Revolution
        case 5308: // Special Agent Ava
            return true;
        default:
            return false;
        }
    }

    void RestoreAppliedAgent(
        const InventorySnapshotSelection& selection, const char* reason) {
        if (!g_appliedAgent.applied || !g_appliedAgent.pawn) {
            ClearAppliedAgentState();
            return;
        }

        char currentModel[260]{};
        const bool modelReadable = ReadPawnModel(g_appliedAgent.pawn,
            selection, currentModel, _countof(currentModel));
        bool modelRestored = !g_appliedAgent.modelApplied;
        if (g_appliedAgent.modelApplied && g_setModel && modelReadable &&
            strcmp(currentModel, g_appliedAgent.targetModel) == 0) {
            g_setModel(reinterpret_cast<void*>(g_appliedAgent.pawn),
                g_appliedAgent.originalModel);
            modelRestored = true;
        } else if (g_appliedAgent.modelApplied) {
            LogModelControlResult("restore-skipped-model-changed",
                g_appliedAgent.pawn, g_appliedAgent.originalModel,
                currentModel);
        }

        bool voiceRestored = !g_appliedAgent.voiceApplied;
        if (g_appliedAgent.voiceApplied && selection.hasFemaleVoiceOffset)
            voiceRestored = SafeWrite(g_appliedAgent.pawn +
                selection.hasFemaleVoiceOffset,
                g_appliedAgent.originalFemaleVoice);

        char observed[260]{};
        (void)ReadPawnModel(g_appliedAgent.pawn, selection, observed,
            _countof(observed));
        LogModelControlResult(modelRestored && voiceRestored
                ? (reason ? reason : "restored") : "restore-partial",
            g_appliedAgent.pawn, g_appliedAgent.originalModel, observed);
        ClearAppliedAgentState();
    }

    void RunAgentModelControl() {
        if (!g_setModel) return;
        const ULONGLONG now = GetTickCount64();
        if (now < g_appliedAgent.nextUpdateAt) return;
        g_appliedAgent.nextUpdateAt = now + kModelUpdateIntervalMs;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection)) {
            if (g_appliedAgent.applied)
                RestoreAppliedAgent(selection, "restored-ipc-unavailable");
            return;
        }
        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t controller = 0;
        uintptr_t pawn = 0;
        uintptr_t stride = 0;
        if (!ResolveAccountPawn(
            client, selection, controller, pawn, stride) || !pawn) {
            if (g_appliedAgent.applied)
                ClearAppliedAgentState();
            return;
        }

        int team = 0;
        uint8_t lifeState = 0xFF;
        if (!SafeRead(pawn + selection.teamNumberOffset, team) ||
            !selection.lifeStateOffset ||
            !SafeRead(pawn + selection.lifeStateOffset, lifeState))
            return;
        if (lifeState != 0) return;

        const int definitionIndex = team == kTerroristTeam
            ? selection.terroristDefinition
            : team == kCounterTerroristTeam
                ? selection.counterTerroristDefinition : 0;
        const char* targetModel = selection.enabled
            ? FindAgentModel(definitionIndex) : nullptr;
        const bool targetFemaleVoice = AgentUsesFemaleVoice(definitionIndex);

        if (g_appliedAgent.applied && pawn != g_appliedAgent.pawn) {
            LogModelControlResult("context-cleared-pawn-changed", pawn,
                targetModel, nullptr);
            ClearAppliedAgentState();
        }

        if (!targetModel) {
            if (g_appliedAgent.applied && pawn == g_appliedAgent.pawn)
                RestoreAppliedAgent(selection, "restored-disabled");
            return;
        }

        char currentModel[260]{};
        if (!ReadPawnModel(
            pawn, selection, currentModel, _countof(currentModel)))
            return;
        bool currentFemaleVoice = false;
        const bool voiceReadable = selection.hasFemaleVoiceOffset &&
            SafeRead(pawn + selection.hasFemaleVoiceOffset,
                currentFemaleVoice);

        if (!g_appliedAgent.applied) {
            const bool modelNeedsUpdate = strcmp(currentModel, targetModel) != 0;
            const bool voiceNeedsUpdate = voiceReadable &&
                currentFemaleVoice != targetFemaleVoice;
            if (!modelNeedsUpdate && !voiceNeedsUpdate) return;
            const ULONGLONG nextUpdateAt = g_appliedAgent.nextUpdateAt;
            g_appliedAgent = {};
            g_appliedAgent.nextUpdateAt = nextUpdateAt;
            g_appliedAgent.pawn = pawn;
            g_appliedAgent.team = team;
            g_appliedAgent.definitionIndex = definitionIndex;
            StringCchCopyA(g_appliedAgent.originalModel,
                _countof(g_appliedAgent.originalModel), currentModel);
            g_appliedAgent.originalFemaleVoice = currentFemaleVoice;
        } else if (team != g_appliedAgent.team) {
            RestoreAppliedAgent(selection, "restored-team-change");
            return;
        }

        if (g_appliedAgent.modelApplied &&
            strcmp(currentModel, g_appliedAgent.targetModel) != 0 &&
            strcmp(currentModel, g_appliedAgent.originalModel) != 0) {
            StringCchCopyA(g_appliedAgent.originalModel,
                _countof(g_appliedAgent.originalModel), currentModel);
        }

        StringCchCopyA(g_appliedAgent.targetModel,
            _countof(g_appliedAgent.targetModel), targetModel);
        g_appliedAgent.targetFemaleVoice = targetFemaleVoice;
        if (strcmp(currentModel, targetModel) != 0) {
            g_setModel(reinterpret_cast<void*>(pawn), targetModel);
            g_appliedAgent.modelApplied = true;
        }
        if (voiceReadable && currentFemaleVoice != targetFemaleVoice &&
            SafeWrite(pawn + selection.hasFemaleVoiceOffset,
                targetFemaleVoice))
            g_appliedAgent.voiceApplied = true;

        char observed[260]{};
        (void)ReadPawnModel(pawn, selection, observed, _countof(observed));
        bool observedFemaleVoice = currentFemaleVoice;
        if (voiceReadable)
            (void)SafeRead(pawn + selection.hasFemaleVoiceOffset,
                observedFemaleVoice);
        const bool modelMatches = strcmp(observed, targetModel) == 0;
        const bool voiceMatches = !voiceReadable ||
            observedFemaleVoice == targetFemaleVoice;
        const bool applied = (g_appliedAgent.modelApplied ||
            g_appliedAgent.voiceApplied) && modelMatches && voiceMatches;
        LogModelControlResult(applied ? "applied-model-and-voice" :
            "apply-model-or-voice-failed",
            pawn, targetModel, observed);
        g_appliedAgent.applied = applied;
        InterlockedExchange(&g_agentModelApplied, applied ? 1 : 0);
        if (!applied) ClearAppliedAgentState();
    }

    void ClearAppliedGloveState() {
        const ULONGLONG nextUpdateAt = g_appliedGloves.nextUpdateAt;
        g_appliedGloves = {};
        // Clearing a disabled/invalid state must not disable the poll throttle.
        g_appliedGloves.nextUpdateAt = nextUpdateAt;
        g_nativeReleasedGloveContext = {};
        g_nativeGloveReleaseObserved = false;
        g_lastGloveContext = {};
        g_gloveLifecycleState = GloveLifecycleState::WaitingForPawn;
        InterlockedExchange(&g_gloveModelApplied, 0);
    }

    bool HasRequiredGloveOffsets(
        const InventorySnapshotSelection& selection) {
        return selection.econGlovesOffset &&
            selection.needToReApplyGlovesOffset &&
            selection.econGlovesChangedOffset &&
            selection.itemDefinitionIndexOffset &&
            selection.entityQualityOffset && selection.itemIdOffset &&
            selection.itemIdHighOffset && selection.itemIdLowOffset &&
            selection.accountIdOffset && selection.initializedOffset;
    }

    void TriggerGloveReapply(
        uintptr_t pawn, const InventorySnapshotSelection& selection) {
        const bool reapply = true;
        (void)SafeWrite(pawn + selection.needToReApplyGlovesOffset, reapply);
    }

    bool WriteInventoryItemIdentity(
        uintptr_t itemView, int definitionIndex,
        const InventorySocacheItemIdentity& identity,
        const InventorySnapshotSelection& selection) {
        const uint16_t definition = static_cast<uint16_t>(definitionIndex);
        const int32_t quality = kUnusualItemQuality;
        const uint64_t itemId = identity.itemId;
        const uint32_t itemIdHigh = static_cast<uint32_t>(itemId >> 32);
        const uint32_t itemIdLow = static_cast<uint32_t>(itemId);
        const uint32_t accountId = identity.accountId;
        const uint8_t initialized = 1;
        return SafeWrite(itemView + selection.itemDefinitionIndexOffset,
                definition) &&
            SafeWrite(itemView + selection.entityQualityOffset, quality) &&
            SafeWrite(itemView + selection.itemIdOffset, itemId) &&
            SafeWrite(itemView + selection.itemIdHighOffset, itemIdHigh) &&
            SafeWrite(itemView + selection.itemIdLowOffset, itemIdLow) &&
            SafeWrite(itemView + selection.accountIdOffset, accountId) &&
            SafeWrite(itemView + selection.initializedOffset, initialized);
    }

    void RestoreAppliedGloves(
        const InventorySnapshotSelection& selection, const char* reason) {
        if (!g_appliedGloves.applied || !g_appliedGloves.pawn ||
            !HasRequiredGloveOffsets(selection)) {
            ClearAppliedGloveState();
            return;
        }
        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t controller = 0;
        uintptr_t currentPawn = 0;
        uintptr_t stride = 0;
        int currentTeam = 0;
        float currentSpawnTime = 0.0f;
        const bool pawnResolved = ResolveAccountPawn(client, selection,
            controller, currentPawn, stride) &&
            currentPawn == g_appliedGloves.pawn &&
            SafeRead(currentPawn + selection.teamNumberOffset, currentTeam) &&
            SafeRead(currentPawn + selection.lastSpawnTimeIndexOffset,
                currentSpawnTime);
        const float spawnDelta = currentSpawnTime -
            g_appliedGloves.spawnTimeIndex;
        const bool contextStillValid = pawnResolved &&
            currentTeam == g_appliedGloves.team &&
            spawnDelta < 0.001f && spawnDelta > -0.001f;
        if (!contextStillValid) {
            AppendLog("Gloves: stale context discarded; no restore was "
                "written to the previous pawn.");
            ClearAppliedGloveState();
            return;
        }

        const uintptr_t itemView = currentPawn +
            selection.econGlovesOffset;
        bool restored = ClearInventorySocacheItemView(itemView);
        if (!restored) {
            restored = SafeWrite(itemView +
                selection.itemDefinitionIndexOffset,
                g_appliedGloves.originalDefinition);
            restored = SafeWrite(itemView + selection.entityQualityOffset,
                g_appliedGloves.originalQuality) && restored;
            restored = SafeWrite(itemView + selection.itemIdOffset,
                g_appliedGloves.originalItemId) && restored;
            restored = SafeWrite(itemView + selection.itemIdHighOffset,
                g_appliedGloves.originalItemIdHigh) && restored;
            restored = SafeWrite(itemView + selection.itemIdLowOffset,
                g_appliedGloves.originalItemIdLow) && restored;
            restored = SafeWrite(itemView + selection.accountIdOffset,
                g_appliedGloves.originalAccountId) && restored;
            restored = SafeWrite(itemView + selection.initializedOffset,
                g_appliedGloves.originalInitialized) && restored;
        }
        (void)SafeWrite(currentPawn +
            selection.econGlovesChangedOffset,
            static_cast<uint8_t>(g_appliedGloves.originalChanged + 1));
        if (g_appliedGloves.bodyGroupApplied && g_setBodyGroup)
            g_setBodyGroup(reinterpret_cast<void*>(currentPawn), 0, 0);
        TriggerGloveReapply(currentPawn, selection);

        char message[256]{};
        StringCchPrintfA(message, _countof(message),
            "Gloves: %s restored=%s pawn=0x%llX def=%u.",
            reason ? reason : "restore", restored ? "yes" : "partial",
            static_cast<unsigned long long>(currentPawn),
            static_cast<unsigned int>(g_appliedGloves.originalDefinition));
        AppendLog(message);
        ClearAppliedGloveState();
    }

    bool EnsureMenuGloveInventory(
        const InventorySnapshotSelection& selection) {
        int team = 0;
        uint64_t localId = 0;
        int definitionIndex = 0;
        int paintKit = 0;
        int seed = 0;
        float wear = 0.15f;
        bool statTrak = false;
        int statTrakCount = 0;

        if (selection.counterTerroristGloves &&
            selection.counterTerroristGlovesDefinition > 0) {
            team = kCounterTerroristTeam;
            localId = selection.counterTerroristGloves;
            definitionIndex = selection.counterTerroristGlovesDefinition;
            paintKit = selection.counterTerroristGlovesPaintKit;
            seed = selection.counterTerroristGlovesSeed;
            wear = selection.counterTerroristGlovesWear;
            statTrak = selection.counterTerroristGlovesStatTrak;
            statTrakCount = selection.counterTerroristGlovesStatTrakCount;
        } else if (selection.terroristGloves &&
            selection.terroristGlovesDefinition > 0) {
            team = kTerroristTeam;
            localId = selection.terroristGloves;
            definitionIndex = selection.terroristGlovesDefinition;
            paintKit = selection.terroristGlovesPaintKit;
            seed = selection.terroristGlovesSeed;
            wear = selection.terroristGlovesWear;
            statTrak = selection.terroristGlovesStatTrak;
            statTrakCount = selection.terroristGlovesStatTrakCount;
        }

        if (!selection.enabled || !localId || definitionIndex <= 0) {
            return false;
        }

        InventorySocacheItemSpec itemSpec{};
        itemSpec.localId = localId;
        itemSpec.team = team;
        itemSpec.definitionIndex = definitionIndex;
        itemSpec.paintKit = paintKit;
        itemSpec.seed = seed;
        itemSpec.wear = wear;
        itemSpec.statTrak = statTrak;
        itemSpec.statTrakCount = statTrakCount;
        itemSpec.loadoutSlot = kGloveLoadoutSlot;
        itemSpec.itemViewItemIdOffset = selection.itemIdOffset;
        itemSpec.unacknowledged = IsPendingRevealItem(selection, localId);
        InventorySocacheItemIdentity identity{};
        return EnsureInventorySocacheItem(itemSpec, identity);
    }

    void RunGloveModelControl() {
        const ULONGLONG now = GetTickCount64();
        if (g_appliedGloves.reapplyFrames <= 0 &&
            now < g_appliedGloves.nextUpdateAt)
            return;
        if (g_appliedGloves.reapplyFrames <= 0)
            g_appliedGloves.nextUpdateAt = now + kModelUpdateIntervalMs;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection)) {
            if (g_appliedGloves.applied)
                RestoreAppliedGloves(selection, "ipc-unavailable");
            return;
        }
        if (!HasRequiredGloveOffsets(selection)) {
            if (g_appliedGloves.applied)
                RestoreAppliedGloves(selection, "offset-or-signature-missing");
            return;
        }

        const bool hasConfiguredGlove = selection.enabled &&
            ((selection.terroristGloves != 0 &&
                selection.terroristGlovesDefinition > 0) ||
             (selection.counterTerroristGloves != 0 &&
                selection.counterTerroristGlovesDefinition > 0));
        const bool hasPartialGloveState = g_appliedGloves.pawn != 0 ||
            g_appliedGloves.localId != 0 ||
            g_appliedGloves.generatedItemId != 0;
        if (hasConfiguredGlove && InterlockedCompareExchange(
                &g_nativeGloveLoadoutApplied, 0, 0) != 0) {
            if (g_appliedGloves.applied)
                RestoreAppliedGloves(selection, "native-loadout-active");
            else if (hasPartialGloveState)
                ClearAppliedGloveState();
            g_gloveLifecycleState = GloveLifecycleState::Applied;
            InterlockedExchange(&g_gloveModelApplied, 1);
            return;
        }
        if (!hasConfiguredGlove && !g_appliedGloves.applied &&
            !hasPartialGloveState) {
            g_gloveLifecycleState = GloveLifecycleState::Disabled;
            return;
        }

        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t controller = 0;
        uintptr_t pawn = 0;
        uintptr_t stride = 0;
        if (!ResolveAccountPawn(client, selection, controller, pawn, stride) ||
            !pawn) {
            // Panorama and SOCache remain available in the main menu even
            // though there is no live pawn to receive the visual model.
            g_appliedGloves = {};
            g_nativeReleasedGloveContext = {};
            g_nativeGloveReleaseObserved = false;
            g_lastGloveContext = {};
            g_gloveLifecycleState = GloveLifecycleState::WaitingForPawn;
            InterlockedExchange(&g_gloveModelApplied, 0);
            (void)EnsureMenuGloveInventory(selection);
            return;
        }

        int team = 0;
        uint8_t lifeState = 0xFF;
        float spawnTimeIndex = 0.0f;
        if (!SafeRead(pawn + selection.teamNumberOffset, team) ||
            !SafeRead(pawn + selection.lifeStateOffset, lifeState) ||
            lifeState != 0)
            return;
        if (selection.lastSpawnTimeIndexOffset)
            (void)SafeRead(pawn + selection.lastSpawnTimeIndexOffset,
                spawnTimeIndex);

        const uint64_t localId = team == kTerroristTeam
            ? selection.terroristGloves
            : team == kCounterTerroristTeam
                ? selection.counterTerroristGloves : 0;
        const int definitionIndex = team == kTerroristTeam
            ? selection.terroristGlovesDefinition
            : team == kCounterTerroristTeam
                ? selection.counterTerroristGlovesDefinition : 0;
        const int paintKit = team == kTerroristTeam
            ? selection.terroristGlovesPaintKit
            : team == kCounterTerroristTeam
                ? selection.counterTerroristGlovesPaintKit : 0;
        const int seed = team == kTerroristTeam
            ? selection.terroristGlovesSeed
            : team == kCounterTerroristTeam
                ? selection.counterTerroristGlovesSeed : 0;
        const float wear = team == kTerroristTeam
            ? selection.terroristGlovesWear
            : team == kCounterTerroristTeam
                ? selection.counterTerroristGlovesWear : 0.15f;
        const bool statTrak = team == kTerroristTeam
            ? selection.terroristGlovesStatTrak
            : team == kCounterTerroristTeam
                ? selection.counterTerroristGlovesStatTrak : false;
        const int statTrakCount = team == kTerroristTeam
            ? selection.terroristGlovesStatTrakCount
            : team == kCounterTerroristTeam
                ? selection.counterTerroristGlovesStatTrakCount : 0;
        const bool shouldApply = selection.enabled && localId &&
            definitionIndex > 0;

        const float wearDelta = wear - g_appliedGloves.wear;
        const float spawnDelta = spawnTimeIndex -
            g_appliedGloves.spawnTimeIndex;
        const bool pawnChanged = pawn != g_appliedGloves.pawn;
        const bool teamChanged = team != g_appliedGloves.team;
        const bool localIdChanged = localId != g_appliedGloves.localId;
        const bool definitionChanged = definitionIndex !=
            g_appliedGloves.definitionIndex;
        const bool paintChanged = paintKit != g_appliedGloves.paintKit;
        const bool seedChanged = seed != g_appliedGloves.seed;
        const bool wearChanged = wearDelta > 0.000001f ||
            wearDelta < -0.000001f;
        const bool statTrakChanged = statTrak !=
            g_appliedGloves.statTrak;
        const bool statTrakCountChanged = statTrakCount !=
            g_appliedGloves.statTrakCount;
        const bool spawnChanged = spawnDelta > 0.001f ||
            spawnDelta < -0.001f;
        const bool nonSpawnContextChanged = pawnChanged || teamChanged ||
            localIdChanged || definitionChanged || paintChanged || seedChanged ||
            wearChanged || statTrakChanged || statTrakCountChanged;
        const bool gloveContextChanged = nonSpawnContextChanged || spawnChanged;
        if (g_appliedGloves.applied && spawnChanged &&
            !nonSpawnContextChanged) {
            char message[192]{};
            StringCchPrintfA(message, _countof(message),
                "Gloves: new spawn %.6f->%.6f; visual state reset, "
                "SOCache preserved.",
                g_appliedGloves.spawnTimeIndex, spawnTimeIndex);
            AppendLog(message);
            const ULONGLONG nextUpdateAt = g_appliedGloves.nextUpdateAt;
            g_appliedGloves = {};
            g_appliedGloves.nextUpdateAt = nextUpdateAt;
            InterlockedExchange(&g_gloveModelApplied, 0);
        }
        if (g_appliedGloves.applied && gloveContextChanged) {
            char message[512]{};
            StringCchPrintfA(message, _countof(message),
                "Gloves: context delta pawn=%d team=%d local=%d def=%d "
                "paint=%d seed=%d wear=%d stattrak=%d count=%d spawn=%d "
                "spawn %.6f->%.6f wear %.6f->%.6f.",
                pawnChanged, teamChanged, localIdChanged, definitionChanged,
                paintChanged, seedChanged, wearChanged, statTrakChanged,
                statTrakCountChanged, spawnChanged,
                g_appliedGloves.spawnTimeIndex, spawnTimeIndex,
                g_appliedGloves.wear, wear);
            AppendLog(message);
            RestoreAppliedGloves(selection, "context-changed");
            return;
        }
        if (!shouldApply) {
            if (g_appliedGloves.applied)
                RestoreAppliedGloves(selection, "disabled");
            else if (hasPartialGloveState)
                ClearAppliedGloveState();
            return;
        }

        const uintptr_t itemView = pawn + selection.econGlovesOffset;
        if (!g_appliedGloves.applied) {
            const ULONGLONG nextUpdateAt = g_appliedGloves.nextUpdateAt;
            g_appliedGloves = {};
            g_appliedGloves.nextUpdateAt = nextUpdateAt;
            g_appliedGloves.pawn = pawn;
            g_appliedGloves.localId = localId;
            g_appliedGloves.team = team;
            g_appliedGloves.definitionIndex = definitionIndex;
            g_appliedGloves.paintKit = paintKit;
            g_appliedGloves.seed = seed;
            g_appliedGloves.wear = wear;
            g_appliedGloves.statTrak = statTrak;
            g_appliedGloves.statTrakCount = statTrakCount;
            g_appliedGloves.spawnTimeIndex = spawnTimeIndex;
            if (!SafeRead(itemView + selection.itemDefinitionIndexOffset,
                    g_appliedGloves.originalDefinition) ||
                !SafeRead(itemView + selection.entityQualityOffset,
                    g_appliedGloves.originalQuality) ||
                !SafeRead(itemView + selection.itemIdOffset,
                    g_appliedGloves.originalItemId) ||
                !SafeRead(itemView + selection.itemIdHighOffset,
                    g_appliedGloves.originalItemIdHigh) ||
                !SafeRead(itemView + selection.itemIdLowOffset,
                    g_appliedGloves.originalItemIdLow) ||
                !SafeRead(itemView + selection.accountIdOffset,
                    g_appliedGloves.originalAccountId) ||
                !SafeRead(itemView + selection.initializedOffset,
                    g_appliedGloves.originalInitialized) ||
                !SafeRead(pawn + selection.needToReApplyGlovesOffset,
                    g_appliedGloves.originalReapply) ||
                !SafeRead(pawn + selection.econGlovesChangedOffset,
                    g_appliedGloves.originalChanged)) {
                ClearAppliedGloveState();
                return;
            }
        }

        InventorySocacheItemSpec itemSpec{};
        itemSpec.localId = localId;
        itemSpec.team = team;
        itemSpec.definitionIndex = definitionIndex;
        itemSpec.paintKit = paintKit;
        itemSpec.seed = seed;
        itemSpec.wear = wear;
        itemSpec.statTrak = statTrak;
        itemSpec.statTrakCount = statTrakCount;
        itemSpec.loadoutSlot = kGloveLoadoutSlot;
        itemSpec.itemViewItemIdOffset = selection.itemIdOffset;
        itemSpec.unacknowledged = IsPendingRevealItem(selection, localId);
        InventorySocacheItemIdentity itemIdentity{};
        if (!EnsureInventorySocacheItem(itemSpec, itemIdentity)) {
            RestoreAppliedGloves(selection, "socache-item-failed");
            return;
        }
        g_appliedGloves.generatedItemId = itemIdentity.itemId;

        uint16_t observedDefinition = 0;
        uint64_t observedItemId = 0;
        (void)SafeRead(itemView + selection.itemDefinitionIndexOffset,
            observedDefinition);
        (void)SafeRead(itemView + selection.itemIdOffset, observedItemId);
        const bool firstApplication = !g_appliedGloves.applied;
        if (firstApplication ||
            observedDefinition != static_cast<uint16_t>(definitionIndex) ||
            observedItemId != itemIdentity.itemId) {
            // Keep the pawn-owned item view intact. Its identity links the
            // generated SOCache item without copying internal render context.
            if (!WriteInventoryItemIdentity(itemView, definitionIndex,
                    itemIdentity,
                    selection)) {
                TriggerGloveReapply(pawn, selection);
                RestoreAppliedGloves(selection, "identity-verify-failed");
                return;
            }
            const uint8_t changed = static_cast<uint8_t>(
                g_appliedGloves.originalChanged + 1);
            (void)SafeWrite(pawn + selection.econGlovesChangedOffset,
                changed);
            LogInventorySocacheItemViewDiagnostics(itemView,
                itemIdentity.loadoutItemView);
            g_appliedGloves.reapplyFrames = 3;
        }
        if (g_appliedGloves.reapplyFrames > 0) {
            const uint8_t initialized = 1;
            (void)SafeWrite(itemView + selection.initializedOffset,
                initialized);
            if (g_setBodyGroup) {
                g_setBodyGroup(reinterpret_cast<void*>(pawn), 0, 1);
                g_appliedGloves.bodyGroupApplied = true;
            }
            TriggerGloveReapply(pawn, selection);
            --g_appliedGloves.reapplyFrames;
            // The native glove path expects three consecutive frame-stage
            // updates; a millisecond throttle leaves provisional attachments
            // unowned at high frame rates.
            g_appliedGloves.nextUpdateAt = 0;
        }

        (void)SafeRead(itemView + selection.itemDefinitionIndexOffset,
            observedDefinition);
        g_appliedGloves.applied = observedDefinition ==
            static_cast<uint16_t>(definitionIndex);
        InterlockedExchange(&g_gloveModelApplied,
            g_appliedGloves.applied ? 1 : 0);
        if (firstApplication && g_appliedGloves.applied) {
            char message[320]{};
            StringCchPrintfA(message, _countof(message),
                "Gloves: applied pawn=0x%llX local=%llu def=%d paint=%d "
                "seed=%d wear=%.6f item=%llu (SOCache identity linked).",
                static_cast<unsigned long long>(pawn),
                static_cast<unsigned long long>(localId), definitionIndex,
                paintKit, seed, wear,
                static_cast<unsigned long long>(itemIdentity.itemId));
            AppendLog(message);
        }
        LogGloveVisualDiagnostics(client, pawn, stride, selection);
        if (!g_appliedGloves.applied) ClearAppliedGloveState();
    }

    bool ResolveActiveWeaponForPawn(
        HMODULE client, const InventorySnapshotSelection& selection,
        uintptr_t pawn, uintptr_t stride, uintptr_t& entityList,
        uint32_t& weaponHandle, uintptr_t& weapon) {
        entityList = 0;
        weaponHandle = 0;
        weapon = 0;
        uintptr_t weaponServices = 0;
        if (!client || !selection.entityListOffset ||
            !selection.weaponServicesOffset || !selection.activeWeaponOffset ||
            !SafeRead(reinterpret_cast<uintptr_t>(client) +
                selection.entityListOffset, entityList) || !entityList ||
            !SafeRead(pawn + selection.weaponServicesOffset, weaponServices) ||
            !weaponServices ||
            !SafeRead(weaponServices + selection.activeWeaponOffset,
                weaponHandle) || weaponHandle == 0 ||
            weaponHandle == 0xFFFFFFFF)
            return false;
        weapon = ReadEntityByIndex(entityList,
            static_cast<int>(weaponHandle & 0x7FFF), stride);
        return weapon != 0;
    }

    uintptr_t ResolveHudViewModelForWeapon(
        uintptr_t entityList, uintptr_t pawn, uintptr_t weapon,
        uintptr_t stride, const InventorySnapshotSelection& selection) {
        if (!entityList || !pawn || !weapon ||
            !selection.hudModelArmsOffset ||
            !selection.gameSceneNodeOffset ||
            !selection.sceneNodeOwnerOffset ||
            !selection.sceneNodeChildOffset ||
            !selection.sceneNodeNextSiblingOffset ||
            !selection.ownerEntityOffset)
            return 0;

        uint32_t hudArmsHandle = 0;
        if (!SafeRead(pawn + selection.hudModelArmsOffset, hudArmsHandle) ||
            hudArmsHandle == 0 || hudArmsHandle == 0xFFFFFFFF)
            return 0;
        const uintptr_t hudArms = ReadEntityByIndex(entityList,
            static_cast<int>(hudArmsHandle & 0x7FFF), stride);
        uintptr_t hudSceneNode = 0;
        uintptr_t child = 0;
        if (!hudArms || !SafeRead(hudArms +
            selection.gameSceneNodeOffset, hudSceneNode) || !hudSceneNode ||
            !SafeRead(hudSceneNode + selection.sceneNodeChildOffset, child))
            return 0;

        for (int visited = 0; child && visited < 32; ++visited) {
            uintptr_t childOwner = 0;
            uint32_t ownerHandle = 0;
            if (SafeRead(child + selection.sceneNodeOwnerOffset, childOwner) &&
                childOwner && SafeRead(childOwner +
                    selection.ownerEntityOffset, ownerHandle) &&
                ownerHandle != 0 && ownerHandle != 0xFFFFFFFF &&
                ReadEntityByIndex(entityList,
                    static_cast<int>(ownerHandle & 0x7FFF), stride) == weapon)
                return childOwner;

            uintptr_t next = 0;
            if (!SafeRead(child + selection.sceneNodeNextSiblingOffset, next) ||
                next == child)
                break;
            child = next;
        }
        return 0;
    }

    void LogKnifeModelControl(
        const char* action, uintptr_t weapon, uintptr_t viewmodel,
        const char* requested, const char* observedWeapon,
        const char* observedViewmodel) {
        char message[1200]{};
        StringCchPrintfA(message, _countof(message),
            "Knife model: %s weapon=0x%llX viewmodel=0x%llX requested=%s "
            "observed_weapon=%s observed_viewmodel=%s",
            action ? action : "unknown",
            static_cast<unsigned long long>(weapon),
            static_cast<unsigned long long>(viewmodel),
            requested ? requested : "<none>",
            observedWeapon && observedWeapon[0]
                ? observedWeapon : "<unreadable>",
            observedViewmodel && observedViewmodel[0]
                ? observedViewmodel : "<unreadable>");
        AppendLog(message);
    }

    void ClearAppliedKnifeState() {
        g_appliedKnife = {};
        InterlockedExchange(&g_knifeModelApplied, 0);
    }

    void ResetPendingKnife() {
        g_pendingKnifeWeapon = 0;
        g_pendingKnifeHandle = 0;
        g_pendingKnifeSince = 0;
    }

    bool IsKnifeEntityHandleValid(
        uintptr_t entityList, uintptr_t stride, uintptr_t weapon,
        uint32_t weaponHandle) {
        if (!entityList || !weapon || !weaponHandle ||
            weaponHandle == 0xFFFFFFFF)
            return false;
        const uintptr_t resolved = ReadEntityByIndex(entityList,
            static_cast<int>(weaponHandle & 0x7FFF), stride);
        // ReadEntityByIndex resolves the complete live handle supplied by the
        // weapon service. The old +0x10 check addressed a pre-update layout
        // field and rejected valid entities after the client schema shifted.
        return resolved == weapon;
    }

    bool HasAppliedKnifeState() {
        return g_appliedKnife.subclassApplied || g_appliedKnife.qualityApplied ||
            g_appliedKnife.finishApplied ||
            g_appliedKnife.itemIdHighApplied ||
            g_appliedKnife.weaponApplied || g_appliedKnife.viewmodelApplied;
    }

    bool SafeUpdateSubclass(uintptr_t weapon) {
        if (!weapon || !g_updateSubclass) return false;
        __try {
            g_updateSubclass(reinterpret_cast<void*>(weapon));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Knife subclass: excepcion al actualizar; operacion cancelada.");
            return false;
        }
    }

    bool SafeRefreshWeaponModules(
        uintptr_t weapon, uintptr_t itemView, bool& changed) {
        changed = false;
        if (!weapon || !itemView || !g_refreshWeaponModules) return false;
        __try {
            changed = g_refreshWeaponModules(
                reinterpret_cast<void*>(weapon),
                reinterpret_cast<void*>(itemView));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Knife modules: excepcion durante la reconstruccion.");
            return false;
        }
    }

    bool SafeSetMeshGroupMask(
        uintptr_t entity, const InventorySnapshotSelection& selection,
        uint64_t mask) {
        if (!entity || !g_setMeshGroupMask ||
            !selection.gameSceneNodeOffset)
            return false;
        uintptr_t sceneNode = 0;
        if (!SafeRead(entity + selection.gameSceneNodeOffset, sceneNode) ||
            !sceneNode)
            return false;
        __try {
            g_setMeshGroupMask(reinterpret_cast<void*>(sceneNode), mask);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Knife finish: excepcion al cambiar mesh group.");
            return false;
        }
    }

    bool SafeRefreshKnifeComposite(
        uintptr_t weapon, bool resetCustomMaterials = false) {
        using UpdateCompositeFn = void* (__fastcall*)(void*, bool);
        if (!weapon) return false;
        const UpdateCompositeFn primary =
            GetVirtualFunction<UpdateCompositeFn>(
                reinterpret_cast<void*>(weapon), 8);
        const UpdateCompositeFn secondary =
            GetVirtualFunction<UpdateCompositeFn>(
                reinterpret_cast<void*>(weapon), 111);
        if (!primary || !secondary) return false;

        MEMORY_BASIC_INFORMATION primaryInfo{};
        MEMORY_BASIC_INFORMATION secondaryInfo{};
        if (!VirtualQuery(reinterpret_cast<const void*>(primary),
                &primaryInfo, sizeof(primaryInfo)) ||
            !VirtualQuery(reinterpret_cast<const void*>(secondary),
                &secondaryInfo, sizeof(secondaryInfo)))
            return false;
        constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((primaryInfo.Protect & executable) == 0 ||
            (secondaryInfo.Protect & executable) == 0)
            return false;

        unsigned char secondaryCode[4]{};
        if (!SafeRead(reinterpret_cast<uintptr_t>(secondary),
                secondaryCode) ||
            (secondaryCode[0] == 0x48 && secondaryCode[1] == 0x8B &&
                secondaryCode[2] == 0xC1 && secondaryCode[3] == 0xC3)) {
            AppendLog("Knife finish: composite secundario invalido.");
            return false;
        }

        __try {
            (void)primary(reinterpret_cast<void*>(weapon), true);
            if (resetCustomMaterials) {
                constexpr uintptr_t kCustomMaterialOwnerOffset = 0x608;
                constexpr uintptr_t kFirstMaterialCountOffset = 0xAA8;
                constexpr uintptr_t kSecondMaterialCountOffset = 0xAC0;
                int firstMaterialCount = 0;
                int secondMaterialCount = 0;
                const bool countsReady = SafeRead(
                        weapon + kFirstMaterialCountOffset,
                        firstMaterialCount) &&
                    SafeRead(weapon + kSecondMaterialCountOffset,
                        secondMaterialCount);
                if (!g_clearWeaponCustomMaterials || !countsReady) {
                    AppendLog("Knife finish: invalidador de materiales no "
                        "disponible.");
                    return false;
                }
                if (firstMaterialCount > 0 || secondMaterialCount > 0) {
                    g_clearWeaponCustomMaterials(reinterpret_cast<void*>(
                        weapon + kCustomMaterialOwnerOffset), true);
                    AppendLog("Knife finish: composite visual anterior "
                        "liberado.");
                }
            }
            (void)secondary(reinterpret_cast<void*>(weapon), true);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Knife finish: excepcion al reconstruir composite.");
            return false;
        }
    }

    bool RefreshKnifeHud(uintptr_t itemView) {
        if (!itemView || !g_findHudElement || !g_clearHudWeaponIcon)
            return false;
        const uintptr_t emptyDescription = 0;
        if (!SafeWrite(itemView + 0x200, emptyDescription))
            return false;
        __try {
            void* hudSelection = g_findHudElement("HudWeaponSelection");
            if (!hudSelection) return false;
            const uintptr_t hud =
                reinterpret_cast<uintptr_t>(hudSelection) - 0x98;
            int weaponCount = 0;
            if (!SafeRead(hud + 0x50, weaponCount) || weaponCount <= 0 ||
                weaponCount > 64)
                return false;

            int index = 0;
            int cleared = 0;
            while (index < weaponCount && cleared < 64) {
                const int returnedIndex = g_clearHudWeaponIcon(
                    reinterpret_cast<void*>(hud), index, 0);
                ++cleared;
                const int nextIndex = returnedIndex + 1;
                index = nextIndex >= 0 && nextIndex <= weaponCount
                    ? nextIndex : index + 1;
                if (!SafeRead(hud + 0x50, weaponCount) || weaponCount < 0 ||
                    weaponCount > 64)
                    break;
            }
            char message[128]{};
            StringCchPrintfA(message, _countof(message),
                "Knife HUD: cache refrescada, entradas=%d.", cleared);
            AppendLog(message);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Knife HUD: excepcion al refrescar; operacion cancelada.");
            return false;
        }
    }

    void MaintainKnifeHud(uintptr_t itemView, ULONGLONG now) {
        if (!HasAppliedKnifeState() || g_appliedKnife.hudRefreshed ||
            now < g_appliedKnife.hudRefreshAt)
            return;
        ++g_appliedKnife.hudRefreshAttempts;
        g_appliedKnife.hudRefreshed = RefreshKnifeHud(itemView);
        if (!g_appliedKnife.hudRefreshed) {
            g_appliedKnife.hudRefreshAt = now + 250;
            if (g_appliedKnife.hudRefreshAttempts >= 5) {
                g_appliedKnife.hudRefreshed = true;
                AppendLog("Knife HUD: refresco no disponible.");
            }
        }
    }

    void LogKnifeIdentity(
        const char* action, uintptr_t weapon, uint16_t definition,
        uint32_t subclassId) {
        char message[256]{};
        StringCchPrintfA(message, _countof(message),
            "Knife identity: %s weapon=0x%llX definition=%hu subclass=%lu",
            action ? action : "unknown",
            static_cast<unsigned long long>(weapon), definition,
            static_cast<unsigned long>(subclassId));
        AppendLog(message);
    }

    bool ReadKnifeFinish(
        uintptr_t weapon, uintptr_t itemView,
        const InventorySnapshotSelection& selection, int& paintKit, int& seed,
        float& wear, int& statTrak, uint32_t& itemIdHigh) {
        return weapon && itemView && selection.fallbackPaintKitOffset &&
            selection.fallbackSeedOffset && selection.fallbackWearOffset &&
            selection.fallbackStatTrakOffset && selection.itemIdHighOffset &&
            SafeRead(weapon + selection.fallbackPaintKitOffset, paintKit) &&
            SafeRead(weapon + selection.fallbackSeedOffset, seed) &&
            SafeRead(weapon + selection.fallbackWearOffset, wear) &&
            SafeRead(weapon + selection.fallbackStatTrakOffset, statTrak) &&
            SafeRead(itemView + selection.itemIdHighOffset, itemIdHigh);
    }

    bool WriteKnifeFinish(
        uintptr_t weapon, uintptr_t itemView,
        const InventorySnapshotSelection& selection, int paintKit, int seed,
        float wear, int statTrak, uint32_t itemIdHigh) {
        return weapon && itemView && selection.fallbackPaintKitOffset &&
            selection.fallbackSeedOffset && selection.fallbackWearOffset &&
            selection.fallbackStatTrakOffset && selection.itemIdHighOffset &&
            SafeWrite(weapon + selection.fallbackPaintKitOffset, paintKit) &&
            SafeWrite(weapon + selection.fallbackSeedOffset, seed) &&
            SafeWrite(weapon + selection.fallbackWearOffset, wear) &&
            SafeWrite(weapon + selection.fallbackStatTrakOffset, statTrak) &&
            SafeWrite(itemView + selection.itemIdHighOffset, itemIdHigh);
    }

    void RestoreAppliedKnife(
        const InventorySnapshotSelection& selection, const char* reason) {
        if (!HasAppliedKnifeState()) {
            ClearAppliedKnifeState();
            g_fastKnifeReapplyUntil = 0;
            return;
        }

        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t entityList = 0;
        const uintptr_t stride = g_appliedKnife.entityStride;
        if (!client || !selection.entityListOffset ||
            !SafeRead(reinterpret_cast<uintptr_t>(client) +
                selection.entityListOffset, entityList) ||
            !IsKnifeEntityHandleValid(entityList, stride,
                g_appliedKnife.weapon, g_appliedKnife.weaponHandle)) {
            AppendLog("Knife identity: restauracion omitida; entidad expirada.");
            ClearAppliedKnifeState();
            ResetPendingKnife();
            g_fastKnifeReapplyUntil = 0;
            return;
        }

        char weaponModel[260]{};
        char viewmodelModel[260]{};
        bool identityRestored = true;
        bool identityRestoreAttempted = false;
        const uintptr_t itemView = g_appliedKnife.weapon +
            selection.attributeManagerOffset + selection.itemOffset;
        if (g_appliedKnife.subclassApplied && g_appliedKnife.weapon &&
            selection.attributeManagerOffset && selection.itemOffset &&
            selection.itemDefinitionIndexOffset &&
            selection.subclassIdOffset) {
            identityRestoreAttempted = true;
            const bool definitionWritten = SafeWrite(itemView +
                selection.itemDefinitionIndexOffset,
                g_appliedKnife.originalDefinition);
            const bool subclassWritten = SafeWrite(g_appliedKnife.weapon +
                selection.subclassIdOffset, g_appliedKnife.originalSubclass);
            identityRestored = definitionWritten && subclassWritten;
        }
        if (g_appliedKnife.qualityApplied && g_appliedKnife.weapon &&
            selection.attributeManagerOffset && selection.itemOffset &&
            selection.entityQualityOffset) {
            identityRestoreAttempted = true;
            identityRestored = SafeWrite(itemView +
                selection.entityQualityOffset,
                g_appliedKnife.originalQuality) && identityRestored;
        }
        if (g_appliedKnife.finishApplied ||
            g_appliedKnife.itemIdHighApplied) {
            identityRestoreAttempted = true;
            identityRestored = WriteKnifeFinish(
                g_appliedKnife.weapon, itemView, selection,
                g_appliedKnife.originalPaintKit,
                g_appliedKnife.originalSeed,
                g_appliedKnife.originalWear,
                g_appliedKnife.originalStatTrak,
                g_appliedKnife.originalItemIdHigh) && identityRestored;
        }
        if (g_appliedKnife.itemIdHighApplied) {
            identityRestoreAttempted = true;
            identityRestored = SafeWrite(itemView + selection.itemIdOffset,
                    g_appliedKnife.originalItemId) && identityRestored;
            identityRestored = SafeWrite(itemView +
                    selection.itemIdHighOffset,
                    g_appliedKnife.originalItemIdHigh) && identityRestored;
            identityRestored = SafeWrite(itemView + selection.itemIdLowOffset,
                    g_appliedKnife.originalItemIdLow) && identityRestored;
            identityRestored = SafeWrite(itemView + selection.accountIdOffset,
                    g_appliedKnife.originalAccountId) && identityRestored;
            identityRestored = SafeWrite(itemView + selection.initializedOffset,
                    g_appliedKnife.originalInitialized) && identityRestored;
        }
        if (g_appliedKnife.subclassApplied)
            identityRestored = SafeUpdateSubclass(g_appliedKnife.weapon) &&
                identityRestored;
        if (identityRestoreAttempted) {
            identityRestored = SafeRefreshKnifeComposite(
                g_appliedKnife.weapon, true) && identityRestored;
        }
        if (identityRestoreAttempted) {
            LogKnifeIdentity(identityRestored ? "restored" : "restore-failed",
                g_appliedKnife.weapon, g_appliedKnife.originalDefinition,
                g_appliedKnife.originalSubclass);
        }

        // The current controlled pawn may already have changed. Restore the
        // viewmodel captured with the weapon instead of resolving through it.
        uintptr_t restoredViewmodel = g_appliedKnife.viewmodel;

        if (g_setModel && g_appliedKnife.weapon &&
            ReadPawnModel(g_appliedKnife.weapon, selection, weaponModel,
                _countof(weaponModel)) &&
            strcmp(weaponModel, g_appliedKnife.targetModel) == 0) {
            g_setModel(reinterpret_cast<void*>(g_appliedKnife.weapon),
                g_appliedKnife.originalWeaponModel);
            (void)ReadPawnModel(g_appliedKnife.weapon, selection, weaponModel,
                _countof(weaponModel));
        }
        if (g_setModel && restoredViewmodel &&
            ReadPawnModel(restoredViewmodel, selection, viewmodelModel,
                _countof(viewmodelModel)) &&
            strcmp(viewmodelModel, g_appliedKnife.targetModel) == 0) {
            g_setModel(reinterpret_cast<void*>(restoredViewmodel),
                g_appliedKnife.originalViewmodelModel);
            (void)ReadPawnModel(restoredViewmodel, selection,
                viewmodelModel, _countof(viewmodelModel));
        }
        LogKnifeModelControl(reason ? reason : "restored",
            g_appliedKnife.weapon, restoredViewmodel,
            g_appliedKnife.originalWeaponModel, weaponModel, viewmodelModel);
        ClearAppliedKnifeState();
        ResetPendingKnife();
        g_fastKnifeReapplyUntil = 0;
    }

    void QueueNativeLoadoutCommand(const NativeLoadoutCommand& command) {
        if (command.type == NativeLoadoutCommandType::None ||
            command.localId == 0 || command.team < 0 || command.team > 2)
            return;
        AcquireSRWLockExclusive(&g_nativeLoadoutCommandLock);
        g_pendingNativeLoadoutCommands[command.team] = command;
        ReleaseSRWLockExclusive(&g_nativeLoadoutCommandLock);
    }

    bool ConsumeNativeLoadoutCommand(NativeLoadoutCommand& command) {
        command = {};
        AcquireSRWLockExclusive(&g_nativeLoadoutCommandLock);
        for (NativeLoadoutCommand& pending :
            g_pendingNativeLoadoutCommands) {
            if (pending.type == NativeLoadoutCommandType::None) continue;
            command = pending;
            pending = {};
            break;
        }
        ReleaseSRWLockExclusive(&g_nativeLoadoutCommandLock);
        return command.type != NativeLoadoutCommandType::None;
    }

    void RequeueNativeLoadoutCommand(const NativeLoadoutCommand& command) {
        if (command.type == NativeLoadoutCommandType::None ||
            command.localId == 0 || command.team < 0 || command.team > 2)
            return;
        AcquireSRWLockExclusive(&g_nativeLoadoutCommandLock);
        NativeLoadoutCommand& pending =
            g_pendingNativeLoadoutCommands[command.team];
        if (pending.type ==
            NativeLoadoutCommandType::None)
            pending = command;
        ReleaseSRWLockExclusive(&g_nativeLoadoutCommandLock);
    }

    void QueueStatTrakCommand(uint64_t localId, int count) {
        if (localId == 0 || count < 0) return;
        AcquireSRWLockExclusive(&g_statTrakCommandLock);
        StatTrakCommand* freeCommand = nullptr;
        for (StatTrakCommand& pending : g_pendingStatTrakCommands) {
            if (pending.localId == localId) {
                if (count > pending.count) pending.count = count;
                ReleaseSRWLockExclusive(&g_statTrakCommandLock);
                return;
            }
            if (!freeCommand && pending.localId == 0)
                freeCommand = &pending;
        }
        if (freeCommand) *freeCommand = { localId, count };
        ReleaseSRWLockExclusive(&g_statTrakCommandLock);
    }

    void QueueStatTrakIncrement(uint64_t localId, int currentCount) {
        if (localId == 0 || currentCount < 0) return;
        AcquireSRWLockExclusive(&g_statTrakCommandLock);
        StatTrakCommand* freeCommand = nullptr;
        for (StatTrakCommand& pending : g_pendingStatTrakCommands) {
            if (pending.localId == localId) {
                const int baseCount = pending.count > currentCount
                    ? pending.count : currentCount;
                pending.count = baseCount + 1;
                ReleaseSRWLockExclusive(&g_statTrakCommandLock);
                return;
            }
            if (!freeCommand && pending.localId == 0)
                freeCommand = &pending;
        }
        if (freeCommand) *freeCommand = { localId, currentCount + 1 };
        ReleaseSRWLockExclusive(&g_statTrakCommandLock);
    }

    bool ConsumeStatTrakCommand(StatTrakCommand& command) {
        command = {};
        AcquireSRWLockExclusive(&g_statTrakCommandLock);
        for (StatTrakCommand& pending : g_pendingStatTrakCommands) {
            if (pending.localId == 0) continue;
            command = pending;
            pending = {};
            break;
        }
        ReleaseSRWLockExclusive(&g_statTrakCommandLock);
        return command.localId != 0;
    }

    void RequeueStatTrakCommand(const StatTrakCommand& command) {
        QueueStatTrakCommand(command.localId, command.count);
    }

    void QueueMusicKitMvpObservation(uint64_t localId, int baseCount) {
        if (localId == 0 || baseCount < 0) return;
        PendingMusicKitMvpObservation observation{};
        observation.localId = localId;
        observation.baseCount = baseCount;
        // The game updates the controller shortly after dispatching round_mvp.
        observation.observeAfter = GetTickCount64() + 250;
        AcquireSRWLockExclusive(&g_musicKitMvpObservationLock);
        g_pendingMusicKitMvpObservation = observation;
        ReleaseSRWLockExclusive(&g_musicKitMvpObservationLock);
    }

    bool ReadMusicKitMvpObservation(
        PendingMusicKitMvpObservation& observation) {
        AcquireSRWLockShared(&g_musicKitMvpObservationLock);
        observation = g_pendingMusicKitMvpObservation;
        ReleaseSRWLockShared(&g_musicKitMvpObservationLock);
        return observation.localId != 0;
    }

    void ClearMusicKitMvpObservation(uint64_t expectedLocalId = 0) {
        AcquireSRWLockExclusive(&g_musicKitMvpObservationLock);
        if (expectedLocalId == 0 ||
            g_pendingMusicKitMvpObservation.localId == expectedLocalId)
            g_pendingMusicKitMvpObservation = {};
        ReleaseSRWLockExclusive(&g_musicKitMvpObservationLock);
    }

    void RunNativeInventoryLoadoutControl() {
        struct ObservedLoadout {
            bool initialized = false;
            uint64_t itemId = 0;
        };
        static ObservedLoadout observed[2]{};
        static ObservedLoadout observedMusicKit{};
        static LONG observedEpoch = -1;
        static ULONGLONG nextPollAt = 0;

        const ULONGLONG now = GetTickCount64();
        const LONG currentEpoch = InterlockedCompareExchange(
            &g_nativeLoadoutObservationEpoch, 0, 0);
        if (observedEpoch != currentEpoch) {
            observed[0] = {};
            observed[1] = {};
            observedMusicKit = {};
            observedEpoch = currentEpoch;
        }
        if (InterlockedCompareExchange(
                &g_inventoryCollectionSyncing, 0, 0) != 0 ||
            now < g_nativeLoadoutObserveAfter.load())
            return;
        if (now < nextPollAt) return;
        nextPollAt = now + 100;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection) ||
            !selection.enabled || !selection.itemIdOffset) {
            observed[0] = {};
            observed[1] = {};
            observedMusicKit = {};
            return;
        }

        const uint64_t desiredMusicKitLocalId = selection.musicKit;
        uint64_t desiredMusicKitItemId = 0;
        const bool desiredMusicKitReady = desiredMusicKitLocalId == 0 ||
            ResolveInventorySocacheGeneratedItemId(
                desiredMusicKitLocalId, desiredMusicKitItemId);
        InventorySocacheLoadoutSelection nativeMusicKit{};
        if (ReadInventorySocacheLoadoutSelection(0,
                kMusicKitLoadoutSlot, selection.itemIdOffset,
                nativeMusicKit)) {
            if (!observedMusicKit.initialized) {
                observedMusicKit.initialized = true;
                observedMusicKit.itemId = nativeMusicKit.itemId;
                if (desiredMusicKitLocalId != 0 && desiredMusicKitReady &&
                    nativeMusicKit.itemId != desiredMusicKitItemId) {
                    AppendLog(
                        "Native Music Kit: baseline distinta; se conserva el estado del Overlay.");
                }
            } else if (nativeMusicKit.itemId != observedMusicKit.itemId) {
                const uint64_t previousItemId = observedMusicKit.itemId;
                observedMusicKit.itemId = nativeMusicKit.itemId;
                if (desiredMusicKitReady) {
                    NativeLoadoutCommand command{};
                    if (nativeMusicKit.generated &&
                        nativeMusicKit.localId != desiredMusicKitLocalId) {
                        command.type = NativeLoadoutCommandType::Equip;
                        command.localId = nativeMusicKit.localId;
                        command.team = 0;
                    } else if (!nativeMusicKit.generated &&
                        desiredMusicKitLocalId != 0 &&
                        nativeMusicKit.itemId != desiredMusicKitItemId) {
                        command.type = NativeLoadoutCommandType::Unequip;
                        command.localId = desiredMusicKitLocalId;
                        command.team = 0;
                    }
                    if (command.type != NativeLoadoutCommandType::None) {
                        QueueNativeLoadoutCommand(command);
                        char message[256]{};
                        StringCchPrintfA(message, _countof(message),
                            "Native Music Kit: %s queued local=%llu previous=%llu current=%llu.",
                            command.type == NativeLoadoutCommandType::Equip
                                ? "equip" : "unequip",
                            static_cast<unsigned long long>(command.localId),
                            static_cast<unsigned long long>(previousItemId),
                            static_cast<unsigned long long>(
                                nativeMusicKit.itemId));
                        AppendLog(message);
                    }
                }
            }
        }

        constexpr int gameTeams[2] = {
            kTerroristTeam, kCounterTerroristTeam
        };
        for (int index = 0; index < 2; ++index) {
            const int gameTeam = gameTeams[index];
            const int protocolTeam = index + 1;
            const uint64_t desiredLocalId = index == 0
                ? selection.terroristKnife
                : selection.counterTerroristKnife;
            uint64_t desiredItemId = 0;
            const bool desiredReady = desiredLocalId == 0 ||
                ResolveInventorySocacheGeneratedItemId(
                    desiredLocalId, desiredItemId);

            InventorySocacheLoadoutSelection nativeSelection{};
            if (!ReadInventorySocacheLoadoutSelection(gameTeam,
                    kKnifeLoadoutSlot, selection.itemIdOffset,
                    nativeSelection))
                continue;
            if (!observed[index].initialized) {
                observed[index].initialized = true;
                observed[index].itemId = nativeSelection.itemId;
                if (desiredLocalId != 0 && desiredReady &&
                    nativeSelection.itemId != desiredItemId) {
                    InterlockedExchange(
                        &g_inventoryLoadoutReapplyRequested, 1);
                    AppendLog("Native loadout: baseline tardia distinta; "
                        "se conservara el estado del Overlay.");
                }
                continue;
            } else if (nativeSelection.itemId == observed[index].itemId) {
                continue;
            }
            const uint64_t previousItemId = observed[index].itemId;
            observed[index].itemId = nativeSelection.itemId;
            if (!desiredReady) continue;

            NativeLoadoutCommand command{};
            if (nativeSelection.generated &&
                nativeSelection.localId != desiredLocalId) {
                command.type = NativeLoadoutCommandType::Equip;
                command.localId = nativeSelection.localId;
                command.team = protocolTeam;
            } else if (!nativeSelection.generated && desiredLocalId != 0 &&
                nativeSelection.itemId != desiredItemId) {
                command.type = NativeLoadoutCommandType::Unequip;
                command.localId = desiredLocalId;
                command.team = protocolTeam;
            }
            if (command.type == NativeLoadoutCommandType::None) continue;

            QueueNativeLoadoutCommand(command);
            char message[256]{};
            StringCchPrintfA(message, _countof(message),
                "Native loadout: %s queued team=%d local=%llu "
                "previous=%llu current=%llu.",
                command.type == NativeLoadoutCommandType::Equip
                    ? "equip" : "unequip",
                protocolTeam,
                static_cast<unsigned long long>(command.localId),
                static_cast<unsigned long long>(previousItemId),
                static_cast<unsigned long long>(nativeSelection.itemId));
            AppendLog(message);
        }
    }

    bool ApplyPublishedKnifeLoadoutToSocache(
        const InventorySnapshotSelection& selection) {
        if (!selection.enabled || !selection.itemIdOffset) return true;

        const uint64_t localIds[2] = {
            selection.terroristKnife,
            selection.counterTerroristKnife
        };
        const int gameTeams[2] = {
            kTerroristTeam,
            kCounterTerroristTeam
        };
        bool applied = true;
        for (int teamIndex = 0; teamIndex < 2; ++teamIndex) {
            const uint64_t localId = localIds[teamIndex];
            if (localId == 0) continue;

            const InventorySnapshotSelection::KnifeCollectionItem* item = nullptr;
            for (int itemIndex = 0;
                itemIndex < selection.knifeItemCount; ++itemIndex) {
                if (selection.knifeItems[itemIndex].localId == localId) {
                    item = &selection.knifeItems[itemIndex];
                    break;
                }
            }
            if (!item) {
                applied = false;
                continue;
            }

            InventorySocacheItemSpec spec{};
            spec.localId = item->localId;
            spec.team = gameTeams[teamIndex];
            spec.definitionIndex = item->definitionIndex;
            spec.paintKit = item->paintKit;
            spec.seed = item->seed;
            spec.wear = item->wear;
            spec.statTrak = item->statTrak;
            spec.statTrakCount = item->statTrakCount;
            spec.loadoutSlot = kKnifeLoadoutSlot;
            spec.itemViewItemIdOffset = selection.itemIdOffset;
            spec.unacknowledged = IsPendingRevealItem(
                selection, item->localId);
            spec.equip = true;
            InventorySocacheItemIdentity identity{};
            applied = EnsureInventorySocacheItem(spec, identity) && applied;
        }
        return applied;
    }

    bool ApplyPublishedGloveLoadoutToSocache(
        const InventorySnapshotSelection& selection) {
        if (!selection.itemIdOffset) {
            InterlockedExchange(&g_nativeGloveLoadoutApplied, 0);
            return false;
        }

        // Rebuild both team overrides as one transaction so an unequipped
        // side cannot retain a stale generated item.
        RestoreInventorySocacheLoadoutSlot(
            kGloveLoadoutSlot, "glove-loadout-refresh");
        if (!selection.enabled) {
            InterlockedExchange(&g_nativeGloveLoadoutApplied, 0);
            return true;
        }

        const uint64_t localIds[2] = {
            selection.terroristGloves,
            selection.counterTerroristGloves
        };
        const int gameTeams[2] = {
            kTerroristTeam,
            kCounterTerroristTeam
        };
        bool applied = true;
        bool hasSelection = false;
        for (int teamIndex = 0; teamIndex < 2; ++teamIndex) {
            const uint64_t localId = localIds[teamIndex];
            if (localId == 0) continue;
            hasSelection = true;

            const InventorySnapshotSelection::GloveCollectionItem* item =
                nullptr;
            for (int itemIndex = 0;
                itemIndex < selection.gloveItemCount; ++itemIndex) {
                if (selection.gloveItems[itemIndex].localId == localId) {
                    item = &selection.gloveItems[itemIndex];
                    break;
                }
            }
            if (!item) {
                applied = false;
                continue;
            }

            InventorySocacheItemSpec spec{};
            spec.localId = item->localId;
            spec.team = gameTeams[teamIndex];
            spec.definitionIndex = item->definitionIndex;
            spec.paintKit = item->paintKit;
            spec.seed = item->seed;
            spec.wear = item->wear;
            spec.statTrak = item->statTrak;
            spec.statTrakCount = item->statTrakCount;
            spec.loadoutSlot = kGloveLoadoutSlot;
            spec.itemViewItemIdOffset = selection.itemIdOffset;
            spec.unacknowledged = IsPendingRevealItem(
                selection, item->localId);
            spec.equip = true;
            InventorySocacheItemIdentity identity{};
            applied = EnsureInventorySocacheItem(spec, identity) && applied;
        }
        InterlockedExchange(&g_nativeGloveLoadoutApplied,
            applied && hasSelection ? 1 : 0);
        return applied;
    }

    bool ResolveMusicKitRuntimeContext(
        const InventorySnapshotSelection& selection,
        uintptr_t& controller, uintptr_t& inventoryServices) {
        controller = 0;
        inventoryServices = 0;
        if (!selection.localPlayerControllerOffset ||
            !selection.inventoryServicesOffset ||
            !selection.serviceMusicIdOffset ||
            !selection.controllerMusicKitIdOffset ||
            !selection.controllerMusicKitMvpsOffset)
            return false;

        HMODULE client = GetModuleHandleW(L"client.dll");
        if (!client || !SafeRead(reinterpret_cast<uintptr_t>(client) +
                selection.localPlayerControllerOffset, controller) ||
            !controller)
            return false;
        return SafeRead(controller + selection.inventoryServicesOffset,
            inventoryServices) && inventoryServices != 0;
    }

    void RestoreMusicKitRuntimeControl() {
        if (!g_appliedMusicKit.hasOriginal) {
            g_appliedMusicKit = {};
            return;
        }

        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t controller = 0;
        uintptr_t inventoryServices = 0;
        const bool sameContext = client &&
            g_appliedMusicKit.localPlayerControllerOffset != 0 &&
            g_appliedMusicKit.inventoryServicesOffset != 0 &&
            SafeRead(reinterpret_cast<uintptr_t>(client) +
                    g_appliedMusicKit.localPlayerControllerOffset,
                controller) && controller != 0 &&
            SafeRead(controller + g_appliedMusicKit.inventoryServicesOffset,
                inventoryServices) && inventoryServices != 0 &&
            controller == g_appliedMusicKit.controller &&
            inventoryServices == g_appliedMusicKit.inventoryServices;
        if (sameContext) {
            (void)SafeWrite(controller +
                    g_appliedMusicKit.controllerMusicKitIdOffset,
                g_appliedMusicKit.originalControllerMusicKitId);
            (void)SafeWrite(controller +
                    g_appliedMusicKit.controllerMusicKitMvpsOffset,
                g_appliedMusicKit.originalControllerMusicKitMvps);
            (void)SafeWrite(inventoryServices +
                    g_appliedMusicKit.serviceMusicIdOffset,
                g_appliedMusicKit.originalServiceMusicId);
            AppendLog("Music Kit runtime: estado original restaurado.");
        } else {
            AppendLog(
                "Music Kit runtime: contexto anterior descartado sin escribir.");
        }
        g_appliedMusicKit = {};
    }

    void RunMusicKitRuntimeControl() {
        constexpr ULONGLONG kRuntimeCheckIntervalMs = 100;
        static ULONGLONG nextControlAt = 0;
        const ULONGLONG now = GetTickCount64();
        if (now < nextControlAt) return;
        nextControlAt = now + kRuntimeCheckIntervalMs;

        InventorySnapshotSelection selection{};
        if (!ReadPublishedInventorySelection(selection) ||
            !selection.enabled || selection.musicKit == 0 ||
            selection.musicKitDefinition <= 0 ||
            selection.musicKitDefinition > 0xFFFF) {
            ClearMusicKitMvpObservation();
            RestoreMusicKitRuntimeControl();
            return;
        }

        uintptr_t controller = 0;
        uintptr_t inventoryServices = 0;
        if (!ResolveMusicKitRuntimeContext(selection, controller,
                inventoryServices))
            return;

        const bool changedContext = g_appliedMusicKit.controller != controller ||
            g_appliedMusicKit.inventoryServices != inventoryServices;
        if (changedContext) {
            // Entity addresses from a previous map/account context are never
            // reused for restoration.
            g_appliedMusicKit = {};
            g_appliedMusicKit.controller = controller;
            g_appliedMusicKit.inventoryServices = inventoryServices;
            g_appliedMusicKit.localPlayerControllerOffset =
                selection.localPlayerControllerOffset;
            g_appliedMusicKit.inventoryServicesOffset =
                selection.inventoryServicesOffset;
            g_appliedMusicKit.controllerMusicKitIdOffset =
                selection.controllerMusicKitIdOffset;
            g_appliedMusicKit.controllerMusicKitMvpsOffset =
                selection.controllerMusicKitMvpsOffset;
            g_appliedMusicKit.serviceMusicIdOffset =
                selection.serviceMusicIdOffset;
            if (!SafeRead(controller + selection.controllerMusicKitIdOffset,
                    g_appliedMusicKit.originalControllerMusicKitId) ||
                !SafeRead(controller + selection.controllerMusicKitMvpsOffset,
                    g_appliedMusicKit.originalControllerMusicKitMvps) ||
                !SafeRead(inventoryServices + selection.serviceMusicIdOffset,
                    g_appliedMusicKit.originalServiceMusicId)) {
                g_appliedMusicKit = {};
                return;
            }
            g_appliedMusicKit.hasOriginal = true;
        }

        int currentControllerId = 0;
        int currentControllerMvps = 0;
        uint16_t currentServiceId = 0;
        if (!SafeRead(controller + selection.controllerMusicKitIdOffset,
                currentControllerId) ||
            !SafeRead(controller + selection.controllerMusicKitMvpsOffset,
                currentControllerMvps) ||
            !SafeRead(inventoryServices + selection.serviceMusicIdOffset,
                currentServiceId))
            return;

        const int targetDefinition = selection.musicKitDefinition;
        const uint16_t targetServiceId =
            static_cast<uint16_t>(targetDefinition);
        int targetMvps = 0;
        uint64_t targetLocalId = 0;
        bool targetHasStatTrak = false;
        for (int index = 0; index < selection.musicKitItemCount; ++index) {
            const auto& item = selection.musicKitItems[index];
            if (item.localId != selection.musicKit) continue;
            targetLocalId = item.localId;
            targetHasStatTrak = item.statTrak;
            targetMvps = item.statTrak && item.statTrakCount > 0
                ? item.statTrakCount : 0;
            break;
        }
        PendingMusicKitMvpObservation mvpObservation{};
        if (ReadMusicKitMvpObservation(mvpObservation)) {
            if (!targetHasStatTrak || targetLocalId == 0 ||
                mvpObservation.localId != targetLocalId) {
                ClearMusicKitMvpObservation(mvpObservation.localId);
            } else if (now < mvpObservation.observeAfter) {
                // Do not restore the old snapshot while CS2 is advancing MVPs.
                return;
            } else {
                const int observedMvps = currentControllerMvps >
                    mvpObservation.baseCount
                    ? currentControllerMvps
                    : mvpObservation.baseCount + 1;
                ClearMusicKitMvpObservation(mvpObservation.localId);
                if (currentControllerMvps != observedMvps) {
                    (void)SafeWrite(controller +
                        selection.controllerMusicKitMvpsOffset,
                        observedMvps);
                }
                QueueStatTrakCommand(targetLocalId, observedMvps);
                g_appliedMusicKit.targetMvps = observedMvps;
                AppendLog(
                    "StatTrak event: MVP local sincronizado tras estabilizar el runtime.");
                return;
            }
        }
        const bool sameAppliedSelection = g_appliedMusicKit.applied &&
            g_appliedMusicKit.targetDefinition == targetDefinition &&
            g_appliedMusicKit.applyRevision == selection.musicKitApplyRevision;
        if (targetHasStatTrak && targetLocalId != 0 && sameAppliedSelection &&
            currentControllerMvps > targetMvps) {
            QueueStatTrakCommand(
                targetLocalId, currentControllerMvps);
            g_appliedMusicKit.targetMvps = currentControllerMvps;
            return;
        }
        const bool selectionChanged =
            g_appliedMusicKit.targetDefinition != targetDefinition ||
            g_appliedMusicKit.targetMvps != targetMvps ||
            g_appliedMusicKit.applyRevision !=
                selection.musicKitApplyRevision;
        const bool needsWrite = selectionChanged ||
            currentControllerId != targetDefinition ||
            currentControllerMvps != targetMvps ||
            currentServiceId != targetServiceId;
        if (!needsWrite) {
            g_appliedMusicKit.applied = true;
            return;
        }

        const bool controllerWritten = currentControllerId == targetDefinition ||
            SafeWrite(controller + selection.controllerMusicKitIdOffset,
                targetDefinition);
        const bool serviceWritten = currentServiceId == targetServiceId ||
            SafeWrite(inventoryServices + selection.serviceMusicIdOffset,
                targetServiceId);
        const bool mvpsWritten = currentControllerMvps == targetMvps ||
            SafeWrite(controller + selection.controllerMusicKitMvpsOffset,
                targetMvps);
        g_appliedMusicKit.targetDefinition = targetDefinition;
        g_appliedMusicKit.targetMvps = targetMvps;
        g_appliedMusicKit.applyRevision = selection.musicKitApplyRevision;
        const bool wasApplied = g_appliedMusicKit.applied;
        g_appliedMusicKit.applied = controllerWritten && serviceWritten &&
            mvpsWritten;
        if (g_appliedMusicKit.applied && (!wasApplied || selectionChanged)) {
            char message[160]{};
            StringCchPrintfA(message, _countof(message),
                "Music Kit runtime: kit=%d mvps=%d aplicado (revision=%llu).",
                targetDefinition, targetMvps, static_cast<unsigned long long>(
                    selection.musicKitApplyRevision));
            AppendLog(message);
        }
    }

    const InventorySnapshotSelection::MusicKitCollectionItem*
    FindPublishedMusicKit(
        const InventorySnapshotSelection& selection, uint64_t localId) {
        for (int index = 0; index < selection.musicKitItemCount; ++index) {
            if (selection.musicKitItems[index].localId == localId)
                return &selection.musicKitItems[index];
        }
        return nullptr;
    }

    bool ApplyPublishedMusicKitLoadoutToSocache(
        const InventorySnapshotSelection& selection) {
        if (!selection.itemIdOffset) return false;
        if (!selection.enabled || selection.musicKit == 0) {
            RestoreInventorySocacheLoadoutSlot(kMusicKitLoadoutSlot,
                selection.enabled ? "music-kit-unequipped"
                                  : "music-kit-feature-disabled");
            return true;
        }

        const auto* item = FindPublishedMusicKit(
            selection, selection.musicKit);
        if (!item) return false;

        InventorySocacheItemSpec spec{};
        spec.localId = item->localId;
        // Slot 54 is shared by all characters and uses the global loadout
        // context rather than either playable team.
        spec.team = 0;
        spec.definitionIndex = kMusicKitItemDefinition;
        spec.musicKitId = item->musicKitId;
        spec.statTrak = item->statTrak;
        spec.statTrakCount = item->statTrakCount;
        spec.statTrakType = 1;
        spec.quality = static_cast<uint8_t>(item->statTrak
            ? kStrangeItemQuality : kUnusualItemQuality);
        spec.rarity = 3;
        spec.loadoutSlot = kMusicKitLoadoutSlot;
        spec.itemViewItemIdOffset = selection.itemIdOffset;
        spec.unacknowledged = IsPendingRevealItem(selection, item->localId);
        spec.equip = true;
        InventorySocacheItemIdentity identity{};
        return EnsureInventorySocacheItem(spec, identity);
    }

    void RunMusicKitCollectionControl() {
        static uint64_t synchronizedHash = 0;
        static uint64_t synchronizedLoadoutHash = 0;
        static int nextItem = 0;
        static ULONGLONG nextItemAt = 0;
        static ULONGLONG nextFailureLogAt = 0;
        static ULONGLONG nextControlAt = 0;

        const ULONGLONG controlNow = GetTickCount64();
        if (controlNow < nextControlAt) return;
        nextControlAt = controlNow + 25;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection) ||
            !selection.itemIdOffset)
            return;

        if (selection.musicKitCollectionHash != synchronizedHash) {
            uint64_t localIds[kMaxPublishedMusicKitItems]{};
            for (int index = 0; index < selection.musicKitItemCount; ++index)
                localIds[index] = selection.musicKitItems[index].localId;
            PruneInventorySocacheCollection(localIds,
                selection.musicKitItemCount, kMusicKitLoadoutSlot,
                "music-kit-collection-snapshot-changed");
            synchronizedHash = selection.musicKitCollectionHash;
            nextItem = 0;
            nextItemAt = 0;
            char message[176]{};
            StringCchPrintfA(message, _countof(message),
                "Music Kit SOCache sync: queued=%d hash=0x%016llX.",
                selection.musicKitItemCount,
                static_cast<unsigned long long>(synchronizedHash));
            AppendLog(message);
        }

        if (nextItem < selection.musicKitItemCount) {
            const ULONGLONG now = GetTickCount64();
            if (now < nextItemAt) return;

            const auto& item = selection.musicKitItems[nextItem];
            InventorySocacheItemSpec spec{};
            spec.localId = item.localId;
            spec.definitionIndex = kMusicKitItemDefinition;
            spec.musicKitId = item.musicKitId;
            spec.statTrak = item.statTrak;
            spec.statTrakCount = item.statTrakCount;
            spec.statTrakType = 1;
            spec.quality = static_cast<uint8_t>(item.statTrak
                ? kStrangeItemQuality : kUnusualItemQuality);
            spec.rarity = 3;
            spec.loadoutSlot = kMusicKitLoadoutSlot;
            spec.itemViewItemIdOffset = selection.itemIdOffset;
            spec.unacknowledged = IsPendingRevealItem(
                selection, item.localId);
            spec.equip = false;
            InventorySocacheItemIdentity identity{};
            if (EnsureInventorySocacheItem(spec, identity)) {
                ++nextItem;
                nextItemAt = now + 25;
                if (nextItem == selection.musicKitItemCount) {
                    char message[144]{};
                    StringCchPrintfA(message, _countof(message),
                        "Music Kit SOCache sync: complete items=%d.",
                        selection.musicKitItemCount);
                    AppendLog(message);
                    RequestPanoramaRevealRefresh();
                }
            } else {
                nextItemAt = now + kSocacheRetryMs;
            }
            return;
        }

        const ULONGLONG now = GetTickCount64();
        if (now < nextItemAt) return;
        const uint64_t loadoutHash = selection.musicKit ^
            (selection.enabled ? 0xD6E8FEB86659FD93ull : 0ull) ^
            (selection.musicKitApplyRevision * 0x9E3779B185EBCA87ull);
        if (loadoutHash == synchronizedLoadoutHash) return;

        const bool applied = ApplyPublishedMusicKitLoadoutToSocache(selection);
        if (applied) {
            synchronizedLoadoutHash = loadoutHash;
            g_nativeLoadoutObserveAfter.store(now + kLoadoutSettleMs);
            nextFailureLogAt = 0;
            AppendLog(
                "Music Kit SOCache loadout: estado publicado aplicado.");
        } else {
            nextItemAt = now + kSocacheRetryMs;
            if (nextFailureLogAt == 0 || now >= nextFailureLogAt) {
                AppendLog(
                    "Music Kit SOCache loadout: aplicacion pendiente.");
                nextFailureLogAt = now + kRetryLogIntervalMs;
            }
        }
    }

    void RunGloveCollectionControl() {
        static uint64_t synchronizedHash = 0;
        static uint64_t synchronizedLoadoutHash = 0;
        static int nextItem = 0;
        static ULONGLONG nextItemAt = 0;
        static ULONGLONG nextFailureLogAt = 0;
        static ULONGLONG nextControlAt = 0;

        const ULONGLONG controlNow = GetTickCount64();
        if (controlNow < nextControlAt) return;
        nextControlAt = controlNow + 25;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection) ||
            !selection.itemIdOffset)
            return;

        if (selection.gloveCollectionHash != synchronizedHash) {
            uint64_t localIds[kMaxPublishedGloveItems]{};
            for (int index = 0; index < selection.gloveItemCount; ++index)
                localIds[index] = selection.gloveItems[index].localId;
            PruneInventorySocacheCollection(localIds,
                selection.gloveItemCount, kGloveLoadoutSlot,
                "glove-collection-snapshot-changed");
            synchronizedHash = selection.gloveCollectionHash;
            nextItem = 0;
            nextItemAt = 0;
            char message[176]{};
            StringCchPrintfA(message, _countof(message),
                "Glove SOCache sync: queued=%d hash=0x%016llX.",
                selection.gloveItemCount,
                static_cast<unsigned long long>(synchronizedHash));
            AppendLog(message);
        }

        const ULONGLONG now = GetTickCount64();
        if (now < nextItemAt) return;

        if (nextItem >= selection.gloveItemCount) {
            const uint64_t loadoutHash = selection.terroristGloves ^
                (selection.counterTerroristGloves << 1) ^
                (selection.enabled ? 0xA24BAED4963EE407ull : 0ull) ^
                selection.gloveCollectionHash;
            if (loadoutHash == synchronizedLoadoutHash) return;
            if (ApplyPublishedGloveLoadoutToSocache(selection)) {
                synchronizedLoadoutHash = loadoutHash;
                nextFailureLogAt = 0;
                AppendLog(
                    "Glove SOCache loadout: estado publicado aplicado.");
            } else {
                nextItemAt = now + kSocacheRetryMs;
                if (nextFailureLogAt == 0 || now >= nextFailureLogAt) {
                    AppendLog(
                        "Glove SOCache loadout: aplicacion pendiente.");
                    nextFailureLogAt = now + kRetryLogIntervalMs;
                }
            }
            return;
        }

        const auto& item = selection.gloveItems[nextItem];
        InventorySocacheItemSpec spec{};
        spec.localId = item.localId;
        spec.team = 0;
        spec.definitionIndex = item.definitionIndex;
        spec.paintKit = item.paintKit;
        spec.seed = item.seed;
        spec.wear = item.wear;
        spec.statTrak = item.statTrak;
        spec.statTrakCount = item.statTrakCount;
        spec.loadoutSlot = kGloveLoadoutSlot;
        spec.itemViewItemIdOffset = selection.itemIdOffset;
        spec.unacknowledged = IsPendingRevealItem(selection, item.localId);
        spec.equip = false;
        InventorySocacheItemIdentity identity{};
        if (EnsureInventorySocacheItem(spec, identity)) {
            ++nextItem;
            nextItemAt = now + 25;
            if (nextItem == selection.gloveItemCount) {
                char message[144]{};
                StringCchPrintfA(message, _countof(message),
                    "Glove SOCache sync: complete items=%d.",
                    selection.gloveItemCount);
                AppendLog(message);
                RequestPanoramaRevealRefresh();
            }
        } else {
            nextItemAt = now + kSocacheRetryMs;
        }
    }

    void RunWeaponSkinCollectionControl() {
        static uint64_t synchronizedHash = 0;
        static int nextItem = 0;
        static ULONGLONG nextItemAt = 0;
        static ULONGLONG nextControlAt = 0;

        if (!g_weaponSkinSocacheAllowed) return;
        const ULONGLONG controlNow = GetTickCount64();
        if (controlNow < nextControlAt) return;
        nextControlAt = controlNow + 25;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection) ||
            !selection.itemIdOffset)
            return;

        if (selection.weaponSkinCollectionHash != synchronizedHash) {
            uint64_t localIds[kMaxPublishedWeaponSkinItems]{};
            for (int index = 0; index < selection.weaponSkinItemCount; ++index)
                localIds[index] = selection.weaponSkinItems[index].localId;
            PruneInventorySocacheCollection(localIds,
                selection.weaponSkinItemCount, kWeaponSkinCollectionSlot,
                "weapon-skin-collection-snapshot-changed");
            synchronizedHash = selection.weaponSkinCollectionHash;
            nextItem = 0;
            nextItemAt = 0;
            char message[192]{};
            StringCchPrintfA(message, _countof(message),
                "Weapon skin SOCache sync: queued=%d hash=0x%016llX.",
                selection.weaponSkinItemCount,
                static_cast<unsigned long long>(synchronizedHash));
            AppendLog(message);
            if (selection.weaponSkinItemCount == 0)
                RequestPanoramaRevealRefresh();
        }

        if (nextItem >= selection.weaponSkinItemCount) return;
        const ULONGLONG now = GetTickCount64();
        if (now < nextItemAt) return;

        const auto& item = selection.weaponSkinItems[nextItem];
        InventorySocacheItemSpec spec{};
        spec.localId = item.localId;
        spec.team = 0;
        spec.definitionIndex = item.definitionIndex;
        spec.paintKit = item.paintKit;
        spec.seed = item.seed;
        spec.wear = item.wear;
        spec.statTrak = item.statTrak;
        spec.statTrakCount = item.statTrakCount;
        spec.statTrakType = 0;
        spec.quality = item.quality;
        spec.rarity = item.rarity;
        spec.loadoutSlot = kWeaponSkinCollectionSlot;
        spec.itemViewItemIdOffset = selection.itemIdOffset;
        spec.unacknowledged = IsPendingRevealItem(
            selection, item.localId);
        spec.equip = false;
        InventorySocacheItemIdentity identity{};
        if (EnsureInventorySocacheItem(spec, identity)) {
            ++nextItem;
            nextItemAt = now + 25;
            if (nextItem == selection.weaponSkinItemCount) {
                char message[160]{};
                StringCchPrintfA(message, _countof(message),
                    "Weapon skin SOCache sync: complete items=%d.",
                    selection.weaponSkinItemCount);
                AppendLog(message);
                RequestPanoramaRevealRefresh();
            }
        } else {
            nextItemAt = now + kSocacheRetryMs;
        }
    }

    void RunMiscCollectionControl() {
        static uint64_t synchronizedHash = 0;
        static int nextItem = 0;
        static ULONGLONG nextItemAt = 0;
        static ULONGLONG nextControlAt = 0;

        const ULONGLONG controlNow = GetTickCount64();
        if (controlNow < nextControlAt) return;
        nextControlAt = controlNow + 25;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection) ||
            !selection.itemIdOffset)
            return;

        if (selection.miscCollectionHash != synchronizedHash) {
            uint64_t localIds[kMaxPublishedMiscItems]{};
            for (int index = 0; index < selection.miscItemCount; ++index)
                localIds[index] = selection.miscItems[index].localId;
            PruneInventorySocacheCollection(localIds,
                selection.miscItemCount, kMiscCollectionSlot,
                "misc-collection-snapshot-changed");
            synchronizedHash = selection.miscCollectionHash;
            nextItem = 0;
            nextItemAt = 0;
            char message[176]{};
            StringCchPrintfA(message, _countof(message),
                "Misc SOCache sync: queued=%d hash=0x%016llX.",
                selection.miscItemCount,
                static_cast<unsigned long long>(synchronizedHash));
            AppendLog(message);
        }

        if (nextItem >= selection.miscItemCount) return;
        const ULONGLONG now = GetTickCount64();
        if (now < nextItemAt) return;

        const auto& item = selection.miscItems[nextItem];
        InventorySocacheItemSpec spec{};
        spec.localId = item.localId;
        spec.team = 0;
        spec.definitionIndex = item.definitionIndex;
        spec.variantAttributeDefinition =
            item.variantAttributeDefinition;
        spec.variantAttributeValue = item.variantAttributeValue;
        spec.quality = item.quality;
        spec.rarity = item.rarity;
        spec.loadoutSlot = kMiscCollectionSlot;
        spec.itemViewItemIdOffset = selection.itemIdOffset;
        spec.unacknowledged = IsPendingRevealItem(
            selection, item.localId);
        spec.equip = false;
        InventorySocacheItemIdentity identity{};
        if (EnsureInventorySocacheItem(spec, identity)) {
            ++nextItem;
            nextItemAt = now + 25;
            if (nextItem == selection.miscItemCount) {
                char message[144]{};
                StringCchPrintfA(message, _countof(message),
                    "Misc SOCache sync: complete items=%d.",
                    selection.miscItemCount);
                AppendLog(message);
                RequestPanoramaRevealRefresh();
            }
        } else {
            nextItemAt = now + kSocacheRetryMs;
        }
    }

    void RunInventoryCollectionControl() {
        static uint64_t synchronizedHash = 0;
        static uint64_t synchronizedLoadoutHash = 0;
        static int nextItem = 0;
        static ULONGLONG nextItemAt = 0;
        static ULONGLONG nextFailureLogAt = 0;
        static ULONGLONG nextControlAt = 0;

        const ULONGLONG controlNow = GetTickCount64();
        if (controlNow < nextControlAt) return;
        nextControlAt = controlNow + 25;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection) ||
            !selection.itemIdOffset)
            return;

        if (selection.knifeCollectionHash != synchronizedHash) {
            InterlockedExchange(&g_inventoryCollectionSyncing, 1);
            InterlockedIncrement(&g_nativeLoadoutObservationEpoch);
            uint64_t localIds[kMaxPublishedKnifeItems]{};
            for (int index = 0; index < selection.knifeItemCount; ++index)
                localIds[index] = selection.knifeItems[index].localId;
            PruneInventorySocacheCollection(localIds,
                selection.knifeItemCount, kKnifeLoadoutSlot,
                "collection-snapshot-changed");
            synchronizedHash = selection.knifeCollectionHash;
            nextItem = 0;
            nextItemAt = 0;
            char message[160]{};
            StringCchPrintfA(message, _countof(message),
                "SOCache collection sync: queued=%d hash=0x%016llX.",
                selection.knifeItemCount,
                static_cast<unsigned long long>(synchronizedHash));
            AppendLog(message);
        }

        if (nextItem >= selection.knifeItemCount) {
            const ULONGLONG now = GetTickCount64();
            if (now < nextItemAt) return;
            const uint64_t loadoutHash = selection.terroristKnife ^
                (selection.counterTerroristKnife << 1) ^
                (selection.enabled ? 0x9E3779B97F4A7C15ull : 0ull);
            const bool reapplyRequested = InterlockedCompareExchange(
                &g_inventoryLoadoutReapplyRequested, 0, 0) != 0;
            if (InterlockedCompareExchange(
                    &g_inventoryCollectionSyncing, 0, 0) != 0 ||
                loadoutHash != synchronizedLoadoutHash || reapplyRequested) {
                const bool loadoutApplied =
                    ApplyPublishedKnifeLoadoutToSocache(selection);
                if (loadoutApplied) {
                    synchronizedLoadoutHash = loadoutHash;
                    InterlockedExchange(&g_inventoryCollectionSyncing, 0);
                    InterlockedExchange(
                        &g_inventoryLoadoutReapplyRequested, 0);
                    g_nativeLoadoutObserveAfter.store(now + kLoadoutSettleMs);
                    InterlockedIncrement(&g_nativeLoadoutObservationEpoch);
                    nextFailureLogAt = 0;
                    AppendLog(
                        "SOCache loadout sync: estado publicado aplicado.");
                } else {
                    InterlockedExchange(&g_inventoryCollectionSyncing, 1);
                    nextItemAt = now + kSocacheRetryMs;
                    if (nextFailureLogAt == 0 || now >= nextFailureLogAt) {
                        AppendLog(
                            "SOCache loadout sync: aplicacion pendiente.");
                        nextFailureLogAt = now + kRetryLogIntervalMs;
                    }
                }
            }
            return;
        }
        const ULONGLONG now = GetTickCount64();
        if (now < nextItemAt) return;

        const auto& item = selection.knifeItems[nextItem];
        InventorySocacheItemSpec spec{};
        spec.localId = item.localId;
        spec.team = 0;
        spec.definitionIndex = item.definitionIndex;
        spec.paintKit = item.paintKit;
        spec.seed = item.seed;
        spec.wear = item.wear;
        spec.statTrak = item.statTrak;
        spec.statTrakCount = item.statTrakCount;
        spec.loadoutSlot = kKnifeLoadoutSlot;
        spec.itemViewItemIdOffset = selection.itemIdOffset;
        spec.unacknowledged = IsPendingRevealItem(selection, item.localId);
        spec.equip = false;
        InventorySocacheItemIdentity identity{};
        if (EnsureInventorySocacheItem(spec, identity)) {
            ++nextItem;
            nextItemAt = now + 25;
            if (nextItem == selection.knifeItemCount) {
                char message[128]{};
                StringCchPrintfA(message, _countof(message),
                    "SOCache collection sync: complete items=%d.",
                    selection.knifeItemCount);
                AppendLog(message);
                RequestPanoramaRevealRefresh();
            }
        } else {
            nextItemAt = now + kSocacheRetryMs;
        }
    }

    void RunPendingRevealAcknowledgementControl() {
        static uint64_t lastQueuedHash = 0;
        static ULONGLONG nextControlAt = 0;
        const ULONGLONG now = GetTickCount64();
        if (now < nextControlAt) return;
        nextControlAt = now + 100;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection) ||
            selection.pendingRevealItemCount <= 0) {
            lastQueuedHash = 0;
            return;
        }

        uint64_t pendingHash = 14695981039346656037ull;
        for (int index = 0; index < selection.pendingRevealItemCount; ++index) {
            const uint64_t localId = selection.pendingRevealLocalIds[index];
            bool unacknowledged = false;
            if (!ReadInventorySocacheItemUnacknowledged(
                    localId, unacknowledged) || unacknowledged)
                return;
            pendingHash ^= localId;
            pendingHash *= 1099511628211ull;
        }
        if (pendingHash == lastQueuedHash) return;
        lastQueuedHash = pendingHash;
        RequestPanoramaRevealAcknowledged(
            selection.pendingRevealLocalIds[0]);
        AppendLog(
            "SOCache reveal: todos los articulos fueron confirmados por CS2.");
    }

    void RunKnifeModelControl() {
        if (!g_setModel || !g_updateSubclass) return;
        const ULONGLONG now = GetTickCount64();
        if (now < g_appliedKnife.nextUpdateAt) return;
        g_appliedKnife.nextUpdateAt = now + kModelUpdateIntervalMs;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection)) return;
        if (!selection.enabled) {
            RestoreInventorySocacheLoadoutSlot(
                kKnifeLoadoutSlot, "knife-feature-disabled");
        }
        if (HasAppliedKnifeState() &&
            g_appliedKnife.appliedToControlledPawn &&
            !selection.applyKnivesToControlledBots) {
            RestoreAppliedKnife(selection, "restored-controlled-bot-disabled");
            return;
        }
        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t controller = 0;
        uintptr_t pawn = 0;
        uintptr_t stride = 0;
        bool usedActivePawn = false;
        if (!ResolvePlayableKnifePawn(client, selection, controller, pawn,
            stride, usedActivePawn) || !pawn)
            return;

        int team = 0;
        uint8_t lifeState = 0xFF;
        float spawnTimeIndex = 0.0f;
        if (!SafeRead(pawn + selection.teamNumberOffset, team) ||
            !selection.lifeStateOffset ||
            !SafeRead(pawn + selection.lifeStateOffset, lifeState))
            return;
        if (selection.lastSpawnTimeIndexOffset)
            (void)SafeRead(pawn + selection.lastSpawnTimeIndexOffset,
                spawnTimeIndex);
        if (HasAppliedKnifeState() && pawn != g_appliedKnife.pawn) {
            char message[224]{};
            StringCchPrintfA(message, _countof(message),
                "Knife identity: cambio de pawn 0x%llX -> 0x%llX; "
                "restaurando entidad anterior.",
                static_cast<unsigned long long>(g_appliedKnife.pawn),
                static_cast<unsigned long long>(pawn));
            AppendLog(message);
            RestoreAppliedKnife(selection, "restored-controlled-pawn-change");
            g_fastKnifeReapplyUntil = now + 2000;
            return;
        }
        if (HasAppliedKnifeState() &&
            spawnTimeIndex != g_appliedKnife.spawnTimeIndex) {
            AppendLog("Knife identity: nuevo spawn; reaplicacion rapida validada.");
            // The previous round entity is being destroyed. Do not restore it:
            // validate the new full handle before applying to the replacement.
            ClearAppliedKnifeState();
            ResetPendingKnife();
            g_fastKnifeReapplyUntil = now + 2000;
        }
        if (lifeState != 0) {
            ResetPendingKnife();
            return;
        }

        const int targetDefinition = team == kTerroristTeam
            ? selection.terroristKnifeDefinition
            : team == kCounterTerroristTeam
                ? selection.counterTerroristKnifeDefinition : 0;
        const uint64_t targetLocalId = team == kTerroristTeam
            ? selection.terroristKnife
            : selection.counterTerroristKnife;
        const int targetPaintKit = team == kTerroristTeam
            ? selection.terroristKnifePaintKit
            : selection.counterTerroristKnifePaintKit;
        const int targetSeed = team == kTerroristTeam
            ? selection.terroristKnifeSeed
            : selection.counterTerroristKnifeSeed;
        const float targetWear = team == kTerroristTeam
            ? selection.terroristKnifeWear
            : selection.counterTerroristKnifeWear;
        const bool targetHasStatTrak = team == kTerroristTeam
            ? selection.terroristKnifeStatTrak
            : selection.counterTerroristKnifeStatTrak;
        const int targetStatTrak = targetHasStatTrak
            ? (team == kTerroristTeam
                ? selection.terroristKnifeStatTrakCount
                : selection.counterTerroristKnifeStatTrakCount)
            : -1;
        const bool targetHasFinish = targetPaintKit > 0 || targetHasStatTrak;
        const KnifeModel* targetKnife = selection.enabled
            ? FindKnifeModel(targetDefinition) : nullptr;
        if (!targetKnife) {
            if (HasAppliedKnifeState())
                RestoreAppliedKnife(selection, "restored-disabled");
            else
                g_fastKnifeReapplyUntil = 0;
            RestoreInventorySocacheLoadoutSlot(
                kKnifeLoadoutSlot, "knife-selection-disabled");
            return;
        }
        const char* targetModel = targetKnife->modelPath;

        uintptr_t entityList = 0;
        uint32_t weaponHandle = 0;
        uintptr_t weapon = 0;
        if (!ResolveActiveWeaponForPawn(client, selection, pawn, stride,
            entityList, weaponHandle, weapon)) {
            if (HasAppliedKnifeState())
                g_appliedKnife.activeLastCheck = false;
            return;
        }

        uint16_t currentDefinition = 0;
        int32_t currentQuality = 0;
        int currentPaintKit = 0;
        int currentSeed = 0;
        float currentWear = 0.0f;
        int currentStatTrak = -1;
        uint32_t currentItemIdHigh = 0;
        uint64_t currentItemId = 0;
        const uintptr_t itemView = weapon + selection.attributeManagerOffset +
            selection.itemOffset;
        if (!selection.itemDefinitionIndexOffset ||
            !selection.entityQualityOffset ||
            !SafeRead(itemView + selection.itemDefinitionIndexOffset,
                currentDefinition) ||
            !SafeRead(itemView + selection.entityQualityOffset,
                currentQuality))
            return;
        const bool finishReadable = ReadKnifeFinish(
            weapon, itemView, selection, currentPaintKit, currentSeed,
            currentWear, currentStatTrak, currentItemIdHigh);
        if (selection.itemIdOffset)
            (void)SafeRead(itemView + selection.itemIdOffset, currentItemId);
        if (targetHasFinish && !finishReadable) return;
        if (!IsKnifeDefinition(currentDefinition)) {
            if (HasAppliedKnifeState())
                g_appliedKnife.activeLastCheck = false;
            else
                ResetPendingKnife();
            return;
        }

        if (HasAppliedKnifeState() && weapon != g_appliedKnife.weapon) {
            RestoreAppliedKnife(selection, "restored-new-knife-entity");
            ResetPendingKnife();
            return;
        }

        uint32_t currentSubclass = 0;
        if (!selection.subclassIdOffset ||
            !SafeRead(weapon + selection.subclassIdOffset, currentSubclass))
            return;

        const float selectedWearDelta = g_appliedKnife.targetWear - targetWear;
        const bool statTrakModeChanged = HasAppliedKnifeState() &&
            ((g_appliedKnife.targetStatTrak < 0) !=
                (targetStatTrak < 0));
        if (HasAppliedKnifeState() &&
            (g_appliedKnife.definitionIndex != targetDefinition ||
                g_appliedKnife.targetPaintKit != targetPaintKit ||
                g_appliedKnife.targetSeed != targetSeed ||
                selectedWearDelta > 0.000001f ||
                selectedWearDelta < -0.000001f ||
                statTrakModeChanged)) {
            RestoreAppliedKnife(selection, "restored-selection-change");
            // The pawn and weapon handle were validated immediately above.
            // Only the selected inventory item changed, so the regular
            // new-entity stability delay would add visible latency without
            // providing additional lifetime safety.
            g_pendingKnifeWeapon = weapon;
            g_pendingKnifeHandle = weaponHandle;
            g_pendingKnifeSince = now >= 200 ? now - 200 : 0;
            g_fastKnifeReapplyUntil = now + 2000;
            return;
        }

        if (HasAppliedKnifeState() && targetStatTrak >= 0 &&
            g_appliedKnife.targetStatTrak != targetStatTrak) {
            if (!selection.fallbackStatTrakOffset ||
                !SafeWrite(weapon + selection.fallbackStatTrakOffset,
                    targetStatTrak)) {
                AppendLog("Knife StatTrak: actualizacion in-place fallo.");
                return;
            }
            g_appliedKnife.targetStatTrak = targetStatTrak;
            currentStatTrak = targetStatTrak;
            AppendLog(
                "Knife StatTrak: contador actualizado sin reaplicar modelo.");
        }

        if (HasAppliedKnifeState() &&
            (currentDefinition != targetKnife->definitionIndex ||
                currentSubclass != targetKnife->subclassId)) {
            AppendLog("Knife identity: estado descartado por reinicio del arma.");
            ClearAppliedKnifeState();
            g_pendingKnifeWeapon = weapon;
            g_pendingKnifeHandle = weaponHandle;
            g_pendingKnifeSince = now;
            return;
        }
        if (HasAppliedKnifeState() && g_appliedKnife.qualityApplied &&
            currentQuality != kUnusualItemQuality) {
            if (!SafeWrite(itemView + selection.entityQualityOffset,
                    kUnusualItemQuality)) {
                RestoreAppliedKnife(selection,
                    "restored-quality-maintenance-failed");
                return;
            }
            g_appliedKnife.hudRefreshed = false;
            g_appliedKnife.hudRefreshAttempts = 0;
            g_appliedKnife.hudRefreshAt = now + 250;
        }
        if (HasAppliedKnifeState() && g_appliedKnife.itemIdHighApplied &&
            currentItemId != g_appliedKnife.generatedItemId) {
            InventorySocacheItemIdentity identity{};
            identity.itemId = g_appliedKnife.generatedItemId;
            identity.accountId = g_appliedKnife.generatedAccountId;
            if (!WriteInventoryItemIdentity(itemView,
                    g_appliedKnife.definitionIndex, identity, selection)) {
                RestoreAppliedKnife(selection,
                    "restored-identity-maintenance-failed");
                return;
            }
            currentItemId = g_appliedKnife.generatedItemId;
            g_appliedKnife.hudRefreshed = false;
            g_appliedKnife.hudRefreshAttempts = 0;
            g_appliedKnife.hudRefreshAt = now + 250;
        }
        if (HasAppliedKnifeState() && g_appliedKnife.finishApplied) {
            const float wearDelta = currentWear - g_appliedKnife.targetWear;
            const bool finishDrifted = !finishReadable ||
                currentPaintKit != g_appliedKnife.targetPaintKit ||
                currentSeed != g_appliedKnife.targetSeed ||
                wearDelta > 0.000001f || wearDelta < -0.000001f ||
                currentStatTrak != g_appliedKnife.targetStatTrak ||
                currentItemId != g_appliedKnife.generatedItemId;
            if (finishDrifted && !WriteKnifeFinish(
                    weapon, itemView, selection,
                    g_appliedKnife.targetPaintKit,
                    g_appliedKnife.targetSeed,
                    g_appliedKnife.targetWear,
                    g_appliedKnife.targetStatTrak,
                    static_cast<uint32_t>(
                        g_appliedKnife.generatedItemId >> 32))) {
                RestoreAppliedKnife(selection,
                    "restored-finish-maintenance-failed");
                return;
            }
            if (finishDrifted) {
                g_appliedKnife.hudRefreshed = false;
                g_appliedKnife.hudRefreshAttempts = 0;
                g_appliedKnife.hudRefreshAt = now + 250;
            }
        }

        if (!HasAppliedKnifeState()) {
            if (g_pendingKnifeWeapon != weapon ||
                g_pendingKnifeHandle != weaponHandle) {
                g_pendingKnifeWeapon = weapon;
                g_pendingKnifeHandle = weaponHandle;
                g_pendingKnifeSince = now;
                return;
            }
            const ULONGLONG stabilityDelay =
                now < g_fastKnifeReapplyUntil ? 200 : 350;
            if (now - g_pendingKnifeSince < stabilityDelay) return;
        }

        if (HasAppliedKnifeState() && g_appliedKnife.activeLastCheck) {
            MaintainKnifeHud(itemView, now);
            if (g_appliedKnife.finishApplied &&
                g_appliedKnife.compositeRefreshesRemaining > 0 &&
                now >= g_appliedKnife.nextCompositeRefreshAt) {
                // The mesh mask is applied once with SetModel. Reapplying it
                // after the scene-node rebuild targets transient nodes.
                (void)SafeRefreshKnifeComposite(g_appliedKnife.weapon);
                --g_appliedKnife.compositeRefreshesRemaining;
                g_appliedKnife.nextCompositeRefreshAt = now + 100;
                if (g_appliedKnife.compositeRefreshesRemaining == 0) {
                    bool modulesChanged = false;
                    if (SafeRefreshWeaponModules(g_appliedKnife.weapon,
                            itemView, modulesChanged)) {
                        AppendLog(modulesChanged
                            ? "Knife modules: adjuntos finalizados tras composite."
                            : "Knife modules: validacion final completada.");
                    } else {
                        AppendLog("Knife modules: validacion final no disponible.");
                    }
                    AppendLog("Knife finish: reconstruccion diferida completada.");
                }
            }
            g_appliedKnife.nextUpdateAt = now +
                (g_appliedKnife.compositeRefreshesRemaining > 0 ? 100 : 500);
            return;
        }

        uintptr_t viewmodel = ResolveHudViewModelForWeapon(
            entityList, pawn, weapon, stride, selection);
        if (!viewmodel) return;

        char currentWeaponModel[260]{};
        char currentViewmodelModel[260]{};
        if (!ReadPawnModel(weapon, selection, currentWeaponModel,
            _countof(currentWeaponModel)) ||
            !ReadPawnModel(viewmodel, selection, currentViewmodelModel,
                _countof(currentViewmodelModel)))
            return;

        if (!HasAppliedKnifeState()) {
            if (!selection.attributeManagerOffset || !selection.itemOffset)
                return;

            const ULONGLONG nextUpdateAt = g_appliedKnife.nextUpdateAt;
            g_appliedKnife = {};
            g_appliedKnife.nextUpdateAt = nextUpdateAt;
            g_appliedKnife.pawn = pawn;
            g_appliedKnife.entityStride = stride;
            g_appliedKnife.weapon = weapon;
            g_appliedKnife.viewmodel = viewmodel;
            g_appliedKnife.weaponHandle = weaponHandle;
            g_appliedKnife.definitionIndex = targetDefinition;
            g_appliedKnife.originalDefinition = currentDefinition;
            g_appliedKnife.originalSubclass = currentSubclass;
            g_appliedKnife.originalQuality = currentQuality;
            g_appliedKnife.originalPaintKit = currentPaintKit;
            g_appliedKnife.originalSeed = currentSeed;
            g_appliedKnife.originalWear = currentWear;
            g_appliedKnife.originalStatTrak = currentStatTrak;
            if (!SafeRead(itemView + selection.itemIdOffset,
                    g_appliedKnife.originalItemId) ||
                !SafeRead(itemView + selection.itemIdLowOffset,
                    g_appliedKnife.originalItemIdLow) ||
                !SafeRead(itemView + selection.accountIdOffset,
                    g_appliedKnife.originalAccountId) ||
                !SafeRead(itemView + selection.initializedOffset,
                    g_appliedKnife.originalInitialized)) {
                ClearAppliedKnifeState();
                return;
            }
            g_appliedKnife.originalItemIdHigh = currentItemIdHigh;
            g_appliedKnife.targetPaintKit = targetPaintKit;
            g_appliedKnife.targetSeed = targetSeed;
            g_appliedKnife.targetWear = targetWear;
            g_appliedKnife.targetStatTrak = targetStatTrak;
            g_appliedKnife.spawnTimeIndex = spawnTimeIndex;
            g_appliedKnife.appliedToControlledPawn = usedActivePawn;
            const char* fallbackOriginal =
                FindDefaultKnifeModel(currentDefinition);
            StringCchCopyA(g_appliedKnife.originalWeaponModel,
                _countof(g_appliedKnife.originalWeaponModel),
                fallbackOriginal ? fallbackOriginal : currentWeaponModel);
            StringCchCopyA(g_appliedKnife.originalViewmodelModel,
                _countof(g_appliedKnife.originalViewmodelModel),
                fallbackOriginal ? fallbackOriginal : currentViewmodelModel);
            StringCchCopyA(g_appliedKnife.targetModel,
                _countof(g_appliedKnife.targetModel), targetModel);
            const bool fastReapply = now < g_fastKnifeReapplyUntil;
            g_appliedKnife.hudRefreshAt = now + (fastReapply ? 250 : 500);

            InventorySocacheItemSpec knifeSpec{};
            knifeSpec.localId = targetLocalId;
            knifeSpec.team = team;
            knifeSpec.definitionIndex = targetDefinition;
            knifeSpec.paintKit = targetPaintKit;
            knifeSpec.seed = targetSeed;
            knifeSpec.wear = targetWear;
            knifeSpec.statTrak = targetHasStatTrak;
            knifeSpec.statTrakCount = targetHasStatTrak
                ? targetStatTrak : 0;
            knifeSpec.loadoutSlot = kKnifeLoadoutSlot;
            knifeSpec.itemViewItemIdOffset = selection.itemIdOffset;
            knifeSpec.unacknowledged = IsPendingRevealItem(
                selection, targetLocalId);
            InventorySocacheItemIdentity knifeIdentity{};
            if (!EnsureInventorySocacheItem(knifeSpec, knifeIdentity)) {
                AppendLog("Knife finish: no se pudo crear el item SOCache.");
                ClearAppliedKnifeState();
                return;
            }
            g_appliedKnife.generatedItemId = knifeIdentity.itemId;
            g_appliedKnife.generatedAccountId = knifeIdentity.accountId;
            g_appliedKnife.itemIdHighApplied = true;
            const bool clearedItemView =
                ClearInventorySocacheItemView(itemView);
            AppendLog(clearedItemView
                ? "Knife finish: contexto visual anterior liberado."
                : "Knife finish: limpieza visual no disponible; "
                    "continuando con la vista existente.");
            // Keep the weapon-owned item view. Copying the loadout view also
            // copies its render/material context, which can leave the live
            // weapon bound to the vanilla composite even when SOC attributes
            // already contain the selected paint kit.
            AppendLog("Knife finish: identidad SOCache vinculada sin copiar "
                "el contexto visual.");
            if (!WriteInventoryItemIdentity(itemView, targetDefinition,
                    knifeIdentity, selection)) {
                RestoreAppliedKnife(selection,
                    "restored-socache-identity-failed");
                return;
            }
            constexpr uintptr_t kRestoreCustomMaterialOffset = 0x1B8;
            constexpr uintptr_t kDisallowSocOffset = 0x1E9;
            const bool restoreCustomMaterial = true;
            const bool allowSoc = false;
            if (!SafeWrite(itemView + kRestoreCustomMaterialOffset,
                    restoreCustomMaterial) ||
                !SafeWrite(itemView + kDisallowSocOffset, allowSoc)) {
                RestoreAppliedKnife(selection,
                    "restored-material-flags-failed");
                return;
            }

            const uint16_t targetDefinitionValue =
                static_cast<uint16_t>(targetKnife->definitionIndex);
            const bool definitionWritten = SafeWrite(itemView +
                selection.itemDefinitionIndexOffset, targetDefinitionValue);
            const bool subclassWritten = SafeWrite(weapon +
                selection.subclassIdOffset, targetKnife->subclassId);
            const bool qualityWritten = SafeWrite(itemView +
                selection.entityQualityOffset, kUnusualItemQuality);
            if (!definitionWritten || !subclassWritten || !qualityWritten) {
                (void)SafeWrite(itemView + selection.itemDefinitionIndexOffset,
                    g_appliedKnife.originalDefinition);
                (void)SafeWrite(weapon + selection.subclassIdOffset,
                    g_appliedKnife.originalSubclass);
                (void)SafeWrite(itemView + selection.entityQualityOffset,
                    g_appliedKnife.originalQuality);
                LogKnifeIdentity("apply-write-failed", weapon,
                    targetDefinitionValue, targetKnife->subclassId);
                ClearAppliedKnifeState();
                return;
            }

            g_appliedKnife.subclassApplied = true;
            g_appliedKnife.qualityApplied = true;
            if (!SafeUpdateSubclass(weapon)) {
                RestoreAppliedKnife(selection,
                    "restored-subclass-update-failed");
                return;
            }
            if (targetHasFinish) {
                g_appliedKnife.finishApplied = true;
                if (!WriteKnifeFinish(weapon, itemView, selection,
                        targetPaintKit, targetSeed, targetWear,
                        targetStatTrak, static_cast<uint32_t>(
                            knifeIdentity.itemId >> 32))) {
                    RestoreAppliedKnife(selection,
                        "restored-finish-apply-failed");
                    return;
                }
                char finishMessage[256]{};
                StringCchPrintfA(finishMessage, _countof(finishMessage),
                    "Knife finish: applied paint=%d seed=%d wear=%.6f "
                    "stattrak=%d.", targetPaintKit, targetSeed, targetWear,
                    targetStatTrak);
                AppendLog(finishMessage);
            }
            LogInventorySocacheItemViewDiagnostics(
                itemView, knifeIdentity.loadoutItemView);
            int32_t rebuiltQuality = 0;
            if (!SafeRead(itemView + selection.entityQualityOffset,
                    rebuiltQuality) ||
                (rebuiltQuality != kUnusualItemQuality &&
                    !SafeWrite(itemView + selection.entityQualityOffset,
                        kUnusualItemQuality))) {
                RestoreAppliedKnife(selection,
                    "restored-quality-update-failed");
                return;
            }
            LogKnifeIdentity("applied", weapon, targetDefinitionValue,
                targetKnife->subclassId);
            ResetPendingKnife();
            g_fastKnifeReapplyUntil = 0;

            const uintptr_t rebuiltViewmodel = ResolveHudViewModelForWeapon(
                entityList, pawn, weapon, stride, selection);
            if (rebuiltViewmodel) {
                viewmodel = rebuiltViewmodel;
                g_appliedKnife.viewmodel = rebuiltViewmodel;
            }
        } else {
            if (viewmodel != g_appliedKnife.viewmodel) {
                g_appliedKnife.viewmodel = viewmodel;
                g_appliedKnife.viewmodelApplied = false;
                const char* fallbackOriginal = FindDefaultKnifeModel(
                    g_appliedKnife.originalDefinition);
                if (fallbackOriginal)
                    StringCchCopyA(g_appliedKnife.originalViewmodelModel,
                        _countof(g_appliedKnife.originalViewmodelModel),
                        fallbackOriginal);
            }
            MaintainKnifeHud(itemView, now);
            g_appliedKnife.activeLastCheck = true;
            if (strcmp(currentWeaponModel, targetModel) == 0 &&
                strcmp(currentViewmodelModel, targetModel) == 0) {
                return;
            }
        }

        g_setModel(reinterpret_cast<void*>(weapon), targetModel);
        g_setModel(reinterpret_cast<void*>(viewmodel), targetModel);
        const bool weaponMeshReady = SafeSetMeshGroupMask(
            weapon, selection, 1);
        const bool viewmodelMeshReady = SafeSetMeshGroupMask(
            viewmodel, selection, 1);
        const bool compositeReady = SafeRefreshKnifeComposite(weapon, true);
        if (!g_appliedKnife.weaponModulesRefreshed) {
            bool modulesChanged = false;
            if (SafeRefreshWeaponModules(weapon, itemView, modulesChanged)) {
                g_appliedKnife.weaponModulesRefreshed = true;
                AppendLog(modulesChanged
                    ? "Knife modules: adjuntos reconstruidos sobre el modelo final."
                    : "Knife modules: adjuntos del modelo final ya actualizados.");
            } else {
                AppendLog("Knife modules: rutina nativa no disponible.");
            }
        }
        if (g_appliedKnife.finishApplied) {
            g_appliedKnife.compositeRefreshesRemaining = 6;
            g_appliedKnife.nextCompositeRefreshAt = now + 100;
        }
        char meshMessage[160]{};
        StringCchPrintfA(meshMessage, _countof(meshMessage),
            "Knife finish: mesh groups weapon=%s viewmodel=%s "
            "composite=%s.",
            weaponMeshReady ? "yes" : "no",
            viewmodelMeshReady ? "yes" : "no",
            compositeReady ? "yes" : "no");
        AppendLog(meshMessage);
        char observedWeapon[260]{};
        char observedViewmodel[260]{};
        (void)ReadPawnModel(weapon, selection, observedWeapon,
            _countof(observedWeapon));
        (void)ReadPawnModel(viewmodel, selection, observedViewmodel,
            _countof(observedViewmodel));
        g_appliedKnife.weaponApplied =
            strcmp(observedWeapon, targetModel) == 0;
        g_appliedKnife.viewmodelApplied =
            strcmp(observedViewmodel, targetModel) == 0;
        const bool applied = g_appliedKnife.weaponApplied &&
            g_appliedKnife.viewmodelApplied;
        g_appliedKnife.activeLastCheck = applied;
        LogKnifeModelControl(applied ? "applied-subclass" : "apply-failed",
            weapon, viewmodel, targetModel, observedWeapon, observedViewmodel);
        InterlockedExchange(&g_knifeModelApplied,
            (g_appliedKnife.subclassApplied ||
                g_appliedKnife.qualityApplied ||
                g_appliedKnife.finishApplied ||
                g_appliedKnife.itemIdHighApplied ||
                g_appliedKnife.weaponApplied ||
                g_appliedKnife.viewmodelApplied) ? 1 : 0);
        if (!applied)
            RestoreAppliedKnife(selection, "restored-partial-apply");
    }

    bool IsExecutableAddress(uintptr_t address) {
        if (!address) return false;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &info,
            sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (info.Protect & executable) != 0;
    }

    void* ResolveEventManager(void* clientInterface) {
        uintptr_t vtable = 0;
        uintptr_t eventRegistrationFunction = 0;
        if (!clientInterface ||
            !SafeRead(reinterpret_cast<uintptr_t>(clientInterface), vtable) ||
            !vtable || !SafeRead(vtable + 14 * sizeof(uintptr_t),
                eventRegistrationFunction) ||
            !IsExecutableAddress(eventRegistrationFunction))
            return nullptr;

        const uintptr_t instruction = eventRegistrationFunction + 0x3E;
        unsigned char prefix[3]{};
        int32_t displacement = 0;
        if (!IsReadableMemory(reinterpret_cast<const void*>(instruction), 7) ||
            !SafeRead(instruction, prefix) ||
            !SafeRead(instruction + 3, displacement) ||
            prefix[0] != 0x48 || prefix[1] != 0x8B ||
            (prefix[2] & 0xC7) != 0x05) {
            char message[192]{};
            StringCchPrintfA(message, _countof(message),
                "Killfeed events: referencia no valida bytes=%02X %02X %02X.",
                prefix[0], prefix[1], prefix[2]);
            AppendLog(message);
            return nullptr;
        }

        const uintptr_t managerGlobal = instruction + 7 + displacement;
        uintptr_t manager = 0;
        uintptr_t managerVtable = 0;
        uintptr_t addListener = 0;
        uintptr_t findListener = 0;
        uintptr_t removeListener = 0;
        if (!SafeRead(managerGlobal, manager) || !manager ||
            !SafeRead(manager, managerVtable) || !managerVtable ||
            !SafeRead(managerVtable + 3 * sizeof(uintptr_t), addListener) ||
            !SafeRead(managerVtable + 4 * sizeof(uintptr_t), findListener) ||
            !SafeRead(managerVtable + 5 * sizeof(uintptr_t), removeListener) ||
            !IsExecutableAddress(addListener) ||
            !IsExecutableAddress(findListener) ||
            !IsExecutableAddress(removeListener))
            return nullptr;
        return reinterpret_cast<void*>(manager);
    }

    void InventoryEventListener::FireGameEvent(void* event) {
        // When the pre-hook is active the registered listener is fallback only.
        if (g_fireEventClientSideSlot && !g_handlingKillFeedPreHook)
            return;
        if (!event) return;

        using GetNameFn = const char* (__fastcall*)(void*);
        using GetStringFn = const char* (__fastcall*)(
            void*, const StringToken&, const char*);
        using GetControllerIdFn = void(__fastcall*)(
            void*, int&, EventBuffer*);
        using SetStringFn = void(__fastcall*)(
            void*, const StringToken&, const char*);
        const GetNameFn getName = GetVirtualFunction<GetNameFn>(event, 1);
        const GetStringFn getString = GetVirtualFunction<GetStringFn>(
            event, 10);
        const GetControllerIdFn getControllerId =
            GetVirtualFunction<GetControllerIdFn>(event, 15);
        const SetStringFn setString = GetVirtualFunction<SetStringFn>(event, 24);
        if (!getName || !getControllerId) return;

        const char* eventName = getName(event);
        if (!eventName || (strcmp(eventName, "player_death") != 0 &&
            strcmp(eventName, "round_mvp") != 0))
            return;
        const bool isRoundMvp = strcmp(eventName, "round_mvp") == 0;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection)) return;
        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t localController = 0;
        uintptr_t pawn = 0;
        uintptr_t stride = 0;
        uintptr_t entityList = 0;
        if (!client || !ResolveAccountPawn(client, selection, localController,
            pawn, stride) || !SafeRead(reinterpret_cast<uintptr_t>(client) +
                selection.entityListOffset, entityList) || !entityList)
            return;

        if (isRoundMvp) {
            EventBuffer userBuffer{};
            userBuffer.name = "userid";
            int userControllerId = -1;
            getControllerId(event, userControllerId, &userBuffer);
            if (userControllerId < 0) return;
            const uintptr_t userController = ReadEntityByIndex(entityList,
                userControllerId + 1, stride);
            if (!userController || userController != localController)
                return;

            for (int index = 0; index < selection.musicKitItemCount;
                ++index) {
                const auto& item = selection.musicKitItems[index];
                if (item.localId != selection.musicKit || !item.statTrak)
                    continue;
                QueueMusicKitMvpObservation(
                    item.localId, item.statTrakCount);
                AppendLog(
                    "StatTrak event: MVP local pendiente de observacion estable.");
                break;
            }
            return;
        }

        if (!g_appliedKnife.subclassApplied || !getString || !setString)
            return;
        const KnifeModel* appliedKnife = FindKnifeModel(
            g_appliedKnife.definitionIndex);
        if (!appliedKnife || !appliedKnife->eventName) return;

        EventBuffer attackerBuffer{};
        attackerBuffer.name = "attacker";
        int attackerControllerId = -1;
        getControllerId(event, attackerControllerId, &attackerBuffer);
        if (attackerControllerId < 0) return;

        const uintptr_t attackerController = ReadEntityByIndex(entityList,
            attackerControllerId + 1, stride);
        if (!attackerController || attackerController != localController)
            return;

        const StringToken weaponToken = MakeStringToken("weapon");
        const StringToken weaponItemIdToken =
            MakeStringToken("weapon_itemid");
        const StringToken weaponFauxItemIdToken =
            MakeStringToken("weapon_fauxitemid");
        const char* weaponName = getString(event, weaponToken, "");
        if (!weaponName || (strcmp(weaponName, "knife") != 0 &&
            strcmp(weaponName, "knife_t") != 0))
            return;

        int localTeam = 0;
        EventBuffer victimBuffer{};
        victimBuffer.name = "userid";
        int victimControllerId = -1;
        getControllerId(event, victimControllerId, &victimBuffer);
        const uintptr_t victimController = victimControllerId >= 0
            ? ReadEntityByIndex(entityList, victimControllerId + 1, stride)
            : 0;
        int victimTeam = 0;
        const bool validEnemyKill = victimControllerId >= 0 &&
            victimControllerId != attackerControllerId && victimController &&
            SafeRead(pawn + selection.teamNumberOffset, localTeam) &&
            SafeRead(victimController + selection.teamNumberOffset,
                victimTeam) && localTeam >= 2 && localTeam <= 3 &&
            victimTeam >= 2 && victimTeam <= 3 && localTeam != victimTeam;
        if (validEnemyKill) {
            const uint64_t localId = localTeam == 2
                ? selection.terroristKnife
                : selection.counterTerroristKnife;
            const bool statTrak = localTeam == 2
                ? selection.terroristKnifeStatTrak
                : selection.counterTerroristKnifeStatTrak;
            const int statTrakCount = localTeam == 2
                ? selection.terroristKnifeStatTrakCount
                : selection.counterTerroristKnifeStatTrakCount;
            if (localId != 0 && statTrak) {
                QueueStatTrakIncrement(localId, statTrakCount);
                AppendLog("StatTrak event: baja local con cuchillo registrada.");
            }
        }

        const char* weaponItemId = getString(
            event, weaponItemIdToken, "0");
        const char* weaponFauxItemId = getString(
            event, weaponFauxItemIdToken, "0");
        char fauxItemId[32]{};
        const std::uint64_t fauxValue = 0xFFFFFFFF00000000ull |
            static_cast<std::uint32_t>(appliedKnife->definitionIndex);
        StringCchPrintfA(fauxItemId, _countof(fauxItemId), "%llu",
            static_cast<unsigned long long>(fauxValue));
        char message[320]{};
        StringCchPrintfA(message, _countof(message),
            "Killfeed events: knife -> %s "
            "(itemid=%s fauxitemid=%s).",
            appliedKnife->eventName,
            weaponItemId ? weaponItemId : "",
            weaponFauxItemId ? weaponFauxItemId : "");

        setString(event, weaponToken, appliedKnife->eventName);
        // Panorama resolves this visual identity before consulting `weapon`.
        // A picked-up knife can retain the default knife's item id here.
        setString(event, weaponItemIdToken, "0");
        setString(event, weaponFauxItemIdToken, fauxItemId);
        AppendLog(message);
    }

    bool __fastcall FireEventClientSideHook(void* self, void* event) {
        g_handlingKillFeedPreHook = true;
        g_inventoryEventListener.FireGameEvent(event);
        g_handlingKillFeedPreHook = false;
        const FireEventClientSideFn original = g_originalFireEventClientSide;
        return original ? original(self, event) : false;
    }

    bool InstallKillFeedListener(void* clientInterface) {
        if (g_killFeedListenerInstalled) return true;
        g_eventManager = ResolveEventManager(clientInterface);
        if (!g_eventManager) {
            AppendLog("Killfeed events: administrador no disponible.");
            return false;
        }

        using AddListenerFn = bool(__fastcall*)(
            void*, GameEventListener*, const char*, bool);
        using FindListenerFn = bool(__fastcall*)(
            void*, GameEventListener*, const char*);
        using RemoveListenerFn = void(__fastcall*)(
            void*, GameEventListener*);
        const AddListenerFn addListener =
            GetVirtualFunction<AddListenerFn>(g_eventManager, 3);
        const FindListenerFn findListener =
            GetVirtualFunction<FindListenerFn>(g_eventManager, 4);
        const RemoveListenerFn removeListener =
            GetVirtualFunction<RemoveListenerFn>(g_eventManager, 5);
        if (!addListener || !findListener || !removeListener) {
            g_eventManager = nullptr;
            return false;
        }

        const bool deathAdded = addListener(g_eventManager,
            &g_inventoryEventListener, "player_death", false);
        const bool deathFound = deathAdded && findListener(g_eventManager,
            &g_inventoryEventListener, "player_death");
        const bool mvpAdded = addListener(g_eventManager,
            &g_inventoryEventListener, "round_mvp", false);
        const bool mvpFound = mvpAdded && findListener(g_eventManager,
            &g_inventoryEventListener, "round_mvp");
        if (!deathFound) {
            if (deathAdded || mvpAdded)
                removeListener(g_eventManager, &g_inventoryEventListener);
            g_eventManager = nullptr;
            AppendLog("Killfeed events: listener no instalado.");
            return false;
        }
        g_killFeedListenerInstalled = true;
        AppendLog(mvpFound
            ? "Inventory events: player_death y round_mvp instalados."
            : "Inventory events: player_death instalado; round_mvp depende del pre-hook.");

        void** managerVtable = nullptr;
        void* originalFireEvent = nullptr;
        if (SafeRead(reinterpret_cast<uintptr_t>(g_eventManager),
            managerVtable) && managerVtable &&
            SafeRead(reinterpret_cast<uintptr_t>(managerVtable + 8),
                originalFireEvent) && IsExecutableAddress(originalFireEvent) &&
            SetVtableEntry(managerVtable + 8,
                reinterpret_cast<void*>(&FireEventClientSideHook),
                reinterpret_cast<void**>(&g_originalFireEventClientSide))) {
            g_fireEventClientSideSlot = managerVtable + 8;
            AppendLog("Killfeed events: pre-hook FireEventClientSide instalado.");
        } else {
            AppendLog("Killfeed events: pre-hook no disponible; listener de respaldo activo.");
        }
        return true;
    }

    void RemoveKillFeedListener() {
        if (!g_killFeedListenerInstalled || !g_eventManager) return;
        if (g_fireEventClientSideSlot && g_originalFireEventClientSide) {
            void* current = nullptr;
            if (SafeRead(reinterpret_cast<uintptr_t>(g_fireEventClientSideSlot),
                current) && current ==
                reinterpret_cast<void*>(&FireEventClientSideHook)) {
                (void)SetVtableEntry(g_fireEventClientSideSlot,
                    reinterpret_cast<void*>(g_originalFireEventClientSide),
                    nullptr);
                AppendLog("Killfeed events: pre-hook retirado.");
            }
        }
        g_fireEventClientSideSlot = nullptr;
        g_originalFireEventClientSide = nullptr;
        using RemoveListenerFn = void(__fastcall*)(
            void*, GameEventListener*);
        const RemoveListenerFn removeListener =
            GetVirtualFunction<RemoveListenerFn>(g_eventManager, 5);
        if (removeListener)
            removeListener(g_eventManager, &g_inventoryEventListener);
        g_killFeedListenerInstalled = false;
        g_eventManager = nullptr;
        AppendLog("Killfeed events: listener retirado.");
    }

    bool ConnectAndReadSnapshot(uint64_t& lastSnapshotHash) {
        if (!WaitNamedPipeW(kPipeName, 500)) return false;
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) return false;

        constexpr char hello[] =
            "{\"protocol_version\":1,\"message_type\":\"client.hello\","
            "\"request_id\":1,\"payload\":{\"client_session_id\":"
            "\"inventory-bridge\"}}";
        const uint32_t helloBytes = static_cast<uint32_t>(sizeof(hello) - 1);
        bool ok = WriteExact(pipe, &helloBytes, sizeof(helloBytes)) &&
            WriteExact(pipe, hello, helloBytes);

        uint32_t responseBytes = 0;
        if (ok) ok = ReadExact(pipe, &responseBytes, sizeof(responseBytes)) &&
            responseBytes > 0 && responseBytes <= kMaxFrameBytes;

        char* response = nullptr;
        if (ok) {
            response = static_cast<char*>(HeapAlloc(
                GetProcessHeap(), HEAP_ZERO_MEMORY, responseBytes + 1));
            ok = response && ReadExact(pipe, response, responseBytes);
        }
        if (ok) {
            const uint64_t snapshotHash = HashText(response);
            if (snapshotHash != lastSnapshotHash) {
                PublishInventorySelection(response);
                PublishPanoramaInventorySnapshot(response);
                AppendLog(lastSnapshotHash == 0
                    ? "IPC snapshot recibido correctamente."
                    : "IPC snapshot actualizado.");
                if (lastSnapshotHash == 0) AppendLog(response);
                lastSnapshotHash = snapshotHash;
            }
        }
        if (response) HeapFree(GetProcessHeap(), 0, response);
        CloseHandle(pipe);
        return ok;
    }

    bool SendPanoramaInventoryCommand(
        const PanoramaInventoryCommand& command, uint64_t requestId,
        bool& accepted) {
        accepted = false;
        if (command.type == PanoramaInventoryCommandType::None ||
            command.localId == 0 || !WaitNamedPipeW(kPipeName, 500))
            return false;
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) return false;

        constexpr char hello[] =
            "{\"protocol_version\":1,\"message_type\":\"client.hello\"," 
            "\"request_id\":1,\"payload\":{\"client_session_id\":"
            "\"inventory-bridge\"}}";
        const uint32_t helloBytes = static_cast<uint32_t>(sizeof(hello) - 1);
        bool ok = WriteExact(pipe, &helloBytes, sizeof(helloBytes)) &&
            WriteExact(pipe, hello, helloBytes);

        uint32_t responseBytes = 0;
        if (ok) ok = ReadExact(pipe, &responseBytes, sizeof(responseBytes)) &&
            responseBytes > 0 && responseBytes <= kMaxFrameBytes;
        char* response = nullptr;
        if (ok) {
            response = static_cast<char*>(HeapAlloc(
                GetProcessHeap(), HEAP_ZERO_MEMORY, responseBytes + 1));
            ok = response && ReadExact(pipe, response, responseBytes);
        }
        if (response) {
            HeapFree(GetProcessHeap(), 0, response);
            response = nullptr;
        }

        char message[320]{};
        const char* messageType = command.type ==
            PanoramaInventoryCommandType::Equip
            ? "inventory.equip" : "inventory.close_reveal";
        if (ok) ok = SUCCEEDED(StringCchPrintfA(message, _countof(message),
            "{\"protocol_version\":1,\"message_type\":\"%s\"," 
            "\"request_id\":%llu,\"payload\":{\"local_id\":%llu}}",
            messageType, static_cast<unsigned long long>(requestId),
            static_cast<unsigned long long>(command.localId)));
        const uint32_t messageBytes = static_cast<uint32_t>(lstrlenA(message));
        if (ok) ok = messageBytes > 0 &&
            WriteExact(pipe, &messageBytes, sizeof(messageBytes)) &&
            WriteExact(pipe, message, messageBytes);

        responseBytes = 0;
        if (ok) ok = ReadExact(pipe, &responseBytes, sizeof(responseBytes)) &&
            responseBytes > 0 && responseBytes <= kMaxFrameBytes;
        if (ok) {
            response = static_cast<char*>(HeapAlloc(
                GetProcessHeap(), HEAP_ZERO_MEMORY, responseBytes + 1));
            ok = response && ReadExact(pipe, response, responseBytes);
        }
        if (ok && response)
            accepted = strstr(response, "\"success\":true") != nullptr;
        if (response) HeapFree(GetProcessHeap(), 0, response);
        CloseHandle(pipe);
        return ok;
    }

    bool SendNativeLoadoutCommand(
        const NativeLoadoutCommand& command, uint64_t requestId,
        bool& accepted) {
        accepted = false;
        if (command.type == NativeLoadoutCommandType::None ||
            command.localId == 0 || command.team < 0 || command.team > 2 ||
            !WaitNamedPipeW(kPipeName, 500))
            return false;
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) return false;

        constexpr char hello[] =
            "{\"protocol_version\":1,\"message_type\":\"client.hello\"," 
            "\"request_id\":1,\"payload\":{\"client_session_id\":"
            "\"native-loadout-bridge\"}}";
        const uint32_t helloBytes = static_cast<uint32_t>(sizeof(hello) - 1);
        bool ok = WriteExact(pipe, &helloBytes, sizeof(helloBytes)) &&
            WriteExact(pipe, hello, helloBytes);

        uint32_t responseBytes = 0;
        if (ok) ok = ReadExact(pipe, &responseBytes, sizeof(responseBytes)) &&
            responseBytes > 0 && responseBytes <= kMaxFrameBytes;
        char* response = nullptr;
        if (ok) {
            response = static_cast<char*>(HeapAlloc(
                GetProcessHeap(), HEAP_ZERO_MEMORY, responseBytes + 1));
            ok = response && ReadExact(pipe, response, responseBytes);
        }
        if (response) {
            HeapFree(GetProcessHeap(), 0, response);
            response = nullptr;
        }

        char message[384]{};
        const char* messageType = command.type ==
            NativeLoadoutCommandType::Equip
            ? "inventory.equip" : "inventory.unequip";
        if (ok) ok = SUCCEEDED(StringCchPrintfA(message, _countof(message),
            "{\"protocol_version\":1,\"message_type\":\"%s\"," 
            "\"request_id\":%llu,\"payload\":{\"local_id\":%llu,"
            "\"team\":%d}}",
            messageType, static_cast<unsigned long long>(requestId),
            static_cast<unsigned long long>(command.localId), command.team));
        const uint32_t messageBytes = static_cast<uint32_t>(lstrlenA(message));
        if (ok) ok = messageBytes > 0 &&
            WriteExact(pipe, &messageBytes, sizeof(messageBytes)) &&
            WriteExact(pipe, message, messageBytes);

        responseBytes = 0;
        if (ok) ok = ReadExact(pipe, &responseBytes, sizeof(responseBytes)) &&
            responseBytes > 0 && responseBytes <= kMaxFrameBytes;
        if (ok) {
            response = static_cast<char*>(HeapAlloc(
                GetProcessHeap(), HEAP_ZERO_MEMORY, responseBytes + 1));
            ok = response && ReadExact(pipe, response, responseBytes);
        }
        if (ok && response)
            accepted = strstr(response, "\"success\":true") != nullptr;
        if (response) HeapFree(GetProcessHeap(), 0, response);
        CloseHandle(pipe);
        return ok;
    }

    bool SendStatTrakCommand(
        const StatTrakCommand& command, uint64_t requestId,
        bool& accepted) {
        accepted = false;
        if (command.localId == 0 || command.count < 0 ||
            !WaitNamedPipeW(kPipeName, 500))
            return false;
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) return false;

        constexpr char hello[] =
            "{\"protocol_version\":1,\"message_type\":\"client.hello\"," 
            "\"request_id\":1,\"payload\":{\"client_session_id\":"
            "\"stattrak-bridge\"}}";
        const uint32_t helloBytes = static_cast<uint32_t>(sizeof(hello) - 1);
        bool ok = WriteExact(pipe, &helloBytes, sizeof(helloBytes)) &&
            WriteExact(pipe, hello, helloBytes);
        uint32_t responseBytes = 0;
        if (ok) ok = ReadExact(pipe, &responseBytes, sizeof(responseBytes)) &&
            responseBytes > 0 && responseBytes <= kMaxFrameBytes;
        char* response = nullptr;
        if (ok) {
            response = static_cast<char*>(HeapAlloc(
                GetProcessHeap(), HEAP_ZERO_MEMORY, responseBytes + 1));
            ok = response && ReadExact(pipe, response, responseBytes);
        }
        if (response) {
            HeapFree(GetProcessHeap(), 0, response);
            response = nullptr;
        }

        char message[384]{};
        if (ok) ok = SUCCEEDED(StringCchPrintfA(message, _countof(message),
            "{\"protocol_version\":1,\"message_type\":"
            "\"inventory.update_stattrak\",\"request_id\":%llu,"
            "\"payload\":{\"local_id\":%llu,\"stattrak_count\":%d}}",
            static_cast<unsigned long long>(requestId),
            static_cast<unsigned long long>(command.localId), command.count));
        const uint32_t messageBytes = static_cast<uint32_t>(lstrlenA(message));
        if (ok) ok = messageBytes > 0 &&
            WriteExact(pipe, &messageBytes, sizeof(messageBytes)) &&
            WriteExact(pipe, message, messageBytes);

        responseBytes = 0;
        if (ok) ok = ReadExact(pipe, &responseBytes, sizeof(responseBytes)) &&
            responseBytes > 0 && responseBytes <= kMaxFrameBytes;
        if (ok) {
            response = static_cast<char*>(HeapAlloc(
                GetProcessHeap(), HEAP_ZERO_MEMORY, responseBytes + 1));
            ok = response && ReadExact(pipe, response, responseBytes);
        }
        if (ok && response)
            accepted = strstr(response, "\"success\":true") != nullptr;
        if (response) HeapFree(GetProcessHeap(), 0, response);
        CloseHandle(pipe);
        return ok;
    }

    DWORD WINAPI BridgeWorker(void*) {
        HANDLE singleton = CreateMutexW(nullptr, FALSE, kMutexName);
        if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (singleton) CloseHandle(singleton);
            return 0;
        }

        wchar_t tempPath[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, tempPath)) {
            wchar_t logPath[MAX_PATH]{};
            lstrcpynW(logPath, tempPath, MAX_PATH);
            lstrcatW(logPath, L"OverlayAI.InventoryBridge.log");
            DeleteFileW(logPath);
        }
        AppendLog("InventoryBridge iniciado.");
        g_weaponSkinSocacheAllowed = true;
        AppendLog(g_weaponSkinSocacheAllowed
            ? "Weapon skin SOCache: habilitado para sesion ."
            : "Weapon skin SOCache: bloqueado; falta ");
        {
            char message[96]{};
            StringCchPrintfA(message, _countof(message),
                "Knife runtime table: %llu modelos.",
                static_cast<unsigned long long>(_countof(kKnifeModels)));
            AppendLog(message);
        }

        char compatibility[384]{};
        if (BridgeCompatibility::FormatStartupDescriptor(
            compatibility, _countof(compatibility),
            GetModuleHandleW(L"client.dll"),
            GetModuleHandleW(L"engine2.dll"),
            GetModuleHandleW(L"panorama.dll"))) {
            AppendLog(compatibility);
        }

        void* clientInterface = nullptr;
        const bool clientReady = ProbeInterface(
            L"client.dll", "Source2Client002",
            "Source2Client002: OK", "Source2Client002: FAIL",
            &clientInterface);
        const bool engineReady = ProbeInterface(
            L"engine2.dll", "Source2EngineToClient001",
            "Source2EngineToClient001: OK", "Source2EngineToClient001: FAIL");
        void* panoramaInterface = nullptr;
        const bool panoramaReady = ProbeInterface(
            L"panorama.dll", "PanoramaUIEngine001",
            "PanoramaUIEngine001: OK", "PanoramaUIEngine001: FAIL",
            &panoramaInterface);
        ProbeCriticalPatterns();
        (void)InitializeInventorySocacheDiagnostics(clientInterface);
        LogPanoramaReadOnlyDiagnostics(panoramaInterface);
        const bool panoramaMountReady = panoramaReady &&
            InitializePanoramaMount(panoramaInterface);

        HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, kReadyEventName);
        if (readyEvent && clientReady && engineReady && panoramaReady)
            SetEvent(readyEvent);

        const bool frameStageHooked = clientReady &&
            InstallFrameStageDiagnostic(clientInterface);
        if (!frameStageHooked)
            AppendLog("FrameStage agent hook: no instalado.");
        else
            (void)InstallKillFeedListener(clientInterface);
        if (frameStageHooked && panoramaMountReady &&
            IsPanoramaProbeEnabled())
            RequestPanoramaProbeCreate();

        uint64_t lastSnapshotHash = 0;
        uint64_t nextInventoryRequestId = GetTickCount64();
        if (nextInventoryRequestId < 2) nextInventoryRequestId = 2;
        int consecutiveIpcFailures = 0;
        bool ipcUnavailable = false;
        while (WaitForSingleObject(g_stopEvent, kSnapshotRefreshMs) ==
            WAIT_TIMEOUT) {
            StatTrakCommand statTrakCommand{};
            if (ConsumeStatTrakCommand(statTrakCommand)) {
                bool accepted = false;
                const bool transported = SendStatTrakCommand(
                    statTrakCommand, nextInventoryRequestId++, accepted);
                if (!transported) {
                    RequeueStatTrakCommand(statTrakCommand);
                    AppendLog("StatTrak: transporte fallo; reintento pendiente.");
                } else if (!accepted) {
                    AppendLog("StatTrak: actualizacion rechazada por el host.");
                } else {
                    lastSnapshotHash = 0;
                    AppendLog("StatTrak: contador sincronizado con el host.");
                }
            }
            NativeLoadoutCommand nativeCommand{};
            if (ConsumeNativeLoadoutCommand(nativeCommand)) {
                bool accepted = false;
                const bool transported = SendNativeLoadoutCommand(
                    nativeCommand, nextInventoryRequestId++, accepted);
                if (!transported) {
                    RequeueNativeLoadoutCommand(nativeCommand);
                    AppendLog("Native loadout: transporte fallo; reintento pendiente.");
                } else if (!accepted) {
                    AppendLog("Native loadout: accion rechazada por el host.");
                } else {
                    lastSnapshotHash = 0;
                    AppendLog("Native loadout: accion aceptada por el host.");
                }
            }
            PanoramaInventoryCommand panoramaCommand{};
            if (ConsumePanoramaInventoryCommand(panoramaCommand)) {
                if (panoramaCommand.type ==
                    PanoramaInventoryCommandType::NativeEquipItem) {
                    uint64_t localId = 0;
                    if (ResolveInventorySocacheGeneratedLocalId(
                            panoramaCommand.localId, localId)) {
                        panoramaCommand.type =
                            PanoramaInventoryCommandType::Equip;
                        panoramaCommand.localId = localId;
                        AppendLog(
                            "Panorama native equip: Item ID resuelto a Local ID.");
                    } else {
                        AppendLog(
                            "Panorama native equip: articulo ajeno ignorado.");
                        panoramaCommand = {};
                    }
                }
                bool accepted = false;
                const bool transported = panoramaCommand.type !=
                        PanoramaInventoryCommandType::None &&
                    SendPanoramaInventoryCommand(panoramaCommand,
                        nextInventoryRequestId++, accepted);
                if (!transported) {
                    if (panoramaCommand.type !=
                        PanoramaInventoryCommandType::None) {
                        RequeuePanoramaInventoryCommand(panoramaCommand);
                        AppendLog("Panorama commands: transporte fallo; reintento pendiente.");
                    }
                } else if (!accepted) {
                    RequestPanoramaRevealRefresh();
                    AppendLog("Panorama commands: accion rechazada por el host.");
                } else {
                    lastSnapshotHash = 0;
                    AppendLog("Panorama commands: accion aceptada por el host.");
                }
            }
            if (ConnectAndReadSnapshot(lastSnapshotHash)) {
                if (ipcUnavailable)
                    AppendLog("IPC reconectado; snapshot y Panorama recuperados.");
                consecutiveIpcFailures = 0;
                ipcUnavailable = false;
            } else if (++consecutiveIpcFailures >=
                kIpcFailuresBeforeRestore) {
                if (!ipcUnavailable) {
                    DisablePublishedInventorySelection();
                    AppendLog(
                        "IPC no disponible; runtime restaurado, bridge en espera.");
                }
                ipcUnavailable = true;
                consecutiveIpcFailures = kIpcFailuresBeforeRestore;
                lastSnapshotHash = 0;
            }
        }

        RemoveKillFeedListener();
        DisablePublishedInventorySelection();
        RestoreMusicKitRuntimeControl();
        const ULONGLONG restoreDeadline = GetTickCount64() + 2000;
        while ((InterlockedCompareExchange(
            &g_agentModelApplied, 0, 0) != 0 ||
            InterlockedCompareExchange(&g_knifeModelApplied, 0, 0) != 0 ||
            InterlockedCompareExchange(&g_gloveModelApplied, 0, 0) != 0) &&
            GetTickCount64() < restoreDeadline)
            Sleep(10);
        RequestPanoramaProbeDestroy();
        const ULONGLONG panoramaDestroyDeadline = GetTickCount64() + 1000;
        while (IsPanoramaProbeMounted() &&
            GetTickCount64() < panoramaDestroyDeadline)
            Sleep(10);
        RemoveGlovePawnUpdateDiagnostic();
        RemoveGloveProcessDiagnostic();
        RemoveGloveRenderDiagnostic();
        RemoveGloveAttachDiagnostic();
        RemoveGloveSpawnDiagnostic();
        RemoveFrameStageDiagnostic();
        ShutdownInventorySocache();
        ShutdownPanoramaMount();
        AppendLog("InventoryBridge detenido.");
        if (readyEvent) CloseHandle(readyEvent);
        CloseHandle(singleton);
        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, kStopEventName);
        if (!g_stopEvent) return FALSE;
        ResetEvent(g_stopEvent);
        HANDLE worker = CreateThread(nullptr, 0, BridgeWorker, nullptr, 0, nullptr);
        if (!worker) {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
            return FALSE;
        }
        CloseHandle(worker);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_stopEvent) SetEvent(g_stopEvent);
    }
    return TRUE;
}
