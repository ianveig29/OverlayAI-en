#include <windows.h>
#include <strsafe.h>
#include <intrin.h>

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
    constexpr ULONGLONG kWeaponSkinStableCheckIntervalMs = 100;
    constexpr ULONGLONG kWeaponSkinEarlyProbeIntervalMs = 16;
    constexpr ULONGLONG kWeaponSkinMaterialRetryIntervalMs = 100;
    constexpr ULONGLONG kWeaponSkinSlowMaterialRetryIntervalMs = 500;
    constexpr ULONGLONG kWeaponSkinSceneRetryIntervalMs = 100;
    constexpr ULONGLONG kWeaponSkinStatTrakModuleRetryIntervalMs = 250;
    constexpr ULONGLONG kWeaponSkinStatTrakModuleSlowRetryIntervalMs = 1000;
    constexpr int kWeaponSkinFastMaterialRetryAttempts = 4;
    constexpr int kWeaponSkinFastStatTrakModuleRetryAttempts = 4;
    constexpr ULONGLONG kWeaponSkinHudRefreshDelayMs = 250;
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
    constexpr int kWeaponSkinEarlyFrameStage = 3;
    constexpr int kAgentFrameStage = 6;

    constexpr uint64_t kModernWeaponMeshGroupMask = 1;
    constexpr uint64_t kLegacyWeaponMeshGroupMask = 2;
    constexpr bool kInstallWeaponMaterialDiagnostics = true;
    constexpr bool kPrepareWeaponSkinInsideNativeAlias = true;

    enum class WeaponSkinRuntimePhase {
        EarlyNetUpdate,
        RenderEnd,
    };

    constexpr uint64_t WeaponMeshGroupMask(bool legacyModel) noexcept {
        return legacyModel ? kLegacyWeaponMeshGroupMask
                           : kModernWeaponMeshGroupMask;
    }

    float EncodeIntegerAttributeValue(uint32_t value) noexcept {
        float encoded = 0.0f;
        static_assert(sizeof(encoded) == sizeof(value));
        std::memcpy(&encoded, &value, sizeof(encoded));
        return encoded;
    }

    HANDLE g_stopEvent = nullptr;
    using FrameStageFn = void(__fastcall*)(void*, int);
    using SetModelFn = void* (__fastcall*)(void*, const char*);
    using SetBodyGroupFn = void(__fastcall*)(void*, uint64_t, uint64_t);
    using SetMeshGroupMaskFn = void(__fastcall*)(void*, uint64_t);
    using SetItemViewAttributeByNameFn = void(__fastcall*)(
        void*, const char*, float);
    using AddOrSetAttributeByNameFn = void(__fastcall*)(
        void*, const char*, float);
    using UpdateCompositeMaterialFn = void(__fastcall*)(void*, bool);
    using UpdateCompositeMaterialSetFn = void(__fastcall*)(void*, bool);
    using UpdateWeaponSkinFn = void(__fastcall*)(void*, bool);
    using PrepareWeaponSkinFn = void(__fastcall*)(void*, bool);
    using BuildWeaponMaterialOverridesFn = bool(__fastcall*)(
        void*, void*, void*, bool);
    using ItemViewGetTextureSeedFn = int(__fastcall*)(void*);
    using SceneNodePostDataUpdateFn = void(__fastcall*)(
        void*, int64_t, int64_t);
    using UpdateSubclassFn = void(__fastcall*)(void*);
    using UpdateWeaponGraphControllerFn = void(__fastcall*)(void*, void*);
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
    SetItemViewAttributeByNameFn g_setItemViewAttributeByName = nullptr;
    AddOrSetAttributeByNameFn g_addOrSetAttributeByName = nullptr;
    UpdateCompositeMaterialFn g_updateCompositeMaterial = nullptr;
    UpdateCompositeMaterialSetFn g_updateCompositeMaterialSet = nullptr;
    UpdateWeaponSkinFn g_updateWeaponSkin = nullptr;
    UpdateCompositeMaterialFn g_observedCompositeMaterial = nullptr;
    UpdateCompositeMaterialSetFn g_observedCompositeMaterialSet = nullptr;
    UpdateWeaponSkinFn g_observedUpdateWeaponSkin = nullptr;
    UpdateWeaponSkinFn g_observedUpdateWeaponSkinAlias = nullptr;
    PrepareWeaponSkinFn g_observedPrepareWeaponSkin = nullptr;
    BuildWeaponMaterialOverridesFn g_observedBuildWeaponMaterialOverrides =
        nullptr;
    ItemViewGetTextureSeedFn g_originalItemViewGetTextureSeed = nullptr;
    void** g_itemViewGetTextureSeedSlot = nullptr;
    std::atomic<uint32_t> g_weaponSkinLifecycleGeneration{ 0 };
    uintptr_t g_compositeMaterialOwnerOffset = 0;
    UpdateSubclassFn g_updateSubclass = nullptr;
    UpdateWeaponGraphControllerFn g_updateWeaponGraphController = nullptr;
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
    ULONGLONG g_nextWeaponObserverLogAt = 0;
    ULONGLONG g_nextWeaponSkinEarlyProbeAt = 0;



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
            int equippedTeam = 0;
            bool legacyModel = false;
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
    struct AppliedWeaponSkinState {
        uintptr_t pawn = 0;
        uintptr_t entityStride = 0;
        uintptr_t weapon = 0;
        uint32_t weaponHandle = 0;
        uint64_t localId = 0;
        int definitionIndex = 0;
        int paintKit = 0;
        int seed = 0;
        float wear = 0.15f;
        int statTrak = -1;
        uint8_t quality = 4;
        uint64_t generatedItemId = 0;
        uintptr_t itemView = 0;
        uintptr_t itemIdOffset = 0;
        uintptr_t itemIdHighOffset = 0;
        uintptr_t itemIdLowOffset = 0;
        uintptr_t initializedOffset = 0;
        uintptr_t fallbackPaintKitOffset = 0;
        uintptr_t fallbackSeedOffset = 0;
        uintptr_t fallbackWearOffset = 0;
        uintptr_t fallbackStatTrakOffset = 0;
        uint16_t originalDefinition = 0;
        int32_t originalQuality = 0;
        uint64_t originalItemId = 0;
        uint32_t originalItemIdHigh = 0;
        uint32_t originalItemIdLow = 0;
        uint32_t originalAccountId = 0;
        uint8_t originalInitialized = 0;
        int originalPaintKit = 0;
        int originalSeed = 0;
        float originalWear = 0.0f;
        int originalStatTrak = -1;
        bool originalRestoreCustomMaterial = false;
        bool originalDisallowSoc = false;
        ULONGLONG nextUpdateAt = 0;
        ULONGLONG hudRefreshAt = 0;
        int hudRefreshAttempts = 0;
        bool hudRefreshed = false;
        bool sceneUpdatePending = false;
        ULONGLONG sceneUpdateAt = 0;
        int sceneUpdateAttempts = 0;
        bool materialRefreshPending = false;
        ULONGLONG materialRefreshAt = 0;
        int materialRefreshAttempts = 0;
        bool statTrakModuleRefreshPending = false;
        ULONGLONG statTrakModuleRefreshAt = 0;
        int statTrakModuleRefreshAttempts = 0;
        float spawnTimeIndex = 0.0f;
        uint32_t lifecycleGeneration = 0;
        bool applied = false;
    };
    AppliedWeaponSkinState g_appliedWeaponSkin{};
    struct WeaponMaterialInlineHook {
        void* target = nullptr;
        void* trampoline = nullptr;
        SIZE_T patchLength = 0;
        unsigned char original[20]{};
    };
    WeaponMaterialInlineHook g_compositeMaterialHook{};
    WeaponMaterialInlineHook g_compositeMaterialSetHook{};
    WeaponMaterialInlineHook g_updateWeaponSkinHook{};
    WeaponMaterialInlineHook g_updateWeaponSkinAliasHook{};
    WeaponMaterialInlineHook g_prepareWeaponSkinHook{};
    WeaponMaterialInlineHook g_buildWeaponMaterialOverridesHook{};
    std::atomic<unsigned> g_weaponMaterialHookLogs{ 0 };
    std::atomic<unsigned> g_weaponStatTrakEventLogs{ 0 };
    std::atomic<bool> g_weaponSkinMidpointLogged{ false };
    std::atomic<bool> g_weaponMaterialOverridesLogged{ false };
    constexpr int kMaxRetainedWeaponSkins = 32;
    AppliedWeaponSkinState g_retainedWeaponSkins[
        kMaxRetainedWeaponSkins]{};
    struct WeaponTextureSeedOverrideEntry {
        uintptr_t itemView = 0;
        uintptr_t itemIdOffset = 0;
        uint64_t generatedItemId = 0;
        int effectiveSeed = 0;
        bool validateIdentity = false;
    };
    constexpr int kMaxWeaponTextureSeedOverrides =
        kMaxRetainedWeaponSkins + 1;
    SRWLOCK g_weaponTextureSeedOverrideLock = SRWLOCK_INIT;
    WeaponTextureSeedOverrideEntry g_weaponTextureSeedOverrides[
        kMaxWeaponTextureSeedOverrides]{};
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
    volatile LONG g_weaponSkinApplied = 0;
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
    void RunWeaponSkinRuntimeControl(WeaponSkinRuntimePhase phase);
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
                value = 0;
                field = strstr(itemStart, "\"equipped_team\":");
                if (field && field < itemEnd &&
                    ReadJsonUnsigned(field, "equipped_team", value))
                    item.equippedTeam = static_cast<int>(value & 0x3);
                field = strstr(itemStart, "\"legacy_model\":");
                if (field && field < itemEnd)
                    (void)ReadJsonBool(field, "legacy_model",
                        item.legacyModel);

                const uint64_t parts[] = {
                    item.localId,
                    static_cast<uint64_t>(item.definitionIndex),
                    static_cast<uint64_t>(item.paintKit),
                    static_cast<uint64_t>(item.seed),
                    static_cast<uint64_t>(item.statTrak),
                    static_cast<uint64_t>(item.statTrakCount),
                    static_cast<uint64_t>(item.quality),
                    static_cast<uint64_t>(item.rarity),
                    static_cast<uint64_t>(item.equippedTeam),
                    static_cast<uint64_t>(item.legacyModel)
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

    inline bool IsValidUserPointer(const void* address, SIZE_T size = 1) noexcept {
        const uintptr_t ptr = reinterpret_cast<uintptr_t>(address);
        return ptr >= 0x10000 && ptr <= (0x7FFFFFFEFFFFull - size);
    }

    bool IsReadableMemory(const void* address, SIZE_T size) {
        if (!IsValidUserPointer(address, size)) return false;
        __try {
            volatile const char* probe = reinterpret_cast<const char*>(address);
            char probeByte = probe[0];
            if (size > 1) probeByte = probe[size - 1];
            (void)probeByte;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    template <typename T>
    bool SafeRead(uintptr_t address, T& value) {
        if (!IsValidUserPointer(reinterpret_cast<const void*>(address), sizeof(T)))
            return false;
        __try {
            CopyMemory(&value, reinterpret_cast<const void*>(address), sizeof(T));
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool IsWritableMemory(void* address, SIZE_T size) {
        if (!IsValidUserPointer(address, size)) return false;
        __try {
            volatile char* probe = reinterpret_cast<char*>(address);
            char probeByte = probe[0];
            probe[0] = probeByte;
            if (size > 1) {
                probeByte = probe[size - 1];
                probe[size - 1] = probeByte;
            }
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    template <typename T>
    bool SafeWrite(uintptr_t address, const T& value) {
        if (!IsValidUserPointer(reinterpret_cast<const void*>(address), sizeof(T)))
            return false;
        __try {
            CopyMemory(reinterpret_cast<void*>(address), &value, sizeof(T));
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
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
        if (!IsValidUserPointer(reinterpret_cast<const void*>(address), 1)) return false;
        __try {
            const char* src = reinterpret_cast<const char*>(address);
            SIZE_T index = 0;
            for (; index + 1 < capacity; ++index) {
                const char character = src[index];
                output[index] = character;
                if (character == '\0') return index != 0;
            }
            output[capacity - 1] = '\0';
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            output[0] = '\0';
            return false;
        }
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

    int GetWeaponDefinitionProtocolTeam(int definitionIndex) {
        switch (definitionIndex) {
        case 4:
        case 7:
        case 11:
        case 13:
        case 17:
        case 29:
        case 30:
        case 39:
            return 1;
        case 3:
        case 8:
        case 10:
        case 16:
        case 27:
        case 32:
        case 34:
        case 38:
        case 60:
        case 61:
            return 2;
        default:
            return 3;
        }
    }

    const char* GetWeaponKillEventName(int definitionIndex) {
        switch (definitionIndex) {
        case 1: return "deagle";
        case 2: return "elite";
        case 3: return "fiveseven";
        case 4: return "glock";
        case 7: return "ak47";
        case 8: return "aug";
        case 9: return "awp";
        case 10: return "famas";
        case 11: return "g3sg1";
        case 13: return "galilar";
        case 14: return "m249";
        case 16: return "m4a1";
        case 17: return "mac10";
        case 19: return "p90";
        case 23: return "mp5sd";
        case 24: return "ump45";
        case 25: return "xm1014";
        case 26: return "bizon";
        case 27: return "mag7";
        case 28: return "negev";
        case 29: return "sawedoff";
        case 30: return "tec9";
        case 31: return "taser";
        case 32: return "hkp2000";
        case 33: return "mp7";
        case 34: return "mp9";
        case 35: return "nova";
        case 36: return "p250";
        case 38: return "scar20";
        case 39: return "sg556";
        case 40: return "ssg08";
        case 60: return "m4a1_silencer";
        case 61: return "usp_silencer";
        case 63: return "cz75a";
        case 64: return "revolver";
        default: return nullptr;
        }
    }

    const InventorySnapshotSelection::WeaponSkinCollectionItem*
    FindWeaponSkinCollectionItem(
        const InventorySnapshotSelection& selection, uint64_t localId) {
        if (!localId) return nullptr;
        for (int index = 0; index < selection.weaponSkinItemCount; ++index) {
            if (selection.weaponSkinItems[index].localId == localId)
                return &selection.weaponSkinItems[index];
        }
        return nullptr;
    }

    bool UsesNativeTeamLoadoutObserver(
        const InventorySnapshotSelection& selection, uint64_t localId) {
        for (int index = 0; index < selection.weaponSkinItemCount; ++index) {
            if (selection.weaponSkinItems[index].localId == localId)
                return true;
        }
        for (int index = 0; index < selection.knifeItemCount; ++index) {
            if (selection.knifeItems[index].localId == localId)
                return true;
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
        if (stage == kWeaponSkinEarlyFrameStage) {
            RunWeaponSkinRuntimeControl(
                WeaponSkinRuntimePhase::EarlyNetUpdate);
        }
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
            RunWeaponSkinRuntimeControl(
                WeaponSkinRuntimePhase::RenderEnd);
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

    bool InstallWeaponMaterialInlineHook(void* target, void* replacement,
        SIZE_T patchLength, WeaponMaterialInlineHook& hook,
        void** original) {
        constexpr SIZE_T kPatchJumpSize = 12;
        constexpr SIZE_T kTrampolineJumpSize = 14;
        if (!target || !replacement || !original ||
            patchLength < kPatchJumpSize ||
            patchLength > sizeof(hook.original))
            return false;

        void* trampoline = VirtualAlloc(nullptr,
            patchLength + kTrampolineJumpSize,
            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!trampoline) return false;
        CopyMemory(hook.original, target, patchLength);
        CopyMemory(trampoline, hook.original, patchLength);

        // RIP-indirect absolute jump preserves RAX and every other register
        // established by the copied prologue.
        unsigned char jumpBack[kTrampolineJumpSize] = {
            0xFF, 0x25, 0, 0, 0, 0
        };
        const uintptr_t resume = reinterpret_cast<uintptr_t>(target) +
            patchLength;
        CopyMemory(jumpBack + 6, &resume, sizeof(resume));
        CopyMemory(static_cast<unsigned char*>(trampoline) + patchLength,
            jumpBack, sizeof(jumpBack));

        unsigned char patch[20]{};
        patch[0] = 0x48;
        patch[1] = 0xB8;
        const uintptr_t destination =
            reinterpret_cast<uintptr_t>(replacement);
        CopyMemory(patch + 2, &destination, sizeof(destination));
        patch[10] = 0xFF;
        patch[11] = 0xE0;
        for (SIZE_T index = kPatchJumpSize; index < patchLength; ++index)
            patch[index] = 0x90;

        DWORD oldProtection = 0;
        if (!VirtualProtect(target, patchLength, PAGE_EXECUTE_READWRITE,
                &oldProtection)) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        CopyMemory(target, patch, patchLength);
        FlushInstructionCache(GetCurrentProcess(), target, patchLength);
        DWORD ignored = 0;
        (void)VirtualProtect(target, patchLength, oldProtection, &ignored);

        hook.target = target;
        hook.trampoline = trampoline;
        hook.patchLength = patchLength;
        *original = trampoline;
        return true;
    }

    void RemoveWeaponMaterialInlineHook(WeaponMaterialInlineHook& hook) {
        if (!hook.target || !hook.patchLength) return;
        DWORD oldProtection = 0;
        if (VirtualProtect(hook.target, hook.patchLength,
                PAGE_EXECUTE_READWRITE, &oldProtection)) {
            CopyMemory(hook.target, hook.original, hook.patchLength);
            FlushInstructionCache(GetCurrentProcess(), hook.target,
                hook.patchLength);
            DWORD ignored = 0;
            (void)VirtualProtect(hook.target, hook.patchLength,
                oldProtection, &ignored);
        }
        Sleep(20);
        if (hook.trampoline)
            VirtualFree(hook.trampoline, 0, MEM_RELEASE);
        hook = {};
    }

    void LogWeaponMaterialHook(const char* name, uintptr_t weapon,
        bool argument, int beforePrimary, int beforeSecondary,
        int afterPrimary, int afterSecondary, uintptr_t caller) {
        if (weapon != g_appliedWeaponSkin.weapon ||
            g_weaponMaterialHookLogs.fetch_add(1) >= 8)
            return;
        const uintptr_t client = reinterpret_cast<uintptr_t>(
            GetModuleHandleW(L"client.dll"));
        char message[320]{};
        StringCchPrintfA(message, _countof(message),
            "Weapon material hook: fn=%s weapon=0x%llX arg=%s "
            "counts=%d/%d->%d/%d caller=0x%llX stage6=%ld.",
            name, static_cast<unsigned long long>(weapon),
            argument ? "true" : "false", beforePrimary, beforeSecondary,
            afterPrimary, afterSecondary,
            static_cast<unsigned long long>(
                client && caller >= client ? caller - client : caller),
            InterlockedCompareExchange(&g_frameStageSixCalls, 0, 0));
        AppendLog(message);
    }

    bool PrepareAppliedWeaponSkinForMaterial(uintptr_t weapon) {
        if (!kPrepareWeaponSkinInsideNativeAlias ||
            weapon != g_appliedWeaponSkin.weapon ||
            !g_appliedWeaponSkin.applied ||
            !g_appliedWeaponSkin.itemView || !g_addOrSetAttributeByName)
            return false;

        constexpr uintptr_t kStaticAttributesCountOffset = 0x210;
        constexpr uintptr_t kNetworkedAttributesOffset = 0x280;
        constexpr uintptr_t kNetworkedAttributesCountOffset = 0x288;
        constexpr uintptr_t kRestoreCustomMaterialOffset = 0x1B8;
        constexpr uintptr_t kDisallowSocOffset = 0x1E9;
        constexpr uint32_t kFallbackItemIdPart = 0xFFFFFFFFu;
        const int emptyCount = 0;
        const uint8_t initialized = 1;
        const bool restoreCustomMaterial = true;
        const bool allowSoc = false;
        const bool fieldsWritten =
            SafeWrite(g_appliedWeaponSkin.itemView +
                g_appliedWeaponSkin.itemIdHighOffset,
                kFallbackItemIdPart) &&
            SafeWrite(g_appliedWeaponSkin.itemView +
                g_appliedWeaponSkin.itemIdLowOffset,
                kFallbackItemIdPart) &&
            SafeWrite(g_appliedWeaponSkin.itemView +
                g_appliedWeaponSkin.initializedOffset, initialized) &&
            SafeWrite(g_appliedWeaponSkin.itemView +
                kRestoreCustomMaterialOffset, restoreCustomMaterial) &&
            SafeWrite(g_appliedWeaponSkin.itemView +
                kDisallowSocOffset, allowSoc) &&
            SafeWrite(weapon + g_appliedWeaponSkin.fallbackPaintKitOffset,
                g_appliedWeaponSkin.paintKit) &&
            SafeWrite(weapon + g_appliedWeaponSkin.fallbackSeedOffset,
                g_appliedWeaponSkin.seed) &&
            SafeWrite(weapon + g_appliedWeaponSkin.fallbackWearOffset,
                g_appliedWeaponSkin.wear) &&
            SafeWrite(weapon + g_appliedWeaponSkin.fallbackStatTrakOffset,
                g_appliedWeaponSkin.statTrak) &&
            SafeWrite(g_appliedWeaponSkin.itemView +
                kStaticAttributesCountOffset, emptyCount) &&
            SafeWrite(g_appliedWeaponSkin.itemView +
                kNetworkedAttributesCountOffset, emptyCount);
        if (!fieldsWritten) return false;

        __try {
            void* attributes = reinterpret_cast<void*>(
                g_appliedWeaponSkin.itemView +
                kNetworkedAttributesOffset);
            g_addOrSetAttributeByName(attributes,
                "set item texture prefab",
                static_cast<float>(g_appliedWeaponSkin.paintKit));
            g_addOrSetAttributeByName(attributes,
                "set item texture seed",
                static_cast<float>(g_appliedWeaponSkin.seed));
            g_addOrSetAttributeByName(attributes,
                "set item texture wear", g_appliedWeaponSkin.wear);
            if (g_appliedWeaponSkin.statTrak >= 0) {
                g_addOrSetAttributeByName(attributes,
                    "kill eater",
                    EncodeIntegerAttributeValue(static_cast<uint32_t>(
                        g_appliedWeaponSkin.statTrak)));
                g_addOrSetAttributeByName(attributes,
                    "kill eater score type",
                    EncodeIntegerAttributeValue(0));
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon skin preparation: attribute setter faulted.");
            return false;
        }
    }

    void __fastcall PrepareWeaponSkinDiagnosticHook(void* weaponObject,
        bool rebuild) {
        const uintptr_t weapon = reinterpret_cast<uintptr_t>(weaponObject);
        int beforeStatic = -1;
        int beforeNetworked = -1;
        int nativeStatic = -1;
        int nativeNetworked = -1;
        int preparedStatic = -1;
        int preparedNetworked = -1;
        const uintptr_t itemView = weapon == g_appliedWeaponSkin.weapon
            ? g_appliedWeaponSkin.itemView : 0;
        if (itemView) {
            (void)SafeRead(itemView + 0x210, beforeStatic);
            (void)SafeRead(itemView + 0x288, beforeNetworked);
        }
        if (g_observedPrepareWeaponSkin)
            g_observedPrepareWeaponSkin(weaponObject, rebuild);
        if (itemView) {
            (void)SafeRead(itemView + 0x210, nativeStatic);
            (void)SafeRead(itemView + 0x288, nativeNetworked);
        }
        const bool prepared = PrepareAppliedWeaponSkinForMaterial(weapon);
        if (itemView) {
            (void)SafeRead(itemView + 0x210, preparedStatic);
            (void)SafeRead(itemView + 0x288, preparedNetworked);
        }
        if (itemView && !g_weaponSkinMidpointLogged.exchange(true)) {
            char message[320]{};
            StringCchPrintfA(message, _countof(message),
                "Weapon skin midpoint: weapon=0x%llX rebuild=%s "
                "attrs=%d/%d->%d/%d->%d/%d prepared=%s.",
                static_cast<unsigned long long>(weapon),
                rebuild ? "true" : "false",
                beforeStatic, beforeNetworked,
                nativeStatic, nativeNetworked,
                preparedStatic, preparedNetworked,
                prepared ? "yes" : "no");
            AppendLog(message);
        }
    }

    void __fastcall CompositeMaterialDiagnosticHook(void* owner,
        bool rebuild) {
        const uintptr_t weapon = reinterpret_cast<uintptr_t>(owner) -
            g_compositeMaterialOwnerOffset;
        int beforePrimary = -1;
        int beforeSecondary = -1;
        int afterPrimary = -1;
        int afterSecondary = -1;
        (void)SafeRead(weapon + 0xAA8, beforePrimary);
        (void)SafeRead(weapon + 0xAC0, beforeSecondary);
        if (g_observedCompositeMaterial)
            g_observedCompositeMaterial(owner, rebuild);
        (void)SafeRead(weapon + 0xAA8, afterPrimary);
        (void)SafeRead(weapon + 0xAC0, afterSecondary);
        LogWeaponMaterialHook("owner", weapon, rebuild,
            beforePrimary, beforeSecondary, afterPrimary, afterSecondary,
            reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }

    void __fastcall CompositeMaterialSetDiagnosticHook(void* weaponObject,
        bool rebuild) {
        const uintptr_t weapon = reinterpret_cast<uintptr_t>(weaponObject);
        int beforePrimary = -1;
        int beforeSecondary = -1;
        int afterPrimary = -1;
        int afterSecondary = -1;
        (void)SafeRead(weapon + 0xAA8, beforePrimary);
        (void)SafeRead(weapon + 0xAC0, beforeSecondary);
        if (g_observedCompositeMaterialSet)
            g_observedCompositeMaterialSet(weaponObject, rebuild);
        (void)SafeRead(weapon + 0xAA8, afterPrimary);
        (void)SafeRead(weapon + 0xAC0, afterSecondary);
        LogWeaponMaterialHook("set", weapon, rebuild,
            beforePrimary, beforeSecondary, afterPrimary, afterSecondary,
            reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }

    bool RegisterWeaponTextureSeedOverride(
        uintptr_t itemView, uintptr_t itemIdOffset, uint64_t generatedItemId,
        int paintKit, int seed, bool validateIdentity) {
        if (!itemView || paintKit <= 0) return false;

        AcquireSRWLockExclusive(&g_weaponTextureSeedOverrideLock);
        WeaponTextureSeedOverrideEntry* destination = nullptr;
        for (auto& entry : g_weaponTextureSeedOverrides) {
            if (entry.itemView == itemView) {
                destination = &entry;
                break;
            }
            if (!destination && !entry.itemView)
                destination = &entry;
        }
        if (!destination) {
            for (auto& entry : g_weaponTextureSeedOverrides) {
                uint64_t currentItemId = 0;
                if (entry.validateIdentity && entry.itemIdOffset &&
                    (!SafeRead(entry.itemView + entry.itemIdOffset,
                        currentItemId) ||
                    currentItemId != entry.generatedItemId)) {
                    destination = &entry;
                    break;
                }
            }
        }
        if (destination) {
            *destination = { itemView, itemIdOffset, generatedItemId,
                seed > 0 ? seed : paintKit, validateIdentity };
        }
        ReleaseSRWLockExclusive(&g_weaponTextureSeedOverrideLock);
        if (!destination)
            AppendLog("Weapon texture seed registry: capacity exhausted.");
        return destination != nullptr;
    }

    void UnregisterWeaponTextureSeedOverride(uintptr_t itemView) {
        if (!itemView) return;
        AcquireSRWLockExclusive(&g_weaponTextureSeedOverrideLock);
        for (auto& entry : g_weaponTextureSeedOverrides) {
            if (entry.itemView == itemView) {
                entry = {};
                break;
            }
        }
        ReleaseSRWLockExclusive(&g_weaponTextureSeedOverrideLock);
    }

    void ClearWeaponTextureSeedOverrides() {
        AcquireSRWLockExclusive(&g_weaponTextureSeedOverrideLock);
        for (auto& entry : g_weaponTextureSeedOverrides) entry = {};
        ReleaseSRWLockExclusive(&g_weaponTextureSeedOverrideLock);
    }

    int __fastcall ItemViewGetTextureSeedOverrideHook(void* itemViewObject) {
        const uintptr_t itemView =
            reinterpret_cast<uintptr_t>(itemViewObject);
        AcquireSRWLockShared(&g_weaponTextureSeedOverrideLock);
        for (const auto& entry : g_weaponTextureSeedOverrides) {
            if (entry.itemView != itemView) continue;
            bool identityMatches = true;
            if (entry.validateIdentity) {
                uint64_t currentItemId = 0;
                identityMatches = entry.itemIdOffset &&
                    entry.generatedItemId &&
                    SafeRead(itemView + entry.itemIdOffset, currentItemId) &&
                    currentItemId == entry.generatedItemId;
            }
            if (identityMatches) {
                const int effectiveSeed = entry.effectiveSeed;
                ReleaseSRWLockShared(&g_weaponTextureSeedOverrideLock);
                return effectiveSeed;
            }
            break;
        }
        ReleaseSRWLockShared(&g_weaponTextureSeedOverrideLock);
        return g_originalItemViewGetTextureSeed
            ? g_originalItemViewGetTextureSeed(itemViewObject) : 0;
    }

    bool InstallItemViewGetTextureSeedOverride(void* itemViewObject) {
        if (!itemViewObject) return false;
        if (g_itemViewGetTextureSeedSlot &&
            g_originalItemViewGetTextureSeed)
            return true;
        void** vtable = nullptr;
        if (!SafeRead(reinterpret_cast<uintptr_t>(itemViewObject), vtable) ||
            !vtable)
            return false;
        void** slot = &vtable[4];
        void* original = nullptr;
        if (!SetVtableEntry(slot,
                reinterpret_cast<void*>(
                    &ItemViewGetTextureSeedOverrideHook),
                &original))
            return false;
        g_itemViewGetTextureSeedSlot = slot;
        g_originalItemViewGetTextureSeed =
            reinterpret_cast<ItemViewGetTextureSeedFn>(original);
        const uintptr_t client = reinterpret_cast<uintptr_t>(
            GetModuleHandleW(L"client.dll"));
        char message[192]{};
        StringCchPrintfA(message, _countof(message),
            "Weapon texture seed getter: override installed slot=4 "
            "original_rva=0x%llX requested=%d effective=%d.",
            static_cast<unsigned long long>(client &&
                reinterpret_cast<uintptr_t>(original) >= client
                ? reinterpret_cast<uintptr_t>(original) - client
                : reinterpret_cast<uintptr_t>(original)),
            g_appliedWeaponSkin.seed,
            g_appliedWeaponSkin.seed > 0
                ? g_appliedWeaponSkin.seed
                : g_appliedWeaponSkin.paintKit);
        AppendLog(message);
        return true;
    }

    void RemoveItemViewGetTextureSeedOverride() {
        if (g_itemViewGetTextureSeedSlot &&
            g_originalItemViewGetTextureSeed) {
            (void)SetVtableEntry(g_itemViewGetTextureSeedSlot,
                reinterpret_cast<void*>(g_originalItemViewGetTextureSeed),
                nullptr);
        }
        g_itemViewGetTextureSeedSlot = nullptr;
        g_originalItemViewGetTextureSeed = nullptr;
        ClearWeaponTextureSeedOverrides();
    }

    bool __fastcall BuildWeaponMaterialOverridesDiagnosticHook(
        void* itemViewObject, void* weaponObject, void* outputObject,
        bool includeWearOffsets) {
        const uintptr_t itemView = reinterpret_cast<uintptr_t>(itemViewObject);
        const uintptr_t weapon = reinterpret_cast<uintptr_t>(weaponObject);
        int beforeCount = -1;
        int afterCount = -1;
        int textureSeed = -1;
        float wear = -1.0f;
        int materialIds[6]{};
        int materialVariants[6]{};
        uintptr_t materialGetter = 0;
        (void)SafeRead(reinterpret_cast<uintptr_t>(outputObject), beforeCount);

        if (weapon == g_appliedWeaponSkin.weapon &&
            itemView == g_appliedWeaponSkin.itemView) {
            (void)InstallItemViewGetTextureSeedOverride(itemViewObject);
            __try {
                void** vtable = *reinterpret_cast<void***>(itemViewObject);
                using GetTextureSeedFn = int(__fastcall*)(void*);
                using GetWearFn = float(__fastcall*)(void*, float);
                textureSeed = reinterpret_cast<GetTextureSeedFn>(vtable[4])(
                    itemViewObject);
                wear = reinterpret_cast<GetWearFn>(vtable[5])(
                    itemViewObject, 0.0f);
                using GetMaterialIdFn = int(__fastcall*)(
                    void*, int, int, int);
                materialGetter = reinterpret_cast<uintptr_t>(vtable[7]);
                const auto getMaterialId =
                    reinterpret_cast<GetMaterialIdFn>(vtable[7]);
                for (int index = 0; index < 6; ++index) {
                    materialIds[index] = getMaterialId(
                        itemViewObject, index, 0, 0);
                    materialVariants[index] = getMaterialId(
                        itemViewObject, index, 6, -1);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                textureSeed = -2;
                wear = -2.0f;
            }
        }

        const bool result = g_observedBuildWeaponMaterialOverrides
            ? g_observedBuildWeaponMaterialOverrides(itemViewObject,
                weaponObject, outputObject, includeWearOffsets)
            : false;
        (void)SafeRead(reinterpret_cast<uintptr_t>(outputObject), afterCount);

        if (weapon == g_appliedWeaponSkin.weapon &&
            itemView == g_appliedWeaponSkin.itemView &&
            !g_weaponMaterialOverridesLogged.exchange(true)) {
            uintptr_t entries = 0;
            uintptr_t firstName = 0;
            uintptr_t secondName = 0;
            char first[96]{};
            char second[96]{};
            (void)SafeRead(reinterpret_cast<uintptr_t>(outputObject) + 8,
                entries);
            if (entries && afterCount > 0) {
                (void)SafeRead(entries, firstName);
                (void)SafeReadString(firstName, first, _countof(first));
            }
            if (entries && afterCount > 1) {
                (void)SafeRead(entries + 0x28, secondName);
                (void)SafeReadString(secondName, second, _countof(second));
            }
            const uintptr_t client = reinterpret_cast<uintptr_t>(
                GetModuleHandleW(L"client.dll"));
            char message[768]{};
            StringCchPrintfA(message, _countof(message),
                "Weapon material builder: weapon=0x%llX view=0x%llX "
                "texture_seed=%d wear=%.6f offsets=%s result=%s "
                "count=%d->%d "
                "getter_rva=0x%llX ids=%d,%d,%d,%d,%d,%d "
                "variants=%d,%d,%d,%d,%d,%d entries=0x%llX "
                "first='%s' second='%s'.",
                static_cast<unsigned long long>(weapon),
                static_cast<unsigned long long>(itemView), textureSeed, wear,
                includeWearOffsets ? "yes" : "no",
                result ? "yes" : "no", beforeCount, afterCount,
                static_cast<unsigned long long>(client &&
                    materialGetter >= client
                    ? materialGetter - client : materialGetter),
                materialIds[0], materialIds[1], materialIds[2],
                materialIds[3], materialIds[4], materialIds[5],
                materialVariants[0], materialVariants[1],
                materialVariants[2], materialVariants[3],
                materialVariants[4], materialVariants[5],
                static_cast<unsigned long long>(entries), first, second);
            AppendLog(message);
        }
        return result;
    }

    void __fastcall UpdateWeaponSkinDiagnosticHook(void* weaponObject,
        bool rebuild) {
        const uintptr_t weapon = reinterpret_cast<uintptr_t>(weaponObject);
        int beforePrimary = -1;
        int beforeSecondary = -1;
        int afterPrimary = -1;
        int afterSecondary = -1;
        (void)SafeRead(weapon + 0xAA8, beforePrimary);
        (void)SafeRead(weapon + 0xAC0, beforeSecondary);
        if (g_observedUpdateWeaponSkin)
            g_observedUpdateWeaponSkin(weaponObject, rebuild);
        (void)SafeRead(weapon + 0xAA8, afterPrimary);
        (void)SafeRead(weapon + 0xAC0, afterSecondary);
        LogWeaponMaterialHook("skin", weapon, rebuild,
            beforePrimary, beforeSecondary, afterPrimary, afterSecondary,
            reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }

    void __fastcall UpdateWeaponSkinAliasDiagnosticHook(void* weaponObject,
        bool rebuild) {
        const uintptr_t weapon = reinterpret_cast<uintptr_t>(weaponObject);
        int beforePrimary = -1;
        int beforeSecondary = -1;
        int afterPrimary = -1;
        int afterSecondary = -1;
        (void)SafeRead(weapon + 0xAA8, beforePrimary);
        (void)SafeRead(weapon + 0xAC0, beforeSecondary);
        if (weapon == g_appliedWeaponSkin.weapon &&
            g_appliedWeaponSkin.itemView) {
            (void)InstallItemViewGetTextureSeedOverride(
                reinterpret_cast<void*>(g_appliedWeaponSkin.itemView));
        }
        (void)PrepareAppliedWeaponSkinForMaterial(weapon);
        if (g_observedUpdateWeaponSkinAlias)
            g_observedUpdateWeaponSkinAlias(weaponObject, rebuild);
        (void)SafeRead(weapon + 0xAA8, afterPrimary);
        (void)SafeRead(weapon + 0xAC0, afterSecondary);
        LogWeaponMaterialHook("skin-alias", weapon, rebuild,
            beforePrimary, beforeSecondary, afterPrimary, afterSecondary,
            reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }

    bool InstallWeaponMaterialDiagnostics() {
        if (!g_updateCompositeMaterial || !g_updateCompositeMaterialSet ||
            !g_updateWeaponSkin)
            return false;
        const uintptr_t updateSkinAlias = reinterpret_cast<uintptr_t>(
            reinterpret_cast<unsigned char*>(g_updateWeaponSkin) + 0x50);
        unsigned char prepareCallOpcode = 0;
        int32_t prepareCallDisplacement = 0;
        uintptr_t prepareWeaponSkin = 0;
        if (SafeRead(updateSkinAlias + 0x0F, prepareCallOpcode) &&
            prepareCallOpcode == 0xE8 &&
            SafeRead(updateSkinAlias + 0x10, prepareCallDisplacement)) {
            prepareWeaponSkin = updateSkinAlias + 0x14 +
                prepareCallDisplacement;
        }
        if (!IsExecutableAddress(
                reinterpret_cast<void*>(prepareWeaponSkin))) {
            AppendLog("Weapon material hooks: native midpoint unresolved.");
            return false;
        }
        const uintptr_t materialBuilderCall =
            reinterpret_cast<uintptr_t>(g_updateCompositeMaterialSet) +
            0x711;
        unsigned char materialBuilderOpcode = 0;
        int32_t materialBuilderDisplacement = 0;
        uintptr_t materialBuilder = 0;
        if (SafeRead(materialBuilderCall, materialBuilderOpcode) &&
            materialBuilderOpcode == 0xE8 &&
            SafeRead(materialBuilderCall + 1,
                materialBuilderDisplacement)) {
            materialBuilder = materialBuilderCall + 5 +
                materialBuilderDisplacement;
        }
        if (!IsExecutableAddress(reinterpret_cast<void*>(materialBuilder))) {
            AppendLog("Weapon material hooks: builder unresolved.");
            return false;
        }
        g_weaponMaterialHookLogs.store(0);
        g_weaponSkinMidpointLogged.store(false);
        g_weaponMaterialOverridesLogged.store(false);
        if (!InstallWeaponMaterialInlineHook(
                reinterpret_cast<void*>(prepareWeaponSkin),
                reinterpret_cast<void*>(&PrepareWeaponSkinDiagnosticHook),
                15, g_prepareWeaponSkinHook,
                reinterpret_cast<void**>(&g_observedPrepareWeaponSkin)) ||
            !InstallWeaponMaterialInlineHook(
                reinterpret_cast<void*>(g_updateCompositeMaterial),
                reinterpret_cast<void*>(&CompositeMaterialDiagnosticHook),
                15, g_compositeMaterialHook,
                reinterpret_cast<void**>(&g_observedCompositeMaterial)) ||
            !InstallWeaponMaterialInlineHook(
                reinterpret_cast<void*>(g_updateCompositeMaterialSet),
                reinterpret_cast<void*>(&CompositeMaterialSetDiagnosticHook),
                13, g_compositeMaterialSetHook,
                reinterpret_cast<void**>(&g_observedCompositeMaterialSet)) ||
            !InstallWeaponMaterialInlineHook(
                reinterpret_cast<void*>(materialBuilder),
                reinterpret_cast<void*>(
                    &BuildWeaponMaterialOverridesDiagnosticHook),
                15, g_buildWeaponMaterialOverridesHook,
                reinterpret_cast<void**>(
                    &g_observedBuildWeaponMaterialOverrides)) ||
            !InstallWeaponMaterialInlineHook(
                reinterpret_cast<void*>(g_updateWeaponSkin),
                reinterpret_cast<void*>(&UpdateWeaponSkinDiagnosticHook),
                15, g_updateWeaponSkinHook,
                reinterpret_cast<void**>(&g_observedUpdateWeaponSkin)) ||
            !InstallWeaponMaterialInlineHook(
                reinterpret_cast<unsigned char*>(g_updateWeaponSkin) + 0x50,
                reinterpret_cast<void*>(
                    &UpdateWeaponSkinAliasDiagnosticHook),
                15, g_updateWeaponSkinAliasHook,
                reinterpret_cast<void**>(
                    &g_observedUpdateWeaponSkinAlias))) {
            RemoveWeaponMaterialInlineHook(g_updateWeaponSkinAliasHook);
            RemoveWeaponMaterialInlineHook(g_updateWeaponSkinHook);
            RemoveWeaponMaterialInlineHook(
                g_buildWeaponMaterialOverridesHook);
            RemoveWeaponMaterialInlineHook(g_compositeMaterialSetHook);
            RemoveWeaponMaterialInlineHook(g_compositeMaterialHook);
            RemoveWeaponMaterialInlineHook(g_prepareWeaponSkinHook);
            g_observedUpdateWeaponSkin = nullptr;
            g_observedUpdateWeaponSkinAlias = nullptr;
            g_observedCompositeMaterialSet = nullptr;
            g_observedCompositeMaterial = nullptr;
            g_observedPrepareWeaponSkin = nullptr;
            g_observedBuildWeaponMaterialOverrides = nullptr;
            return false;
        }
        const uintptr_t client = reinterpret_cast<uintptr_t>(
            GetModuleHandleW(L"client.dll"));
        char message[192]{};
        StringCchPrintfA(message, _countof(message),
            "Weapon material hooks: diagnostics installed midpoint_rva=0x%llX "
            "builder_rva=0x%llX.",
            static_cast<unsigned long long>(
                client ? prepareWeaponSkin - client : prepareWeaponSkin),
            static_cast<unsigned long long>(
                client ? materialBuilder - client : materialBuilder));
        AppendLog(message);
        return true;
    }

    void RemoveWeaponMaterialDiagnostics() {
        RemoveItemViewGetTextureSeedOverride();
        RemoveWeaponMaterialInlineHook(g_updateWeaponSkinAliasHook);
        RemoveWeaponMaterialInlineHook(g_updateWeaponSkinHook);
        RemoveWeaponMaterialInlineHook(g_buildWeaponMaterialOverridesHook);
        RemoveWeaponMaterialInlineHook(g_compositeMaterialSetHook);
        RemoveWeaponMaterialInlineHook(g_compositeMaterialHook);
        RemoveWeaponMaterialInlineHook(g_prepareWeaponSkinHook);
        g_observedUpdateWeaponSkin = nullptr;
        g_observedUpdateWeaponSkinAlias = nullptr;
        g_observedCompositeMaterialSet = nullptr;
        g_observedCompositeMaterial = nullptr;
        g_observedPrepareWeaponSkin = nullptr;
        g_observedBuildWeaponMaterialOverrides = nullptr;
        AppendLog("Weapon material hooks: diagnostics removed.");
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
        uintptr_t second = 0;
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
                if (!result.first) {
                    result.first = reinterpret_cast<uintptr_t>(
                        sectionStart + offset);
                } else if (!result.second) {
                    result.second = reinterpret_cast<uintptr_t>(
                        sectionStart + offset);
                }
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
        constexpr unsigned char updateWeaponGraphController[] = {
            0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x57, 0x48,
            0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xA0,
            0x00, 0x00, 0x00, 0x48, 0x8D, 0x05, 0x00, 0x00,
            0x00, 0x00, 0x48, 0xC7, 0x45, 0xCF, 0xB5, 0x00,
            0x00, 0x00
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
        // This callsite is used by current client builds to set an ItemView
        // attribute by its schema name.
        constexpr unsigned char setItemViewAttributeCall[] = {
            0xE8, 0x00, 0x00, 0x00, 0x00, 0x66, 0x41, 0x0F,
            0x6E, 0xD4
        };
        constexpr unsigned char updateCompositeMaterialCall[] = {
            0x48, 0x81, 0xC1, 0x08, 0x06, 0x00, 0x00, 0xB2,
            0x01, 0xE8, 0x00, 0x00, 0x00, 0x00
        };
        constexpr unsigned char updateCompositeMaterialSet[] = {
            0x40, 0x55, 0x53, 0x41, 0x57, 0x48, 0x8D, 0xAC,
            0x24, 0x00, 0xFE, 0x00, 0x00
        };
        constexpr unsigned char updateWeaponSkin[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
            0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0xE8,
            0x00, 0x00, 0x00, 0x00, 0xF6, 0xC3, 0x01, 0x74,
            0x0A, 0x33, 0xD2, 0x48, 0x8B, 0xCF, 0xE8, 0x00,
            0x00, 0x00, 0x00, 0x48, 0x8D, 0x8F, 0x90, 0x19,
            0x00, 0x00
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
        const PatternResult updateWeaponGraphControllerResult =
            ScanExecutableSections(client, updateWeaponGraphController,
                "xxxxxxxxxxxxxxxxxxxxxx????xxxxxxxx");
        LogPatternResult("UpdateWeaponGraphController", client,
            updateWeaponGraphControllerResult);
        g_updateWeaponGraphController =
            updateWeaponGraphControllerResult.count == 1
            ? reinterpret_cast<UpdateWeaponGraphControllerFn>(
                updateWeaponGraphControllerResult.first) : nullptr;
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
        const PatternResult setItemViewAttributeCallResult =
            ScanExecutableSections(client, setItemViewAttributeCall,
                "x????xxxxx");
        LogPatternResult("SetItemViewAttributeByName callsite", client,
            setItemViewAttributeCallResult);
        g_setItemViewAttributeByName = nullptr;
        if (setItemViewAttributeCallResult.count == 1) {
            int32_t displacement = 0;
            if (SafeRead(setItemViewAttributeCallResult.first + 1,
                    displacement)) {
                const uintptr_t target =
                    setItemViewAttributeCallResult.first + 5 + displacement;
                if (IsExecutableAddress(reinterpret_cast<void*>(target)))
                    g_setItemViewAttributeByName =
                        reinterpret_cast<SetItemViewAttributeByNameFn>(target);
            }
        }
        char attributeMessage[192]{};
        StringCchPrintfA(attributeMessage, _countof(attributeMessage),
            "SetItemViewAttributeByName: resolved=%s target_rva=0x%llX.",
            g_setItemViewAttributeByName ? "yes" : "no",
            static_cast<unsigned long long>(g_setItemViewAttributeByName
                ? reinterpret_cast<uintptr_t>(g_setItemViewAttributeByName) -
                    reinterpret_cast<uintptr_t>(client)
                : 0));
        AppendLog(attributeMessage);
        // The ItemView wrapper adds 0x208 (m_AttributeList) and calls the
        // generic CAttributeList setter. Resolve that inner call, then use
        // it with m_NetworkedDynamicAttributes at 0x280.
        g_addOrSetAttributeByName = nullptr;
        if (g_setItemViewAttributeByName) {
            const uintptr_t wrapper = reinterpret_cast<uintptr_t>(
                g_setItemViewAttributeByName);
            uint8_t rex = 0;
            uint8_t addOpcode = 0;
            uint8_t addRegister = 0;
            uint32_t wrapperListOffset = 0;
            uint8_t callOpcode = 0;
            int32_t callDisplacement = 0;
            if (SafeRead(wrapper + 0x09, rex) && rex == 0x48 &&
                SafeRead(wrapper + 0x0A, addOpcode) &&
                addOpcode == 0x81 &&
                SafeRead(wrapper + 0x0B, addRegister) &&
                addRegister == 0xC1 &&
                SafeRead(wrapper + 0x0C, wrapperListOffset) &&
                wrapperListOffset == 0x208 &&
                SafeRead(wrapper + 0x10, callOpcode) &&
                callOpcode == 0xE8 &&
                SafeRead(wrapper + 0x11, callDisplacement)) {
                const uintptr_t innerSetter = wrapper + 0x15 +
                    callDisplacement;
                if (IsExecutableAddress(
                        reinterpret_cast<void*>(innerSetter))) {
                    g_addOrSetAttributeByName =
                        reinterpret_cast<AddOrSetAttributeByNameFn>(
                            innerSetter);
                }
            }
        }
        char networkedAttributeMessage[224]{};
        StringCchPrintfA(networkedAttributeMessage,
            _countof(networkedAttributeMessage),
            "AddOrSetNetworkedAttributeByName: resolved=%s "
            "target_rva=0x%llX list_offset=0x280.",
            g_addOrSetAttributeByName ? "yes" : "no",
            static_cast<unsigned long long>(g_addOrSetAttributeByName
                ? reinterpret_cast<uintptr_t>(
                    g_addOrSetAttributeByName) -
                    reinterpret_cast<uintptr_t>(client)
                : 0));
        AppendLog(networkedAttributeMessage);
        const PatternResult updateCompositeMaterialResult =
            ScanExecutableSections(client, updateCompositeMaterialCall,
                "xxxxxxxxxx????");
        LogPatternResult("UpdateCompositeMaterial callsite", client,
            updateCompositeMaterialResult);
        g_updateCompositeMaterial = nullptr;
        g_compositeMaterialOwnerOffset = 0;
        if (updateCompositeMaterialResult.count == 1) {
            int32_t callDisplacement = 0;
            if (SafeRead(updateCompositeMaterialResult.first + 10,
                    callDisplacement)) {
                const uintptr_t target =
                    updateCompositeMaterialResult.first + 14 +
                    callDisplacement;
                if (IsExecutableAddress(reinterpret_cast<void*>(target))) {
                    g_updateCompositeMaterial =
                        reinterpret_cast<UpdateCompositeMaterialFn>(target);
                    g_compositeMaterialOwnerOffset = 0x608;
                }
            }
        }
        char compositeMessage[224]{};
        StringCchPrintfA(compositeMessage, _countof(compositeMessage),
            "UpdateCompositeMaterial: resolved=%s target_rva=0x%llX "
            "owner_offset=0x%llX.",
            g_updateCompositeMaterial ? "yes" : "no",
            static_cast<unsigned long long>(g_updateCompositeMaterial
                ? reinterpret_cast<uintptr_t>(g_updateCompositeMaterial) -
                    reinterpret_cast<uintptr_t>(client) : 0),
            static_cast<unsigned long long>(
                g_compositeMaterialOwnerOffset));
        AppendLog(compositeMessage);
        const PatternResult updateCompositeMaterialSetResult =
            ScanExecutableSections(client, updateCompositeMaterialSet,
                "xxxxxxxxxxx??");
        LogPatternResult("UpdateCompositeMaterialSet", client,
            updateCompositeMaterialSetResult);
        g_updateCompositeMaterialSet =
            updateCompositeMaterialSetResult.count == 1
            ? reinterpret_cast<UpdateCompositeMaterialSetFn>(
                updateCompositeMaterialSetResult.first) : nullptr;
        const PatternResult updateWeaponSkinResult =
            ScanExecutableSections(client, updateWeaponSkin,
                "xxxxxxxxxxxxxxxx????xxxxxxxxxxx????xxxxxxx");
        LogPatternResult("UpdateWeaponSkin aliases", client,
            updateWeaponSkinResult);
        g_updateWeaponSkin = updateWeaponSkinResult.count == 2 &&
            updateWeaponSkinResult.second ==
                updateWeaponSkinResult.first + 0x50
            ? reinterpret_cast<UpdateWeaponSkinFn>(
                updateWeaponSkinResult.first) : nullptr;
        char updateSkinMessage[192]{};
        StringCchPrintfA(updateSkinMessage, _countof(updateSkinMessage),
            "UpdateWeaponSkin: resolved=%s target_rva=0x%llX.",
            g_updateWeaponSkin ? "yes" : "no",
            static_cast<unsigned long long>(g_updateWeaponSkin
                ? reinterpret_cast<uintptr_t>(g_updateWeaponSkin) -
                    reinterpret_cast<uintptr_t>(client) : 0));
        AppendLog(updateSkinMessage);
        if (kInstallWeaponMaterialDiagnostics) {
            AppendLog(InstallWeaponMaterialDiagnostics()
                ? "Weapon material hooks: ready."
                : "Weapon material hooks: installation failed.");
        } else {
            AppendLog("Weapon material hooks: disabled for native equip trial.");
        }
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
        const InventorySnapshotSelection& selection,
        int32_t quality = kUnusualItemQuality) {
        const uint16_t definition = static_cast<uint16_t>(definitionIndex);
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

    uintptr_t ResolveHudWeaponSceneNode(
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
        if (!SafeRead(pawn + selection.hudModelArmsOffset,
                hudArmsHandle) || !hudArmsHandle ||
            hudArmsHandle == 0xFFFFFFFF)
            return 0;
        const uintptr_t hudArms = ReadEntityByIndex(entityList,
            static_cast<int>(hudArmsHandle & 0x7FFF), stride);
        uintptr_t hudSceneNode = 0;
        uintptr_t child = 0;
        if (!hudArms || !SafeRead(hudArms +
                selection.gameSceneNodeOffset, hudSceneNode) ||
            !hudSceneNode || !SafeRead(hudSceneNode +
                selection.sceneNodeChildOffset, child))
            return 0;

        for (int visited = 0; child && visited < 32; ++visited) {
            uintptr_t childOwner = 0;
            uint32_t ownerHandle = 0;
            if (SafeRead(child + selection.sceneNodeOwnerOffset,
                    childOwner) && childOwner &&
                SafeRead(childOwner + selection.ownerEntityOffset,
                    ownerHandle) && ownerHandle &&
                ownerHandle != 0xFFFFFFFF &&
                ReadEntityByIndex(entityList,
                    static_cast<int>(ownerHandle & 0x7FFF), stride) ==
                    weapon)
                return child;

            uintptr_t next = 0;
            if (!SafeRead(child + selection.sceneNodeNextSiblingOffset,
                    next) || next == child)
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

    bool SafeSetItemViewSkinAttributes(
        uintptr_t itemView, int paintKit, int seed, float wear,
        int statTrak = -1, int statTrakType = 0) {
        if (!itemView || !g_setItemViewAttributeByName) return false;
        __try {
            g_setItemViewAttributeByName(
                reinterpret_cast<void*>(itemView),
                "set item texture prefab", static_cast<float>(paintKit));
            g_setItemViewAttributeByName(
                reinterpret_cast<void*>(itemView),
                "set item texture seed", static_cast<float>(seed));
            g_setItemViewAttributeByName(
                reinterpret_cast<void*>(itemView),
                "set item texture wear", wear);
            if (statTrak >= 0) {
                g_setItemViewAttributeByName(
                    reinterpret_cast<void*>(itemView),
                    "kill eater", EncodeIntegerAttributeValue(
                        static_cast<uint32_t>(statTrak)));
                g_setItemViewAttributeByName(
                    reinterpret_cast<void*>(itemView),
                    "kill eater score type", EncodeIntegerAttributeValue(
                        static_cast<uint32_t>(statTrakType)));
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon skin attributes: ItemView setter faulted.");
            return false;
        }
    }

    bool SafeSetNetworkedItemViewStatTrakAttributes(
        uintptr_t itemView, int statTrak, int statTrakType = 0) {
        constexpr uintptr_t kNetworkedDynamicAttributesOffset = 0x280;
        if (!itemView || statTrak < 0)
            return false;
        if (g_setItemViewAttributeByName) {
            __try {
                g_setItemViewAttributeByName(
                    reinterpret_cast<void*>(itemView),
                    "kill eater", EncodeIntegerAttributeValue(
                        static_cast<uint32_t>(statTrak)));
                g_setItemViewAttributeByName(
                    reinterpret_cast<void*>(itemView),
                    "kill eater score type", EncodeIntegerAttributeValue(
                        static_cast<uint32_t>(statTrakType)));
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (!g_addOrSetAttributeByName) return true;
        __try {
            void* attributes = reinterpret_cast<void*>(
                itemView + kNetworkedDynamicAttributesOffset);
            g_addOrSetAttributeByName(attributes,
                "kill eater", EncodeIntegerAttributeValue(
                    static_cast<uint32_t>(statTrak)));
            g_addOrSetAttributeByName(attributes,
                "kill eater score type", EncodeIntegerAttributeValue(
                    static_cast<uint32_t>(statTrakType)));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon StatTrak: networked attribute update faulted.");
            return false;
        }
    }

    bool SafeSetNetworkedItemViewSkinAttributes(
        uintptr_t itemView, int paintKit, int seed, float wear,
        int statTrak = -1, int statTrakType = 0) {
        constexpr uintptr_t kNetworkedDynamicAttributesOffset = 0x280;
        constexpr uintptr_t kAttributeCountOffset = 0x08;
        if (!itemView) return false;
        if (g_setItemViewAttributeByName) {
            __try {
                g_setItemViewAttributeByName(
                    reinterpret_cast<void*>(itemView),
                    "set item texture prefab", static_cast<float>(paintKit));
                g_setItemViewAttributeByName(
                    reinterpret_cast<void*>(itemView),
                    "set item texture seed", static_cast<float>(seed));
                g_setItemViewAttributeByName(
                    reinterpret_cast<void*>(itemView),
                    "set item texture wear", wear);
                if (statTrak >= 0) {
                    g_setItemViewAttributeByName(
                        reinterpret_cast<void*>(itemView),
                        "kill eater", EncodeIntegerAttributeValue(
                            static_cast<uint32_t>(statTrak)));
                    g_setItemViewAttributeByName(
                        reinterpret_cast<void*>(itemView),
                        "kill eater score type",
                        EncodeIntegerAttributeValue(
                            static_cast<uint32_t>(statTrakType)));
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (!g_addOrSetAttributeByName) return true;
        __try {
            const int emptyCount = 0;
            if (!SafeWrite(itemView + kNetworkedDynamicAttributesOffset +
                    kAttributeCountOffset, emptyCount))
                return false;
            void* attributes = reinterpret_cast<void*>(
                itemView + kNetworkedDynamicAttributesOffset);
            g_addOrSetAttributeByName(attributes,
                "set item texture prefab", static_cast<float>(paintKit));
            g_addOrSetAttributeByName(attributes,
                "set item texture seed", static_cast<float>(seed));
            g_addOrSetAttributeByName(attributes,
                "set item texture wear", wear);
            if (statTrak >= 0) {
                g_addOrSetAttributeByName(attributes,
                    "kill eater", EncodeIntegerAttributeValue(
                        static_cast<uint32_t>(statTrak)));
                g_addOrSetAttributeByName(attributes,
                    "kill eater score type",
                    EncodeIntegerAttributeValue(
                        static_cast<uint32_t>(statTrakType)));
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon skin attributes: networked setter faulted.");
            return false;
        }
    }

    bool SafeClearStaticItemViewAttributes(uintptr_t itemView) {
        constexpr uintptr_t kStaticDynamicAttributesOffset = 0x208;
        constexpr uintptr_t kAttributeCountOffset = 0x08;
        const int emptyCount = 0;
        return itemView && SafeWrite(itemView +
            kStaticDynamicAttributesOffset + kAttributeCountOffset,
            emptyCount);
    }

    bool SafeRunWeaponCompositeOwnerTrial(uintptr_t weapon) {
        if (!weapon || !g_updateCompositeMaterial ||
            !g_compositeMaterialOwnerOffset)
            return false;
        __try {
            g_updateCompositeMaterial(reinterpret_cast<void*>(
                weapon + g_compositeMaterialOwnerOffset), true);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon material trial: CompositeOwner faulted.");
            return false;
        }
    }

    bool SafeRunWeaponCompositeSetTrial(uintptr_t weapon) {
        if (!weapon || !g_updateCompositeMaterialSet) return false;
        __try {
            g_updateCompositeMaterialSet(
                reinterpret_cast<void*>(weapon), false);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon material trial: CompositeSet faulted.");
            return false;
        }
    }

    bool SafeRunWeaponUpdateSkinTrial(
        uintptr_t weapon, bool rebuildCompositeSet = true) {
        if (!weapon || !g_updateWeaponSkin) return false;
        __try {
            g_updateWeaponSkin(reinterpret_cast<void*>(weapon),
                rebuildCompositeSet);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon material trial: UpdateSkin faulted.");
            return false;
        }
    }

    bool SafeRunWeaponUpdateSubclassTrial(uintptr_t weapon) {
        if (!weapon || !g_updateSubclass) return false;
        __try {
            g_updateSubclass(reinterpret_cast<void*>(weapon));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon material trial: UpdateSubclass faulted.");
            return false;
        }
    }

    struct WeaponGraphLegacyState {
        uintptr_t controller = 0;
        uintptr_t binding = 0;
        int16_t index = -1;
        uintptr_t value = 0;
        bool legacy = false;
        bool readable = false;
    };

    WeaponGraphLegacyState ReadWeaponGraphLegacyState(uintptr_t controller) {
        constexpr uintptr_t kLegacyParamOffset = 0x190;
        WeaponGraphLegacyState state{};
        if (!controller) return state;
        state.controller = controller;
        if (!SafeRead(state.controller + kLegacyParamOffset + 8,
                state.binding) ||
            !SafeRead(state.controller + kLegacyParamOffset + 0x10,
                state.index) ||
            !state.binding || state.index < 0)
            return state;
        uintptr_t values = 0;
        if (!SafeRead(state.binding + 0x10, values) || !values ||
            !SafeRead(values + static_cast<uintptr_t>(state.index) *
                sizeof(uintptr_t), state.value) || !state.value ||
            !SafeRead(state.value + 0x18, state.legacy))
            return state;
        state.readable = true;
        return state;
    }

    uintptr_t ResolveHudWeaponGraphController(uintptr_t weaponSceneNode,
        const InventorySnapshotSelection& selection) {
        constexpr uintptr_t kSceneNodeParentOffset = 0x38;
        constexpr uintptr_t kGraphControllerPointerOffset = 0x1048;
        if (!weaponSceneNode || !selection.sceneNodeOwnerOffset)
            return 0;
        uintptr_t parentSceneNode = 0;
        uintptr_t parentOwner = 0;
        uintptr_t controller = 0;
        uintptr_t vtable = 0;
        uintptr_t firstVirtual = 0;
        if (!SafeRead(weaponSceneNode + kSceneNodeParentOffset,
                parentSceneNode) || !parentSceneNode ||
            !SafeRead(parentSceneNode + selection.sceneNodeOwnerOffset,
                parentOwner) || !parentOwner ||
            !SafeRead(parentOwner + kGraphControllerPointerOffset,
                controller) || !controller ||
            !SafeRead(controller, vtable) || !vtable ||
            !SafeRead(vtable, firstVirtual) ||
            !IsExecutableAddress(reinterpret_cast<void*>(firstVirtual)))
            return 0;
        return controller;
    }

    bool SafeSyncWeaponGraphController(uintptr_t controller,
        uintptr_t weapon, WeaponGraphLegacyState& before,
        WeaponGraphLegacyState& after) {
        before = ReadWeaponGraphLegacyState(controller);
        after = before;
        if (!controller || !weapon || !g_updateWeaponGraphController)
            return false;
        uintptr_t vtable = 0;
        uintptr_t firstVirtual = 0;
        if (!SafeRead(controller, vtable) || !vtable ||
            !SafeRead(vtable, firstVirtual) ||
            !IsExecutableAddress(reinterpret_cast<void*>(firstVirtual)))
            return false;
        __try {
            g_updateWeaponGraphController(
                reinterpret_cast<void*>(controller),
                reinterpret_cast<void*>(weapon));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon graph: native synchronization faulted.");
            return false;
        }
        after = ReadWeaponGraphLegacyState(controller);
        return true;
    }

    bool SafePostDataUpdateSceneNode(
        uintptr_t entity, const InventorySnapshotSelection& selection) {
        if (!entity || !selection.gameSceneNodeOffset) return false;
        uintptr_t sceneNode = 0;
        if (!SafeRead(entity + selection.gameSceneNodeOffset, sceneNode) ||
            !sceneNode)
            return false;
        const auto postDataUpdate =
            GetVirtualFunction<SceneNodePostDataUpdateFn>(
                reinterpret_cast<void*>(sceneNode), 25);
        if (!IsExecutableAddress(reinterpret_cast<void*>(postDataUpdate)))
            return false;
        __try {
            postDataUpdate(reinterpret_cast<void*>(sceneNode), 0, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon skin scene: PostDataUpdate faulted.");
            return false;
        }
    }

    bool SafeSetMeshGroupMaskOnSceneNode(
        uintptr_t sceneNode, uint64_t mask) {
        if (!sceneNode || !g_setMeshGroupMask) return false;
        __try {
            g_setMeshGroupMask(reinterpret_cast<void*>(sceneNode), mask);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon skin scene: child mesh update faulted.");
            return false;
        }
    }

    bool SafePostDataUpdateSceneNodeAddress(uintptr_t sceneNode) {
        if (!sceneNode) return false;
        const auto postDataUpdate =
            GetVirtualFunction<SceneNodePostDataUpdateFn>(
                reinterpret_cast<void*>(sceneNode), 25);
        if (!IsExecutableAddress(reinterpret_cast<void*>(postDataUpdate)))
            return false;
        __try {
            postDataUpdate(reinterpret_cast<void*>(sceneNode), 0, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon skin scene: child PostDataUpdate faulted.");
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

    bool RefreshWeaponHudCache(uintptr_t itemView, const char* context) {
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
                "%s HUD: cache refrescada, entradas=%d.",
                context ? context : "Weapon", cleared);
            AppendLog(message);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            AppendLog("Weapon HUD: excepcion al refrescar; operacion cancelada.");
            return false;
        }
    }

    static uintptr_t s_lastRefreshedKnifeItemView = 0;
    void MaintainKnifeHud(uintptr_t itemView, ULONGLONG now) {
        if (!HasAppliedKnifeState() || g_appliedKnife.hudRefreshed ||
            now < g_appliedKnife.hudRefreshAt)
            return;

        if (s_lastRefreshedKnifeItemView == itemView && s_lastRefreshedKnifeItemView != 0) {
            g_appliedKnife.hudRefreshed = true;
            return;
        }

        ++g_appliedKnife.hudRefreshAttempts;
        g_appliedKnife.hudRefreshed = RefreshWeaponHudCache(
            itemView, "Knife");
        if (g_appliedKnife.hudRefreshed) {
            s_lastRefreshedKnifeItemView = itemView;
        } else {
            g_appliedKnife.hudRefreshAt = now + 250;
            if (g_appliedKnife.hudRefreshAttempts >= 3) {
                g_appliedKnife.hudRefreshed = true;
                s_lastRefreshedKnifeItemView = itemView;
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

    bool HasRetainedWeaponSkinState() {
        for (const auto& state : g_retainedWeaponSkins) {
            if (state.applied) return true;
        }
        return false;
    }

    void UpdateWeaponSkinAppliedFlag() {
        InterlockedExchange(&g_weaponSkinApplied,
            (g_appliedWeaponSkin.applied || HasRetainedWeaponSkinState())
                ? 1 : 0);
    }

    void ClearAppliedWeaponSkinState() {
        UnregisterWeaponTextureSeedOverride(g_appliedWeaponSkin.itemView);
        g_appliedWeaponSkin = {};
        UpdateWeaponSkinAppliedFlag();
    }

    void RetainAppliedWeaponSkinState() {
        if (!g_appliedWeaponSkin.applied) return;
        AppliedWeaponSkinState* destination = nullptr;
        for (auto& retained : g_retainedWeaponSkins) {
            if (retained.applied &&
                retained.weapon == g_appliedWeaponSkin.weapon) {
                destination = &retained;
                break;
            }
            if (!destination && !retained.applied)
                destination = &retained;
        }
        if (destination) *destination = g_appliedWeaponSkin;
        char message[192]{};
        StringCchPrintfA(message, _countof(message),
            "Weapon skin lifecycle: retained weapon=0x%llX handle=0x%08X local=%llu.",
            static_cast<unsigned long long>(g_appliedWeaponSkin.weapon),
            g_appliedWeaponSkin.weaponHandle,
            static_cast<unsigned long long>(g_appliedWeaponSkin.localId));
        AppendLog(message);
        g_appliedWeaponSkin = {};
        UpdateWeaponSkinAppliedFlag();
    }

    bool ResumeRetainedWeaponSkinState(
        uintptr_t weapon, uint32_t weaponHandle) {
        for (auto& retained : g_retainedWeaponSkins) {
            if (!retained.applied || retained.weapon != weapon ||
                retained.weaponHandle != weaponHandle)
                continue;
            g_appliedWeaponSkin = retained;
            retained = {};
            // A newly rebound viewmodel needs one maintenance pass, but the
            // weapon material itself can remain cached across switches/drops.
            g_appliedWeaponSkin.sceneUpdatePending = true;
            g_appliedWeaponSkin.sceneUpdateAt = 0;
            g_appliedWeaponSkin.sceneUpdateAttempts = 0;
            g_appliedWeaponSkin.nextUpdateAt = 0;
            (void)RegisterWeaponTextureSeedOverride(
                g_appliedWeaponSkin.itemView,
                g_appliedWeaponSkin.itemIdOffset,
                g_appliedWeaponSkin.generatedItemId,
                g_appliedWeaponSkin.paintKit,
                g_appliedWeaponSkin.seed, true);
            char message[192]{};
            StringCchPrintfA(message, _countof(message),
                "Weapon skin lifecycle: resumed weapon=0x%llX handle=0x%08X local=%llu.",
                static_cast<unsigned long long>(weapon), weaponHandle,
                static_cast<unsigned long long>(
                    g_appliedWeaponSkin.localId));
            AppendLog(message);
            UpdateWeaponSkinAppliedFlag();
            return true;
        }
        return false;
    }

    const InventorySnapshotSelection::WeaponSkinCollectionItem*
    FindEquippedWeaponSkin(
        const InventorySnapshotSelection& selection, int definitionIndex,
        int gameTeam) {
        const int protocolTeam = gameTeam == kTerroristTeam
            ? 1 : gameTeam == kCounterTerroristTeam ? 2 : 0;
        for (int index = 0; index < selection.weaponSkinItemCount; ++index) {
            const auto& item = selection.weaponSkinItems[index];
            if (item.definitionIndex == definitionIndex &&
                (item.equippedTeam & protocolTeam) != 0)
                return &item;
        }
        // Fallback: If no skin is equipped specifically for current team (e.g. CT picking up an AK-47,
        // or T picking up an M4A1-S/M4A4), apply the player's configured skin from the opposing team.
        for (int index = 0; index < selection.weaponSkinItemCount; ++index) {
            const auto& item = selection.weaponSkinItems[index];
            if (item.definitionIndex == definitionIndex &&
                item.equippedTeam != 0)
                return &item;
        }
        return nullptr;
    }

    const InventorySnapshotSelection::WeaponSkinCollectionItem*
    FindWeaponSkinForLiveEntity(
        const InventorySnapshotSelection& selection, int definitionIndex,
        int gameTeam, uintptr_t weapon, uintptr_t itemView) {
        // Once an entity has acquired one of our generated identities, keep
        // that identity for the lifetime of the entity. Changing the loadout
        // should affect future purchases, not repaint a dropped weapon when
        // it is picked up again.
        if (g_appliedWeaponSkin.applied &&
            g_appliedWeaponSkin.weapon == weapon) {
            const auto* retainedItem = FindWeaponSkinCollectionItem(
                selection, g_appliedWeaponSkin.localId);
            if (retainedItem &&
                retainedItem->definitionIndex == definitionIndex)
                return retainedItem;
        }

        uint64_t liveItemId = 0;
        uint64_t liveLocalId = 0;
        if (itemView && selection.itemIdOffset &&
            SafeRead(itemView + selection.itemIdOffset, liveItemId) &&
            liveItemId && ResolveInventorySocacheGeneratedLocalId(
                liveItemId, liveLocalId)) {
            const auto* liveItem = FindWeaponSkinCollectionItem(
                selection, liveLocalId);
            if (liveItem && liveItem->definitionIndex == definitionIndex)
                return liveItem;
        }

        return FindEquippedWeaponSkin(selection, definitionIndex, gameTeam);
    }

    bool WeaponObserverDue(ULONGLONG now) {
        if (now < g_nextWeaponObserverLogAt) return false;
        g_nextWeaponObserverLogAt = now + 10000;
        return true;
    }

    void LogWeaponObserverWait(const char* state, int team = 0,
        int definitionIndex = 0) {
        char message[224]{};
        StringCchPrintfA(message, _countof(message),
            "Weapon observer: state=%s stage6=%ld team=%d def=%d "
            "attr_setter=%s.",
            state ? state : "unknown",
            InterlockedCompareExchange(&g_frameStageSixCalls, 0, 0),
            team, definitionIndex,
            g_setItemViewAttributeByName ? "itemview" : "no");
        AppendLog(message);
    }

    void RestoreAppliedWeaponSkin(
        const InventorySnapshotSelection& selection, const char* reason) {
        if (!g_appliedWeaponSkin.applied) {
            ClearAppliedWeaponSkinState();
            return;
        }

        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t entityList = 0;
        const bool entityValid = client && selection.entityListOffset &&
            SafeRead(reinterpret_cast<uintptr_t>(client) +
                selection.entityListOffset, entityList) && entityList &&
            IsKnifeEntityHandleValid(entityList,
                g_appliedWeaponSkin.entityStride,
                g_appliedWeaponSkin.weapon,
                g_appliedWeaponSkin.weaponHandle);
        if (!entityValid) {
            AppendLog("Weapon skin: restauracion omitida; entidad expirada.");
            ClearAppliedWeaponSkinState();
            return;
        }

        const uintptr_t itemView = g_appliedWeaponSkin.weapon +
            selection.attributeManagerOffset + selection.itemOffset;
        constexpr uintptr_t kRestoreCustomMaterialOffset = 0x1B8;
        constexpr uintptr_t kDisallowSocOffset = 0x1E9;
        const bool contextCleared =
            ClearInventorySocacheItemView(itemView);
        const bool restored =
            SafeWrite(itemView + selection.itemDefinitionIndexOffset,
                g_appliedWeaponSkin.originalDefinition) &&
            SafeWrite(itemView + selection.entityQualityOffset,
                g_appliedWeaponSkin.originalQuality) &&
            SafeWrite(itemView + selection.itemIdOffset,
                g_appliedWeaponSkin.originalItemId) &&
            SafeWrite(itemView + selection.itemIdHighOffset,
                g_appliedWeaponSkin.originalItemIdHigh) &&
            SafeWrite(itemView + selection.itemIdLowOffset,
                g_appliedWeaponSkin.originalItemIdLow) &&
            SafeWrite(itemView + selection.accountIdOffset,
                g_appliedWeaponSkin.originalAccountId) &&
            SafeWrite(itemView + selection.initializedOffset,
                g_appliedWeaponSkin.originalInitialized) &&
            SafeWrite(g_appliedWeaponSkin.weapon +
                selection.fallbackPaintKitOffset,
                g_appliedWeaponSkin.originalPaintKit) &&
            SafeWrite(g_appliedWeaponSkin.weapon +
                selection.fallbackSeedOffset,
                g_appliedWeaponSkin.originalSeed) &&
            SafeWrite(g_appliedWeaponSkin.weapon +
                selection.fallbackWearOffset,
                g_appliedWeaponSkin.originalWear) &&
            SafeWrite(g_appliedWeaponSkin.weapon +
                selection.fallbackStatTrakOffset,
                g_appliedWeaponSkin.originalStatTrak) &&
            SafeWrite(itemView + kRestoreCustomMaterialOffset,
                g_appliedWeaponSkin.originalRestoreCustomMaterial) &&
            SafeWrite(itemView + kDisallowSocOffset,
                g_appliedWeaponSkin.originalDisallowSoc);

        char message[224]{};
        StringCchPrintfA(message, _countof(message),
            "Weapon skin: %s local=%llu def=%d context_cleared=%s "
            "restored=%s.",
            reason ? reason : "restore",
            static_cast<unsigned long long>(g_appliedWeaponSkin.localId),
            g_appliedWeaponSkin.definitionIndex,
            contextCleared ? "yes" : "no",
            restored ? "yes" : "no");
        AppendLog(message);
        ClearAppliedWeaponSkinState();
    }

    void RestoreRetainedWeaponSkins(
        const InventorySnapshotSelection& selection, const char* reason) {
        for (auto& retained : g_retainedWeaponSkins) {
            if (!retained.applied) continue;
            g_appliedWeaponSkin = retained;
            retained = {};
            RestoreAppliedWeaponSkin(selection, reason);
        }
        UpdateWeaponSkinAppliedFlag();
    }

    void RunWeaponSkinRuntimeControl(WeaponSkinRuntimePhase phase) {
        const ULONGLONG now = GetTickCount64();
        const bool earlyStage =
            phase == WeaponSkinRuntimePhase::EarlyNetUpdate;
        const bool sceneMaintenanceDue =
            g_appliedWeaponSkin.sceneUpdatePending &&
            now >= g_appliedWeaponSkin.sceneUpdateAt;
        const bool materialMaintenanceDue =
            g_appliedWeaponSkin.materialRefreshPending &&
            now >= g_appliedWeaponSkin.materialRefreshAt;
        const bool statTrakModuleMaintenanceDue =
            g_appliedWeaponSkin.statTrakModuleRefreshPending &&
            now >= g_appliedWeaponSkin.statTrakModuleRefreshAt;
        const bool urgentMaintenance = sceneMaintenanceDue ||
            materialMaintenanceDue || statTrakModuleMaintenanceDue;

        // The final networked-attribute path used to bypass the RenderEnd
        // throttle entirely, causing the full weapon context/readback path to
        // run every frame. Stable state only needs periodic drift detection;
        // pending material/scene work remains immediate.
        if (!earlyStage && !urgentMaintenance &&
            now < g_appliedWeaponSkin.nextUpdateAt)
            return;

        // EarlyNetUpdate exists to catch a newly created/switched weapon before
        // its first composite. Once a skin is already stable, probing at most
        // once per display-frame-sized interval avoids duplicate high-FPS work
        // while keeping weapon changes effectively immediate.
        if (earlyStage && g_appliedWeaponSkin.applied && !urgentMaintenance) {
            if (now < g_nextWeaponSkinEarlyProbeAt)
                return;
            g_nextWeaponSkinEarlyProbeAt =
                now + kWeaponSkinEarlyProbeIntervalMs;
        }

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection)) {
            if (WeaponObserverDue(now))
                LogWeaponObserverWait("snapshot-missing");
            return;
        }
        if (!selection.attributeManagerOffset || !selection.itemOffset ||
            !selection.itemDefinitionIndexOffset ||
            !selection.entityQualityOffset || !selection.itemIdOffset ||
            !selection.itemIdHighOffset || !selection.itemIdLowOffset ||
            !selection.accountIdOffset || !selection.initializedOffset ||
            !selection.fallbackPaintKitOffset ||
            !selection.fallbackSeedOffset ||
            !selection.fallbackWearOffset ||
            !selection.fallbackStatTrakOffset) {
            ClearWeaponTextureSeedOverrides();
            g_appliedWeaponSkin = {};
            for (auto& retained : g_retainedWeaponSkins) retained = {};
            UpdateWeaponSkinAppliedFlag();
            if (WeaponObserverDue(now))
                LogWeaponObserverWait("offsets-missing");
            return;
        }
        if (!selection.enabled) {
            RestoreAppliedWeaponSkin(selection, "disabled");
            RestoreRetainedWeaponSkins(selection, "disabled-retained");
            if (WeaponObserverDue(now))
                LogWeaponObserverWait("inventory-disabled");
            return;
        }

        HMODULE client = GetModuleHandleW(L"client.dll");
        uintptr_t controller = 0;
        uintptr_t pawn = 0;
        uintptr_t stride = 0;
        if (!ResolveAccountPawn(client, selection, controller, pawn, stride) ||
            !pawn) {
            if (g_appliedWeaponSkin.applied)
                RetainAppliedWeaponSkinState();
            if (WeaponObserverDue(now))
                LogWeaponObserverWait("pawn-missing");
            return;
        }

        int team = 0;
        if (!SafeRead(pawn + selection.teamNumberOffset, team)) return;
        float spawnTimeIndex = 0.0f;
        if (selection.lastSpawnTimeIndexOffset)
            (void)SafeRead(pawn + selection.lastSpawnTimeIndexOffset,
                spawnTimeIndex);
        uintptr_t entityList = 0;
        uint32_t weaponHandle = 0;
        uintptr_t weapon = 0;
        if (!ResolveActiveWeaponForPawn(client, selection, pawn, stride,
                entityList, weaponHandle, weapon)) {
            if (g_appliedWeaponSkin.applied)
                RetainAppliedWeaponSkinState();
            if (WeaponObserverDue(now))
                LogWeaponObserverWait("active-weapon-missing", team);
            return;
        }

        const uintptr_t itemView = weapon +
            selection.attributeManagerOffset + selection.itemOffset;
        uint16_t definitionIndex = 0;
        if (!SafeRead(itemView + selection.itemDefinitionIndexOffset,
                definitionIndex))
            return;
        if (IsKnifeDefinition(definitionIndex)) {
            if (g_appliedWeaponSkin.applied)
                RetainAppliedWeaponSkinState();
            if (WeaponObserverDue(now))
                LogWeaponObserverWait("knife-active", team, definitionIndex);
            return;
        }

        // Stage 3 runs every frame so a newly spawned weapon receives its
        // finish before the renderer creates the first material composite.
        if (earlyStage && g_appliedWeaponSkin.applied &&
            g_appliedWeaponSkin.weapon == weapon &&
            g_appliedWeaponSkin.weaponHandle == weaponHandle)
            return;

        if (g_appliedWeaponSkin.applied &&
            g_appliedWeaponSkin.weapon != weapon)
            RetainAppliedWeaponSkinState();
        if (!g_appliedWeaponSkin.applied)
            (void)ResumeRetainedWeaponSkinState(weapon, weaponHandle);

        const auto* target = FindWeaponSkinForLiveEntity(
            selection, definitionIndex, team, weapon, itemView);
        if (!target) {
            if (g_appliedWeaponSkin.applied)
                RestoreAppliedWeaponSkin(selection, "selection-missing");
            if (WeaponObserverDue(now))
                LogWeaponObserverWait("selection-missing", team,
                    definitionIndex);
            return;
        }
        if (g_appliedWeaponSkin.applied &&
            g_appliedWeaponSkin.localId != target->localId) {
            RestoreAppliedWeaponSkin(selection, "selection-changed");
            return;
        }

        InventorySocacheItemSpec spec{};
        spec.localId = target->localId;
        spec.team = 0;
        spec.definitionIndex = target->definitionIndex;
        spec.paintKit = target->paintKit;
        spec.seed = target->seed;
        spec.wear = target->wear;
        spec.statTrak = target->statTrak;
        spec.statTrakCount = target->statTrakCount;
        spec.statTrakType = 0;
        spec.quality = target->quality;
        spec.rarity = target->rarity;
        spec.loadoutSlot = kWeaponSkinCollectionSlot;
        spec.itemViewItemIdOffset = selection.itemIdOffset;
        spec.unacknowledged = IsPendingRevealItem(
            selection, target->localId);
        spec.equip = false;
        InventorySocacheItemIdentity identity{};
        if (!EnsureInventorySocacheItem(spec, identity)) {
            if (WeaponObserverDue(now))
                LogWeaponObserverWait("socache-item-missing", team,
                    definitionIndex);
            return;
        }
        uintptr_t sourceItemView = 0;
        int sourceLoadoutSlot = -1;
        uint64_t liveItemId = 0;
        const bool liveIdentityMatches =
            SafeRead(itemView + selection.itemIdOffset, liveItemId) &&
            liveItemId == identity.itemId;
        const bool sourceItemViewReady =
            ResolveInventorySocacheLoadoutItemView(target->localId, team,
                selection.itemIdOffset, sourceItemView,
                sourceLoadoutSlot);
        if (!sourceItemViewReady && !liveIdentityMatches) {
            if (WeaponObserverDue(now))
                LogWeaponObserverWait("loadout-view-missing", team,
                    definitionIndex);
            return;
        }
        identity.loadoutItemView = sourceItemView;

        constexpr uintptr_t kRestoreCustomMaterialOffset = 0x1B8;
        constexpr uintptr_t kDisallowSocOffset = 0x1E9;
        if (!g_appliedWeaponSkin.applied) {
            AppliedWeaponSkinState next{};
            next.pawn = pawn;
            next.entityStride = stride;
            next.weapon = weapon;
            next.weaponHandle = weaponHandle;
            next.localId = target->localId;
            next.definitionIndex = target->definitionIndex;
            next.paintKit = target->paintKit;
            next.seed = target->seed;
            next.wear = target->wear;
            next.statTrak = target->statTrak
                ? target->statTrakCount : -1;
            next.quality = target->quality;
            next.generatedItemId = identity.itemId;
            next.itemView = itemView;
            next.spawnTimeIndex = spawnTimeIndex;
            next.lifecycleGeneration =
                g_weaponSkinLifecycleGeneration.load();
            next.itemIdOffset = selection.itemIdOffset;
            next.itemIdHighOffset = selection.itemIdHighOffset;
            next.itemIdLowOffset = selection.itemIdLowOffset;
            next.initializedOffset = selection.initializedOffset;
            next.fallbackPaintKitOffset =
                selection.fallbackPaintKitOffset;
            next.fallbackSeedOffset = selection.fallbackSeedOffset;
            next.fallbackWearOffset = selection.fallbackWearOffset;
            next.fallbackStatTrakOffset =
                selection.fallbackStatTrakOffset;
            next.nextUpdateAt = now + kWeaponSkinStableCheckIntervalMs;
            next.hudRefreshAt = now + kWeaponSkinHudRefreshDelayMs;
            const bool captured =
                SafeRead(itemView + selection.itemDefinitionIndexOffset,
                    next.originalDefinition) &&
                SafeRead(itemView + selection.entityQualityOffset,
                    next.originalQuality) &&
                SafeRead(itemView + selection.itemIdOffset,
                    next.originalItemId) &&
                SafeRead(itemView + selection.itemIdHighOffset,
                    next.originalItemIdHigh) &&
                SafeRead(itemView + selection.itemIdLowOffset,
                    next.originalItemIdLow) &&
                SafeRead(itemView + selection.accountIdOffset,
                    next.originalAccountId) &&
                SafeRead(itemView + selection.initializedOffset,
                    next.originalInitialized) &&
                SafeRead(weapon + selection.fallbackPaintKitOffset,
                    next.originalPaintKit) &&
                SafeRead(weapon + selection.fallbackSeedOffset,
                    next.originalSeed) &&
                SafeRead(weapon + selection.fallbackWearOffset,
                    next.originalWear) &&
                SafeRead(weapon + selection.fallbackStatTrakOffset,
                    next.originalStatTrak) &&
                SafeRead(itemView + kRestoreCustomMaterialOffset,
                    next.originalRestoreCustomMaterial) &&
                SafeRead(itemView + kDisallowSocOffset,
                    next.originalDisallowSoc);
            if (!captured) return;
            g_appliedWeaponSkin = next;
            g_weaponMaterialHookLogs.store(0);
            g_weaponSkinMidpointLogged.store(false);
        }

        uint64_t currentItemId = 0;
        int32_t currentQuality = 0;
        int currentPaintKit = 0;
        int currentSeed = 0;
        float currentWear = 0.0f;
        int currentStatTrak = -1;
        (void)SafeRead(itemView + selection.itemIdOffset, currentItemId);
        (void)SafeRead(itemView + selection.entityQualityOffset,
            currentQuality);
        (void)SafeRead(weapon + selection.fallbackPaintKitOffset,
            currentPaintKit);
        (void)SafeRead(weapon + selection.fallbackSeedOffset, currentSeed);
        (void)SafeRead(weapon + selection.fallbackWearOffset, currentWear);
        (void)SafeRead(weapon + selection.fallbackStatTrakOffset,
            currentStatTrak);
        const int targetStatTrak = target->statTrak
            ? target->statTrakCount : -1;
        const float wearDelta = currentWear - target->wear;
        const bool spawnChanged = g_appliedWeaponSkin.applied &&
            selection.lastSpawnTimeIndexOffset &&
            g_appliedWeaponSkin.spawnTimeIndex != spawnTimeIndex;
        const uint32_t lifecycleGeneration =
            g_weaponSkinLifecycleGeneration.load();
        const bool lifecycleChanged = g_appliedWeaponSkin.applied &&
            g_appliedWeaponSkin.lifecycleGeneration != lifecycleGeneration;
        const bool statTrakModeChanged =
            (currentStatTrak < 0) != (targetStatTrak < 0);
        const bool statTrakCountDrifted = !statTrakModeChanged &&
            targetStatTrak >= 0 && currentStatTrak != targetStatTrak;
        const bool visualDrifted = spawnChanged || lifecycleChanged ||
            currentItemId != identity.itemId ||
            currentQuality != target->quality ||
            currentPaintKit != target->paintKit ||
            currentSeed != target->seed || wearDelta > 0.000001f ||
            wearDelta < -0.000001f || statTrakModeChanged;
        const bool drifted = visualDrifted || statTrakCountDrifted;

        const bool observerDue = WeaponObserverDue(now);
        if (observerDue) {
            uint8_t itemViewInitialized = 0;
            uint8_t attributesInitialized = 0;
            int dynamicAttributeCount = -1;
            int networkedAttributeCount = -1;
            (void)SafeRead(itemView + selection.initializedOffset,
                itemViewInitialized);
            if (selection.attributeManagerOffset >= 8)
                (void)SafeRead(weapon + selection.attributeManagerOffset - 8,
                    attributesInitialized);
            (void)SafeRead(itemView + 0x210, dynamicAttributeCount);
            (void)SafeRead(itemView + 0x288, networkedAttributeCount);
            int materialPrimaryCount = -1;
            int materialSecondaryCount = -1;
            uint8_t restoreCustomMaterial = 0;
            (void)SafeRead(weapon + 0xAA8, materialPrimaryCount);
            (void)SafeRead(weapon + 0xAC0, materialSecondaryCount);
            (void)SafeRead(itemView + kRestoreCustomMaterialOffset,
                restoreCustomMaterial);
            char message[480]{};
            StringCchPrintfA(message, _countof(message),
                "Weapon observer: state=target stage6=%ld team=%d def=%u "
                "local=%llu weapon=0x%llX view=0x%llX source=0x%llX "
                "slot=%d id=%llu quality=%d/%u paint=%d/%d seed=%d/%d "
                "wear=%.6f/%.6f stattrak=%d/%d init=%u/%u attrs=%d/%d "
                "materials=%d/%d restore=%u drift=%s setter=%s.",
                InterlockedCompareExchange(&g_frameStageSixCalls, 0, 0),
                team, static_cast<unsigned>(definitionIndex),
                static_cast<unsigned long long>(target->localId),
                static_cast<unsigned long long>(weapon),
                static_cast<unsigned long long>(itemView),
                static_cast<unsigned long long>(sourceItemView),
                sourceLoadoutSlot,
                static_cast<unsigned long long>(currentItemId),
                currentQuality, static_cast<unsigned>(target->quality),
                currentPaintKit, target->paintKit,
                currentSeed, target->seed, currentWear, target->wear,
                currentStatTrak, targetStatTrak,
                static_cast<unsigned>(itemViewInitialized),
                static_cast<unsigned>(attributesInitialized),
                dynamicAttributeCount, networkedAttributeCount,
                materialPrimaryCount, materialSecondaryCount,
                static_cast<unsigned>(restoreCustomMaterial),
                drifted ? "yes" : "no",
                g_setItemViewAttributeByName ? "itemview" : "no");
            AppendLog(message);
            LogInventorySocacheItemViewDiagnostics(
                itemView, identity.loadoutItemView);
        }

        if (!g_appliedWeaponSkin.applied || visualDrifted) {
            const bool restoreCustomMaterial = true;
            const bool allowSoc = false;
            g_appliedWeaponSkin.applied = true;
            const bool textureSeedRegistered =
                RegisterWeaponTextureSeedOverride(
                    itemView, selection.itemIdOffset, identity.itemId,
                    target->paintKit, target->seed, false);
            const bool textureSeedGetterOverridden =
                textureSeedRegistered &&
                InstallItemViewGetTextureSeedOverride(
                    reinterpret_cast<void*>(itemView));
            const bool itemViewCopied = !identity.loadoutItemView ||
                CopyInventorySocacheItemView(
                    itemView, identity.loadoutItemView);
            const bool identityWritten = WriteInventoryItemIdentity(
                itemView, target->definitionIndex, identity, selection,
                target->quality);
            const bool finishWritten = WriteKnifeFinish(
                weapon, itemView, selection, target->paintKit,
                target->seed, target->wear, targetStatTrak,
                static_cast<uint32_t>(identity.itemId >> 32));
            constexpr uint32_t kFallbackItemIdPart = 0xFFFFFFFFu;
            const bool fallbackIdentityWritten =
                SafeWrite(itemView + selection.itemIdHighOffset,
                    kFallbackItemIdPart) &&
                SafeWrite(itemView + selection.itemIdLowOffset,
                    kFallbackItemIdPart);
            const bool flagsWritten = SafeWrite(itemView +
                    kRestoreCustomMaterialOffset, restoreCustomMaterial) &&
                SafeWrite(itemView + kDisallowSocOffset, allowSoc);
            const bool staticAttributesCleared =
                SafeClearStaticItemViewAttributes(itemView);
            if (!textureSeedGetterOverridden || !itemViewCopied ||
                !identityWritten || !finishWritten ||
                !fallbackIdentityWritten || !flagsWritten ||
                !staticAttributesCleared) {
                RestoreAppliedWeaponSkin(selection, "apply-write-failed");
                return;
            }
            if (!RegisterWeaponTextureSeedOverride(
                    itemView, selection.itemIdOffset, identity.itemId,
                    target->paintKit, target->seed, true)) {
                RestoreAppliedWeaponSkin(
                    selection, "texture-seed-registry-failed");
                return;
            }

            uintptr_t viewmodel = ResolveHudViewModelForWeapon(
                entityList, pawn, weapon, stride, selection);
            const uintptr_t viewmodelSceneNode = ResolveHudWeaponSceneNode(
                entityList, pawn, weapon, stride, selection);
            const uint64_t meshGroupMask =
                WeaponMeshGroupMask(target->legacyModel);
            // The native attribute setter can start material composition
            // immediately. Select the legacy/modern mesh first so the
            // composite is generated against the matching UV layout.
            const bool weaponMeshUpdated = SafeSetMeshGroupMask(
                weapon, selection, meshGroupMask);
            const bool viewmodelMeshUpdated = viewmodel &&
                SafeSetMeshGroupMask(viewmodel, selection, meshGroupMask);
            const bool viewmodelSceneMeshUpdated = viewmodelSceneNode &&
                SafeSetMeshGroupMaskOnSceneNode(
                    viewmodelSceneNode, meshGroupMask);
            const bool itemViewAttrsWritten =
                SafeSetItemViewSkinAttributes(itemView,
                    target->paintKit, target->seed, target->wear,
                    targetStatTrak);
            (void)itemViewAttrsWritten;
            const bool attributesWritten =
                SafeSetNetworkedItemViewSkinAttributes(itemView,
                    target->paintKit, target->seed, target->wear,
                    targetStatTrak);
            bool initialModulesChanged = false;
            const bool modulesRefreshed = targetStatTrak >= 0 &&
                SafeRefreshWeaponModules(weapon, itemView, initialModulesChanged);
            const bool weaponPostUpdated = !earlyStage &&
                SafePostDataUpdateSceneNode(weapon, selection);
            const bool viewmodelPostUpdated = !earlyStage && viewmodel &&
                SafePostDataUpdateSceneNode(viewmodel, selection);
            g_appliedWeaponSkin.sceneUpdatePending = earlyStage;
            g_appliedWeaponSkin.sceneUpdateAt = earlyStage ? now : 0;
            g_appliedWeaponSkin.sceneUpdateAttempts = 0;
            g_appliedWeaponSkin.materialRefreshPending = true;
            g_appliedWeaponSkin.materialRefreshAt = now;
            g_appliedWeaponSkin.materialRefreshAttempts = 0;
            g_appliedWeaponSkin.statTrakModuleRefreshPending = (targetStatTrak >= 0 && !modulesRefreshed);
            g_appliedWeaponSkin.statTrakModuleRefreshAt = modulesRefreshed ? 0 : now + kWeaponSkinStatTrakModuleRetryIntervalMs;
            g_appliedWeaponSkin.statTrakModuleRefreshAttempts = 0;
            g_appliedWeaponSkin.statTrak = targetStatTrak;
            g_appliedWeaponSkin.spawnTimeIndex = spawnTimeIndex;
            g_appliedWeaponSkin.lifecycleGeneration = lifecycleGeneration;
            LogInventorySocacheItemViewDiagnostics(
                itemView, identity.loadoutItemView);
            g_appliedWeaponSkin.generatedItemId = identity.itemId;
            g_appliedWeaponSkin.hudRefreshed = false;
            g_appliedWeaponSkin.hudRefreshAttempts = 0;
            g_appliedWeaponSkin.hudRefreshAt = now + 250;
            UpdateWeaponSkinAppliedFlag();

            char message[448]{};
            StringCchPrintfA(message, _countof(message),
                "Weapon skin: applied local=%llu def=%d paint=%d seed=%d "
                "wear=%.6f stattrak=%d quality=%u legacy=%s mesh=%llu "
                "slot=%d phase=%s order=mesh-before-attrs view_copy=%s "
                "item=%llu attrs=%s/%s fallback_id=%s "
                "mesh_update=%s/%s/%s child=0x%llX "
                "post_update=%s/%s spawn_changed=%s lifecycle_changed=%s "
                "getter=%s.",
                static_cast<unsigned long long>(target->localId),
                target->definitionIndex, target->paintKit, target->seed,
                target->wear, targetStatTrak,
                static_cast<unsigned>(target->quality),
                target->legacyModel ? "yes" : "no",
                static_cast<unsigned long long>(meshGroupMask),
                sourceLoadoutSlot,
                earlyStage ? "early" : "render",
                itemViewCopied ? "yes" : "failed",
                static_cast<unsigned long long>(identity.itemId),
                attributesWritten ? "yes" : "no",
                "networked",
                fallbackIdentityWritten ? "yes" : "no",
                weaponMeshUpdated ? "yes" : "no",
                viewmodelMeshUpdated ? "yes" : "no",
                viewmodelSceneMeshUpdated ? "yes" : "no",
                static_cast<unsigned long long>(viewmodelSceneNode),
                earlyStage ? "deferred" :
                    (weaponPostUpdated ? "yes" : "no"),
                earlyStage ? "deferred" :
                    (viewmodelPostUpdated ? "yes" : "no"),
                spawnChanged ? "yes" : "no",
                lifecycleChanged ? "yes" : "no",
                textureSeedGetterOverridden ? "yes" : "no");
            AppendLog(message);
        }

        if (g_appliedWeaponSkin.applied && statTrakCountDrifted &&
            !visualDrifted) {
            const bool fallbackUpdated = SafeWrite(
                weapon + selection.fallbackStatTrakOffset, targetStatTrak);
            const bool attributesUpdated = fallbackUpdated &&
                SafeSetNetworkedItemViewStatTrakAttributes(
                    itemView, targetStatTrak);
            bool modulesChanged = false;
            const bool modulesRefreshed = attributesUpdated &&
                SafeRefreshWeaponModules(weapon, itemView, modulesChanged);
            if (attributesUpdated) {
                g_appliedWeaponSkin.statTrak = targetStatTrak;
                g_appliedWeaponSkin.statTrakModuleRefreshPending =
                    !modulesRefreshed;
                g_appliedWeaponSkin.statTrakModuleRefreshAt =
                    modulesRefreshed ? 0 :
                    now + kWeaponSkinStatTrakModuleRetryIntervalMs;
                g_appliedWeaponSkin.statTrakModuleRefreshAttempts = 0;
                (void)SafePostDataUpdateSceneNode(weapon, selection);
            }
            char message[256]{};
            StringCchPrintfA(message, _countof(message),
                "Weapon StatTrak: count=%d attrs=%s modules=%s/%s "
                "pending=%s.",
                targetStatTrak,
                attributesUpdated ? "yes" : "no",
                modulesRefreshed ? "yes" : "no",
                modulesChanged ? "changed" : "stable",
                g_appliedWeaponSkin.statTrakModuleRefreshPending
                    ? "yes" : "no");
            AppendLog(message);
        }

        const bool runtimeMaintenanceNeeded = visualDrifted ||
            sceneMaintenanceDue || materialMaintenanceDue;
        if (!earlyStage && g_appliedWeaponSkin.applied &&
            runtimeMaintenanceNeeded) {
            constexpr uint32_t kFallbackItemIdPart = 0xFFFFFFFFu;
            const uint64_t meshGroupMask =
                WeaponMeshGroupMask(target->legacyModel);
            const uintptr_t viewmodel = ResolveHudViewModelForWeapon(
                entityList, pawn, weapon, stride, selection);
            const uintptr_t viewmodelSceneNode = ResolveHudWeaponSceneNode(
                entityList, pawn, weapon, stride, selection);
            (void)SafeWrite(itemView + selection.itemIdHighOffset,
                kFallbackItemIdPart);
            (void)SafeWrite(itemView + selection.itemIdLowOffset,
                kFallbackItemIdPart);
            (void)SafeClearStaticItemViewAttributes(itemView);
            (void)SafeSetNetworkedItemViewSkinAttributes(itemView,
                target->paintKit, target->seed, target->wear,
                targetStatTrak);
            (void)SafeSetMeshGroupMask(weapon, selection, meshGroupMask);
            if (viewmodel)
                (void)SafeSetMeshGroupMask(
                    viewmodel, selection, meshGroupMask);
            if (viewmodelSceneNode)
                (void)SafeSetMeshGroupMaskOnSceneNode(
                    viewmodelSceneNode, meshGroupMask);
        }

        if (!earlyStage && g_appliedWeaponSkin.materialRefreshPending &&
            now >= g_appliedWeaponSkin.materialRefreshAt) {
            const uintptr_t viewmodel = ResolveHudViewModelForWeapon(
                entityList, pawn, weapon, stride, selection);
            const uintptr_t viewmodelSceneNode = ResolveHudWeaponSceneNode(
                entityList, pawn, weapon, stride, selection);
            const uint64_t meshGroupMask =
                WeaponMeshGroupMask(target->legacyModel);
            const bool getterReady = RegisterWeaponTextureSeedOverride(
                    itemView, selection.itemIdOffset, identity.itemId,
                    target->paintKit, target->seed, true) &&
                InstallItemViewGetTextureSeedOverride(
                    reinterpret_cast<void*>(itemView));
            const bool materialsCleared = getterReady &&
                SafeRunWeaponCompositeOwnerTrial(weapon);
            const bool attributesReapplied = materialsCleared &&
                SafeSetItemViewSkinAttributes(itemView,
                    target->paintKit, target->seed, target->wear,
                    targetStatTrak);
            const bool skinUpdated = attributesReapplied &&
                SafeRunWeaponUpdateSkinTrial(weapon, true);
            bool modulesChanged = false;
            const bool modulesRefreshed = targetStatTrak < 0 ||
                (skinUpdated && SafeRefreshWeaponModules(
                    weapon, itemView, modulesChanged));
            const bool weaponMeshUpdated = SafeSetMeshGroupMask(
                weapon, selection, meshGroupMask);
            const bool viewmodelMeshUpdated = viewmodel &&
                SafeSetMeshGroupMask(
                    viewmodel, selection, meshGroupMask);
            const bool childMeshUpdated = viewmodelSceneNode &&
                SafeSetMeshGroupMaskOnSceneNode(
                    viewmodelSceneNode, meshGroupMask);
            const bool weaponPostUpdated = SafePostDataUpdateSceneNode(
                weapon, selection);
            const bool viewmodelPostUpdated = viewmodel &&
                SafePostDataUpdateSceneNode(viewmodel, selection);
            const bool refreshed = getterReady && materialsCleared &&
                attributesReapplied && skinUpdated && weaponMeshUpdated;
            ++g_appliedWeaponSkin.materialRefreshAttempts;
            if (refreshed) {
                g_appliedWeaponSkin.materialRefreshPending = false;
                g_appliedWeaponSkin.materialRefreshAt = 0;
                g_appliedWeaponSkin.materialRefreshAttempts = 0;
            } else {
                const ULONGLONG retryDelay =
                    g_appliedWeaponSkin.materialRefreshAttempts <=
                        kWeaponSkinFastMaterialRetryAttempts
                    ? kWeaponSkinMaterialRetryIntervalMs
                    : kWeaponSkinSlowMaterialRetryIntervalMs;
                g_appliedWeaponSkin.materialRefreshPending = true;
                g_appliedWeaponSkin.materialRefreshAt = now + retryDelay;
            }
            if (targetStatTrak >= 0 && refreshed && !modulesRefreshed) {
                g_appliedWeaponSkin.statTrakModuleRefreshPending = true;
                g_appliedWeaponSkin.statTrakModuleRefreshAt =
                    now + kWeaponSkinStatTrakModuleRetryIntervalMs;
                g_appliedWeaponSkin.statTrakModuleRefreshAttempts = 0;
            } else if (modulesRefreshed) {
                g_appliedWeaponSkin.statTrakModuleRefreshPending = false;
                g_appliedWeaponSkin.statTrakModuleRefreshAt = 0;
                g_appliedWeaponSkin.statTrakModuleRefreshAttempts = 0;
            }

            char message[448]{};
            StringCchPrintfA(message, _countof(message),
                "Weapon skin material refresh: weapon=0x%llX "
                "getter=%s clear=%s attrs=%s skin=%s modules=%s/%s "
                "mesh=%s/%s/%s "
                "post=%s/%s pending=%s.",
                static_cast<unsigned long long>(weapon),
                getterReady ? "yes" : "no",
                materialsCleared ? "yes" : "no",
                attributesReapplied ? "yes" : "no",
                skinUpdated ? "yes" : "no",
                modulesRefreshed ? "yes" : "no",
                modulesChanged ? "changed" : "stable",
                weaponMeshUpdated ? "yes" : "no",
                viewmodelMeshUpdated ? "yes" : "no",
                childMeshUpdated ? "yes" : "no",
                weaponPostUpdated ? "yes" : "no",
                viewmodelPostUpdated ? "yes" : "no",
                g_appliedWeaponSkin.materialRefreshPending
                    ? "yes" : "no");
            if (refreshed ||
                g_appliedWeaponSkin.materialRefreshAttempts <= 1)
                AppendLog(message);
        }

        if (!earlyStage &&
            g_appliedWeaponSkin.statTrakModuleRefreshPending &&
            now >= g_appliedWeaponSkin.statTrakModuleRefreshAt) {
            ++g_appliedWeaponSkin.statTrakModuleRefreshAttempts;
            const bool attributesUpdated =
                SafeSetNetworkedItemViewStatTrakAttributes(
                    itemView, targetStatTrak);
            bool modulesChanged = false;
            const bool modulesRefreshed = attributesUpdated &&
                SafeRefreshWeaponModules(weapon, itemView, modulesChanged);
            if (modulesRefreshed) {
                g_appliedWeaponSkin.statTrakModuleRefreshPending = false;
                g_appliedWeaponSkin.statTrakModuleRefreshAt = 0;
                g_appliedWeaponSkin.statTrakModuleRefreshAttempts = 0;
                (void)SafePostDataUpdateSceneNode(weapon, selection);
            } else {
                const ULONGLONG retryDelay =
                    g_appliedWeaponSkin.statTrakModuleRefreshAttempts <=
                        kWeaponSkinFastStatTrakModuleRetryAttempts
                    ? kWeaponSkinStatTrakModuleRetryIntervalMs
                    : kWeaponSkinStatTrakModuleSlowRetryIntervalMs;
                g_appliedWeaponSkin.statTrakModuleRefreshAt =
                    now + retryDelay;
            }
            if (modulesRefreshed ||
                g_appliedWeaponSkin.statTrakModuleRefreshAttempts <= 1) {
                char message[224]{};
                StringCchPrintfA(message, _countof(message),
                    "Weapon StatTrak modules: count=%d attrs=%s "
                    "refresh=%s/%s pending=%s.",
                    targetStatTrak,
                    attributesUpdated ? "yes" : "no",
                    modulesRefreshed ? "yes" : "no",
                    modulesChanged ? "changed" : "stable",
                    g_appliedWeaponSkin.statTrakModuleRefreshPending
                        ? "yes" : "no");
                AppendLog(message);
            }
        }

        if (!earlyStage && g_appliedWeaponSkin.sceneUpdatePending &&
            now >= g_appliedWeaponSkin.sceneUpdateAt) {
            const uintptr_t viewmodel = ResolveHudViewModelForWeapon(
                entityList, pawn, weapon, stride, selection);
            const uintptr_t viewmodelSceneNode = ResolveHudWeaponSceneNode(
                entityList, pawn, weapon, stride, selection);
            const uint64_t meshGroupMask =
                WeaponMeshGroupMask(target->legacyModel);
            const bool weaponMeshUpdated = SafeSetMeshGroupMask(
                weapon, selection, meshGroupMask);
            const bool viewmodelMeshUpdated = viewmodel &&
                SafeSetMeshGroupMask(viewmodel, selection, meshGroupMask);
            const bool viewmodelSceneMeshUpdated = viewmodelSceneNode &&
                SafeSetMeshGroupMaskOnSceneNode(
                    viewmodelSceneNode, meshGroupMask);
            const bool weaponPostUpdated = SafePostDataUpdateSceneNode(
                weapon, selection);
            const bool viewmodelPostUpdated = viewmodel &&
                SafePostDataUpdateSceneNode(viewmodel, selection);
            const bool sceneReady = viewmodel && viewmodelSceneNode;
            g_appliedWeaponSkin.sceneUpdatePending = !sceneReady;
            if (sceneReady) {
                g_appliedWeaponSkin.sceneUpdateAt = 0;
                g_appliedWeaponSkin.sceneUpdateAttempts = 0;
            } else {
                ++g_appliedWeaponSkin.sceneUpdateAttempts;
                g_appliedWeaponSkin.sceneUpdateAt =
                    now + kWeaponSkinSceneRetryIntervalMs;
            }

            char message[256]{};
            StringCchPrintfA(message, _countof(message),
                "Weapon skin: phase=finalize weapon=0x%llX "
                "mesh_update=%s/%s/%s child=0x%llX "
                "post_update=%s/%s.",
                static_cast<unsigned long long>(weapon),
                weaponMeshUpdated ? "yes" : "no",
                viewmodelMeshUpdated ? "yes" : "no",
                viewmodelSceneMeshUpdated ? "yes" : "no",
                static_cast<unsigned long long>(viewmodelSceneNode),
                weaponPostUpdated ? "yes" : "no",
                viewmodelPostUpdated ? "yes" : "no");
            if (sceneReady || g_appliedWeaponSkin.sceneUpdateAttempts <= 1)
                AppendLog(message);
        }

        if (!g_appliedWeaponSkin.hudRefreshed &&
            now >= g_appliedWeaponSkin.hudRefreshAt) {
            ++g_appliedWeaponSkin.hudRefreshAttempts;
            g_appliedWeaponSkin.hudRefreshed = RefreshWeaponHudCache(
                itemView, "Weapon skin");
            if (!g_appliedWeaponSkin.hudRefreshed) {
                g_appliedWeaponSkin.hudRefreshAt =
                    now + kWeaponSkinHudRefreshDelayMs;
                if (g_appliedWeaponSkin.hudRefreshAttempts >= 3)
                    g_appliedWeaponSkin.hudRefreshed = true;
            }
        }
        g_appliedWeaponSkin.nextUpdateAt =
            now + kWeaponSkinStableCheckIntervalMs;
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
        static ObservedLoadout observedWeapons[2][54]{};
        static ObservedLoadout observedMusicKit{};
        static LONG observedEpoch = -1;
        static ULONGLONG nextPollAt = 0;

        const ULONGLONG now = GetTickCount64();
        const LONG currentEpoch = InterlockedCompareExchange(
            &g_nativeLoadoutObservationEpoch, 0, 0);
        if (observedEpoch != currentEpoch) {
            observed[0] = {};
            observed[1] = {};
            ZeroMemory(observedWeapons, sizeof(observedWeapons));
            observedMusicKit = {};
            observedEpoch = currentEpoch;
        }
        if (InterlockedCompareExchange(
                &g_inventoryCollectionSyncing, 0, 0) != 0 ||
            now < g_nativeLoadoutObserveAfter.load())
            return;
        if (now < nextPollAt) return;
        nextPollAt = now + 500;

        InventorySnapshotSelection selection;
        if (!ReadPublishedInventorySelection(selection) ||
            !selection.enabled || !selection.itemIdOffset) {
            observed[0] = {};
            observed[1] = {};
            ZeroMemory(observedWeapons, sizeof(observedWeapons));
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

        // Weapon slots are definition-specific. Observe the native loadout
        // instead of assigning every gun to one synthetic slot, and mirror
        // only selections the player already made through CS2.
        for (int teamIndex = 0; teamIndex < 2; ++teamIndex) {
            const int gameTeam = gameTeams[teamIndex];
            const int protocolTeam = teamIndex + 1;
            for (int slot = 1; slot <= 53; ++slot) {
                if (slot == kGloveLoadoutSlot) continue;
                InventorySocacheLoadoutSelection nativeSelection{};
                if (!ReadInventorySocacheLoadoutSelection(gameTeam, slot,
                        selection.itemIdOffset, nativeSelection))
                    continue;

                ObservedLoadout& weaponObservation =
                    observedWeapons[teamIndex][slot];
                if (!weaponObservation.initialized) {
                    weaponObservation.initialized = true;
                    weaponObservation.itemId = nativeSelection.itemId;
                    continue;
                }
                if (weaponObservation.itemId == nativeSelection.itemId)
                    continue;
                const uint64_t previousItemId = weaponObservation.itemId;
                weaponObservation.itemId = nativeSelection.itemId;
                if (!nativeSelection.generated ||
                    nativeSelection.localId == 0)
                    continue;

                const InventorySnapshotSelection::WeaponSkinCollectionItem*
                    weaponItem = nullptr;
                for (int itemIndex = 0;
                    itemIndex < selection.weaponSkinItemCount; ++itemIndex) {
                    if (selection.weaponSkinItems[itemIndex].localId ==
                        nativeSelection.localId) {
                        weaponItem = &selection.weaponSkinItems[itemIndex];
                        break;
                    }
                }
                if (!weaponItem ||
                    (GetWeaponDefinitionProtocolTeam(
                        weaponItem->definitionIndex) != 3 &&
                        GetWeaponDefinitionProtocolTeam(
                            weaponItem->definitionIndex) != protocolTeam) ||
                    (weaponItem->equippedTeam & protocolTeam) != 0)
                    continue;

                NativeLoadoutCommand command{};
                command.type = NativeLoadoutCommandType::Equip;
                command.localId = nativeSelection.localId;
                command.team = protocolTeam;
                QueueNativeLoadoutCommand(command);
                char message[224]{};
                StringCchPrintfA(message, _countof(message),
                    "Native weapon loadout: equip queued team=%d slot=%d "
                    "local=%llu previous=%llu current=%llu.",
                    protocolTeam, slot,
                    static_cast<unsigned long long>(command.localId),
                    static_cast<unsigned long long>(previousItemId),
                    static_cast<unsigned long long>(nativeSelection.itemId));
                AppendLog(message);
                return;
            }
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
            const bool visualDrifted = !finishReadable ||
                currentPaintKit != g_appliedKnife.targetPaintKit ||
                currentSeed != g_appliedKnife.targetSeed ||
                wearDelta > 0.000001f || wearDelta < -0.000001f;
            const bool finishDrifted = visualDrifted ||
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
            if (visualDrifted) {
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
        if (!getName) return;

        const char* eventName = getName(event);
        if (eventName && (strcmp(eventName, "round_prestart") == 0 ||
            strcmp(eventName, "round_start") == 0)) {
            const uint32_t generation =
                g_weaponSkinLifecycleGeneration.fetch_add(1) + 1;
            char message[160]{};
            StringCchPrintfA(message, _countof(message),
                "Weapon skin lifecycle: event=%s generation=%u.",
                eventName, generation);
            AppendLog(message);
        }
        if (!eventName || (strcmp(eventName, "player_death") != 0 &&
            strcmp(eventName, "round_mvp") != 0))
            return;
        if (!getControllerId) return;
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

        if (!getString) return;
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
        const char* weaponItemId = getString(
            event, weaponItemIdToken, "0");
        const char* weaponFauxItemId = getString(
            event, weaponFauxItemIdToken, "0");

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
            uint64_t eventItemId = 0;
            if (weaponItemId && *weaponItemId) {
                char* parseEnd = nullptr;
                eventItemId = strtoull(weaponItemId, &parseEnd, 10);
                if (parseEnd == weaponItemId) eventItemId = 0;
            }
            uint64_t weaponLocalId = 0;
            const char* resolution = "none";
            if (eventItemId && ResolveInventorySocacheGeneratedLocalId(
                    eventItemId, weaponLocalId)) {
                resolution = "itemid";
            } else if (g_appliedWeaponSkin.applied && weaponName) {
                const char* expectedWeaponName = GetWeaponKillEventName(
                    g_appliedWeaponSkin.definitionIndex);
                if (expectedWeaponName &&
                    strcmp(weaponName, expectedWeaponName) == 0) {
                    weaponLocalId = g_appliedWeaponSkin.localId;
                    resolution = "active-definition";
                }
            }

            const auto* weaponItem = FindWeaponSkinCollectionItem(
                selection, weaponLocalId);
            const int protocolTeam = localTeam == kTerroristTeam ? 1 : 2;
            if (weaponItem && weaponItem->statTrak &&
                (weaponItem->equippedTeam & protocolTeam) != 0) {
                QueueStatTrakIncrement(
                    weaponItem->localId, weaponItem->statTrakCount);
                char message[256]{};
                StringCchPrintfA(message, _countof(message),
                    "StatTrak event: weapon kill local=%llu def=%d "
                    "count=%d weapon=%s itemid=%llu resolution=%s.",
                    static_cast<unsigned long long>(weaponItem->localId),
                    weaponItem->definitionIndex,
                    weaponItem->statTrakCount,
                    weaponName ? weaponName : "",
                    static_cast<unsigned long long>(eventItemId),
                    resolution);
                AppendLog(message);
            } else if (g_weaponStatTrakEventLogs.fetch_add(1) < 16) {
                char message[256]{};
                StringCchPrintfA(message, _countof(message),
                    "StatTrak event probe: weapon=%s itemid=%llu "
                    "local=%llu resolved=%s configured=%s team_ok=%s.",
                    weaponName ? weaponName : "",
                    static_cast<unsigned long long>(eventItemId),
                    static_cast<unsigned long long>(weaponLocalId),
                    resolution,
                    weaponItem ? "yes" : "no",
                    weaponItem &&
                        (weaponItem->equippedTeam & protocolTeam) != 0
                        ? "yes" : "no");
                AppendLog(message);
            }
        }

        if (!g_appliedKnife.subclassApplied || !setString || !weaponName ||
            (strcmp(weaponName, "knife") != 0 &&
                strcmp(weaponName, "knife_t") != 0))
            return;
        const KnifeModel* appliedKnife = FindKnifeModel(
            g_appliedKnife.definitionIndex);
        if (!appliedKnife || !appliedKnife->eventName) return;

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
        const bool roundPrestartAdded = addListener(g_eventManager,
            &g_inventoryEventListener, "round_prestart", false);
        const bool roundStartAdded = addListener(g_eventManager,
            &g_inventoryEventListener, "round_start", false);
        if (!deathFound) {
            if (deathAdded || mvpAdded || roundPrestartAdded || roundStartAdded)
                removeListener(g_eventManager, &g_inventoryEventListener);
            g_eventManager = nullptr;
            AppendLog("Killfeed events: listener no instalado.");
            return false;
        }
        g_killFeedListenerInstalled = true;
        char listenerMessage[224]{};
        StringCchPrintfA(listenerMessage, _countof(listenerMessage),
            "Inventory events: death=yes mvp=%s round_prestart=%s "
            "round_start=%s.",
            mvpFound ? "yes" : "pre-hook",
            roundPrestartAdded ? "yes" : "pre-hook",
            roundStartAdded ? "yes" : "pre-hook");
        AppendLog(listenerMessage);

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
            "Source2EngineToClient001: OK", "Source2EngineToClient001: FAIL",
            nullptr);
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
                        InventorySnapshotSelection selection{};
                        if (ReadPublishedInventorySelection(selection) &&
                            UsesNativeTeamLoadoutObserver(selection, localId)) {
                            panoramaCommand = {};
                            AppendLog("Panorama native equip: arma/cuchillo "
                                "delegado al observador por equipo.");
                        } else {
                            panoramaCommand.type =
                                PanoramaInventoryCommandType::Equip;
                            panoramaCommand.localId = localId;
                            AppendLog("Panorama native equip: Item ID "
                                "resuelto a Local ID.");
                        }
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
            InterlockedCompareExchange(&g_weaponSkinApplied, 0, 0) != 0 ||
            InterlockedCompareExchange(&g_knifeModelApplied, 0, 0) != 0 ||
            InterlockedCompareExchange(&g_gloveModelApplied, 0, 0) != 0) &&
            GetTickCount64() < restoreDeadline)
            Sleep(10);
        RequestPanoramaProbeDestroy();
        const ULONGLONG panoramaDestroyDeadline = GetTickCount64() + 1000;
        while (IsPanoramaProbeMounted() &&
            GetTickCount64() < panoramaDestroyDeadline)
            Sleep(10);
        RemoveWeaponMaterialDiagnostics();
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
