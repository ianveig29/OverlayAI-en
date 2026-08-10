#include "InventorySocache.h"

#include "BridgeLogging.h"

#include <windows.h>
#include <strsafe.h>

#include <cstdint>

namespace {
    struct PatternResult {
        DWORD count = 0;
        uintptr_t first = 0;
    };

    InventorySocacheDiagnostics g_diagnostics{};

    struct SOID {
        uint64_t id = 0;
        uint32_t type = 0;
        uint32_t padding = 0;
    };

    using CreateEconItemFn = void* (__cdecl*)();
    using AllocateAttributeFn = void* (__fastcall*)(void*);
    using GetEconItemSystemFn = void* (__fastcall*)(void*);
    using GetAttributeDefinitionFn = void* (__fastcall*)(void*, int);
    using SetDynamicAttributeValueFn = __int64(__fastcall*)(
        void*, void*, void*);
    using AddObjectFn = bool(__fastcall*)(void*, void*);
    using RemoveObjectFn = void* (__fastcall*)(void*, void*);
    using SOEventFn = void(__fastcall*)(void*, SOID, void*, int);
    using DestructEconItemFn = void(__fastcall*)(void*, bool);
    using EquipItemInLoadoutFn = bool(__fastcall*)(
        void*, int, int, uint64_t);
    using GetItemInLoadoutFn = void* (__fastcall*)(void*, int, int);
    using CopyEconItemViewFn = void* (__fastcall*)(void*, const void*);
    using ClearEconItemViewFn = void(__fastcall*)(void*);
    using GetEconItemStaticDataFn = void* (__fastcall*)(void*);
    using RejectEconItemViewFn = bool(__fastcall*)(void*);
    using GetEconItemPaintKitFn = int(__fastcall*)(void*);
    using GetEconItemPaintDataFn = void* (__fastcall*)(void*);

    CreateEconItemFn g_createEconItem = nullptr;
    AllocateAttributeFn g_allocateAttribute = nullptr;
    SetDynamicAttributeValueFn g_setDynamicAttributeValue = nullptr;
    void* g_itemSchema = nullptr;
    EquipItemInLoadoutFn g_equipItemInLoadout = nullptr;
    CopyEconItemViewFn g_copyEconItemView = nullptr;
    ClearEconItemViewFn g_clearEconItemView = nullptr;
    GetEconItemStaticDataFn g_getEconItemStaticData = nullptr;
    RejectEconItemViewFn g_rejectEconItemView = nullptr;
    GetEconItemPaintDataFn g_getEconItemPaintData = nullptr;
    struct GeneratedItemState {
        void* item = nullptr;
        uint64_t localId = 0;
        uint64_t itemId = 0;
        uint32_t accountId = 0;
        int definition = 0;
        int musicKitId = 0;
        int paintKit = 0;
        int seed = 0;
        float wear = 0.0f;
        bool statTrak = false;
        int statTrakCount = 0;
        int statTrakType = 0;
        int variantAttributeDefinition = 0;
        uint32_t variantAttributeValue = 0;
        uint8_t quality = 0;
        uint8_t rarity = 0;
        bool unacknowledged = false;
        int loadoutSlot = -1;
        uintptr_t itemViewItemIdOffset = 0;
    };

    struct LoadoutOverrideState {
        bool active = false;
        int team = 0;
        int loadoutSlot = -1;
        uintptr_t itemViewItemIdOffset = 0;
        uint64_t originalItemId = 0;
        uint64_t currentItemId = 0;
    };

    constexpr int kMaxGeneratedItems = 256;
    constexpr int kMaxLoadoutOverrides = 8;
    GeneratedItemState g_generatedItems[kMaxGeneratedItems]{};
    LoadoutOverrideState g_loadoutOverrides[kMaxLoadoutOverrides]{};

    bool IsReadable(const void* address, SIZE_T size) noexcept {
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
        const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) +
            info.RegionSize;
        return start <= end && size <= end - start;
    }

    bool IsExecutable(const void* address) noexcept {
        MEMORY_BASIC_INFORMATION info{};
        if (!address || !VirtualQuery(address, &info, sizeof(info)) ||
            info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (info.Protect & executable) != 0;
    }

    template <typename T>
    bool Read(uintptr_t address, T& value) noexcept {
        if (!IsReadable(reinterpret_cast<const void*>(address), sizeof(T)))
            return false;
        CopyMemory(&value, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    }

    bool IsWritable(void* address, SIZE_T size) noexcept {
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
        const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) +
            info.RegionSize;
        return start <= end && size <= end - start;
    }

    template <typename T>
    bool Write(uintptr_t address, const T& value) noexcept {
        if (!IsWritable(reinterpret_cast<void*>(address), sizeof(T)))
            return false;
        CopyMemory(reinterpret_cast<void*>(address), &value, sizeof(T));
        return true;
    }

    template <typename Function>
    Function GetVirtualFunction(void* object, SIZE_T index) noexcept {
        uintptr_t vtable = 0;
        uintptr_t function = 0;
        if (!object || !Read(reinterpret_cast<uintptr_t>(object), vtable) ||
            !vtable || !Read(vtable + index * sizeof(uintptr_t), function) ||
            !IsExecutable(reinterpret_cast<const void*>(function)))
            return nullptr;
        return reinterpret_cast<Function>(function);
    }

    bool CreateEconItem(void*& item) noexcept {
        item = nullptr;
        if (!g_createEconItem) return false;
        __try {
            item = g_createEconItem();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            item = nullptr;
        }
        return item != nullptr && IsWritable(item, 0x48);
    }

    void* AllocateCompactAttribute(void* item) noexcept {
        if (!item || !g_allocateAttribute) return nullptr;
        __try {
            return g_allocateAttribute(item);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    bool AddCompactAttribute(
        void* item, uint16_t definitionIndex, uint32_t rawValue) noexcept {
        void* slot = AllocateCompactAttribute(item);
        if (!slot || !IsWritable(slot, 16)) return false;
        unsigned char empty[16]{};
        CopyMemory(slot, empty, sizeof(empty));
        return Write(reinterpret_cast<uintptr_t>(slot), definitionIndex) &&
            Write(reinterpret_cast<uintptr_t>(slot) + 8, rawValue);
    }

    bool AddEconAttribute(
        void* item, int definitionIndex, void* value,
        uint32_t compactValue) noexcept {
        if (item && g_itemSchema && g_setDynamicAttributeValue) {
            const auto getDefinition =
                GetVirtualFunction<GetAttributeDefinitionFn>(
                    g_itemSchema, 27);
            if (getDefinition) {
                void* definition = nullptr;
                __try {
                    definition = getDefinition(
                        g_itemSchema, definitionIndex);
                    if (definition) {
                        (void)g_setDynamicAttributeValue(
                            item, definition, value);
                        return true;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    AppendLog("SOCache item attribute: native setter faulted.");
                    return false;
                }
            }
        }
        return AddCompactAttribute(item,
            static_cast<uint16_t>(definitionIndex), compactValue);
    }

    uint32_t FloatBits(float value) noexcept {
        uint32_t bits = 0;
        CopyMemory(&bits, &value, sizeof(bits));
        return bits;
    }

    PatternResult ScanExecutableSections(
        HMODULE module, const unsigned char* bytes, const char* mask) noexcept {
        PatternResult result;
        if (!module || !bytes || !mask) return result;
        const SIZE_T length = static_cast<SIZE_T>(lstrlenA(mask));
        if (length == 0) return result;

        const auto* base = reinterpret_cast<const unsigned char*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!IsReadable(dos, sizeof(*dos)) ||
            dos->e_magic != IMAGE_DOS_SIGNATURE)
            return result;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (!IsReadable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE)
            return result;

        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (WORD sectionIndex = 0;
            sectionIndex < nt->FileHeader.NumberOfSections;
            ++sectionIndex, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const SIZE_T sectionSize = section->Misc.VirtualSize;
            if (sectionSize < length) continue;
            const unsigned char* start = base + section->VirtualAddress;
            if (!IsReadable(start, sectionSize)) continue;
            for (SIZE_T offset = 0; offset <= sectionSize - length; ++offset) {
                bool matches = true;
                for (SIZE_T index = 0; index < length; ++index) {
                    if (mask[index] == 'x' &&
                        start[offset + index] != bytes[index]) {
                        matches = false;
                        break;
                    }
                }
                if (!matches) continue;
                ++result.count;
                if (!result.first)
                    result.first = reinterpret_cast<uintptr_t>(start + offset);
            }
        }
        return result;
    }

    bool ResolveItemTypeCache(
        uintptr_t socache, uintptr_t& itemTypeCache) noexcept {
        itemTypeCache = 0;
        int cacheCount = 0;
        uintptr_t caches = 0;
        if (!Read(socache + 0x10, cacheCount) || cacheCount <= 0 ||
            cacheCount > 64 || !Read(socache + 0x18, caches) || !caches ||
            !IsReadable(reinterpret_cast<const void*>(caches),
                static_cast<SIZE_T>(cacheCount) * sizeof(uintptr_t)))
            return false;

        for (int index = 0; index < cacheCount; ++index) {
            uintptr_t cache = 0;
            int classId = 0;
            if (!Read(caches + static_cast<uintptr_t>(index) *
                    sizeof(uintptr_t), cache) || !cache ||
                !Read(cache + 0x78, classId))
                continue;
            if (classId == 1) {
                itemTypeCache = cache;
                return true;
            }
        }
        return false;
    }

    bool ValidateInventoryCandidate(
        uintptr_t inventory, uintptr_t& socache,
        uintptr_t& itemTypeCache, int& itemCount) noexcept {
        socache = 0;
        itemTypeCache = 0;
        itemCount = 0;
        uintptr_t vtable = 0;
        uintptr_t firstFunction = 0;
        uint32_t ownerType = 0;
        if (!inventory || !Read(inventory, vtable) || !vtable ||
            !Read(vtable, firstFunction) || !IsExecutable(
                reinterpret_cast<const void*>(firstFunction)) ||
            !Read(inventory + 0x18, ownerType) || ownerType != 1 ||
            !Read(inventory + 0x68, socache) || !socache ||
            !ResolveItemTypeCache(socache, itemTypeCache))
            return false;

        uintptr_t itemData = 0;
        if (!Read(itemTypeCache + 0x8, itemCount) || itemCount < 0 ||
            itemCount > 8192 || !Read(itemTypeCache + 0x10, itemData))
            return false;
        return itemCount == 0 || (itemData && IsReadable(
            reinterpret_cast<const void*>(itemData),
            static_cast<SIZE_T>(itemCount) * sizeof(uintptr_t)));
    }

    bool ResolveInventoryFromManager(
        uintptr_t manager, uintptr_t& inventory, uintptr_t& socache,
        uintptr_t& itemTypeCache, int& itemCount) noexcept {
        inventory = 0;
        uintptr_t vtable = 0;
        if (!Read(manager, vtable) || !vtable) return false;

        // Detect the trivial inventory member accessor instead of pinning its
        // virtual slot, which already moved between two consecutive builds.
        for (SIZE_T index = 48; index < 96; ++index) {
            uintptr_t function = 0;
            unsigned char code[8]{};
            if (!Read(vtable + index * sizeof(uintptr_t), function) ||
                !IsExecutable(reinterpret_cast<const void*>(function)) ||
                !IsReadable(reinterpret_cast<const void*>(function),
                    sizeof(code)))
                continue;
            CopyMemory(code, reinterpret_cast<const void*>(function),
                sizeof(code));
            if (code[0] != 0x48 || code[1] != 0x8B || code[2] != 0x81 ||
                code[7] != 0xC3)
                continue;
            int32_t memberOffset = 0;
            CopyMemory(&memberOffset, code + 3, sizeof(memberOffset));
            if (memberOffset <= 0 || memberOffset > 0x100000)
                continue;
            uintptr_t candidate = 0;
            if (!Read(manager + static_cast<uintptr_t>(memberOffset),
                    candidate) || !candidate)
                continue;
            if (ValidateInventoryCandidate(candidate, socache,
                    itemTypeCache, itemCount)) {
                inventory = candidate;
                return true;
            }
        }
        return false;
    }

    bool IsItemInTypeCache(void* item) noexcept {
        if (!item || !g_diagnostics.itemTypeCache) return false;
        int count = 0;
        uintptr_t data = 0;
        if (!Read(g_diagnostics.itemTypeCache + 0x8, count) || count < 0 ||
            count > 8192 || !Read(g_diagnostics.itemTypeCache + 0x10, data) ||
            (count > 0 && !data))
            return false;
        for (int index = 0; index < count; ++index) {
            uintptr_t current = 0;
            if (Read(data + static_cast<uintptr_t>(index) *
                    sizeof(uintptr_t), current) &&
                current == reinterpret_cast<uintptr_t>(item))
                return true;
        }
        return false;
    }

    bool InvokeAddObject(void* item) noexcept {
        AddObjectFn function = GetVirtualFunction<AddObjectFn>(
            reinterpret_cast<void*>(g_diagnostics.itemTypeCache), 1);
        if (!function) return false;
        bool result = false;
        __try {
            result = function(reinterpret_cast<void*>(
                g_diagnostics.itemTypeCache), item);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            result = false;
        }
        return result;
    }

    bool InvokeRemoveObject(void* item) noexcept {
        RemoveObjectFn function = GetVirtualFunction<RemoveObjectFn>(
            reinterpret_cast<void*>(g_diagnostics.itemTypeCache), 3);
        if (!function) return false;
        __try {
            (void)function(reinterpret_cast<void*>(
                g_diagnostics.itemTypeCache), item);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return true;
    }

    bool InvokeInventoryEvent(
        SIZE_T virtualIndex, void* item) noexcept {
        SOEventFn function = GetVirtualFunction<SOEventFn>(
            reinterpret_cast<void*>(g_diagnostics.inventory), virtualIndex);
        SOID owner{};
        if (!function || !Read(g_diagnostics.inventory + 0x10, owner))
            return false;
        __try {
            function(reinterpret_cast<void*>(g_diagnostics.inventory), owner,
                item, 4);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return true;
    }

    bool IsValidLoadoutContext(int team, int loadoutSlot) noexcept {
        if (loadoutSlot < 0 || loadoutSlot > 63) return false;
        // Music Kit, flair and spray are all-character slots. Source 2
        // addresses those through the global loadout context instead of T/CT.
        if (loadoutSlot >= 54 && loadoutSlot <= 56) return team == 0;
        return team >= 2 && team <= 3;
    }

    void* GetLoadoutItemView(int team, int loadoutSlot) noexcept {
        if (!IsValidLoadoutContext(team, loadoutSlot))
            return nullptr;
        GetItemInLoadoutFn function = GetVirtualFunction<GetItemInLoadoutFn>(
            reinterpret_cast<void*>(g_diagnostics.inventory), 8);
        if (!function) return nullptr;
        void* itemView = nullptr;
        __try {
            itemView = function(reinterpret_cast<void*>(
                g_diagnostics.inventory), team, loadoutSlot);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            itemView = nullptr;
        }
        return itemView;
    }

    bool ReadLoadoutItemId(
        int team, int loadoutSlot, uintptr_t itemIdOffset, uint64_t& itemId,
        uintptr_t* itemViewAddress = nullptr) noexcept {
        itemId = 0;
        if (itemViewAddress) *itemViewAddress = 0;
        if (!IsValidLoadoutContext(team, loadoutSlot) || itemIdOffset == 0 ||
            itemIdOffset > 0x1000)
            return false;
        void* itemView = GetLoadoutItemView(team, loadoutSlot);
        if (itemViewAddress)
            *itemViewAddress = reinterpret_cast<uintptr_t>(itemView);
        return itemView && Read(reinterpret_cast<uintptr_t>(itemView) +
            itemIdOffset, itemId);
    }

    bool EquipLoadoutItem(
        int team, int loadoutSlot, uint64_t itemId) noexcept {
        if (!g_equipItemInLoadout ||
            !IsValidLoadoutContext(team, loadoutSlot) ||
            !g_diagnostics.manager)
            return false;
        bool equipped = false;
        __try {
            equipped = g_equipItemInLoadout(reinterpret_cast<void*>(
                g_diagnostics.manager), team, loadoutSlot, itemId);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            equipped = false;
        }
        return equipped;
    }

    GeneratedItemState* FindGeneratedItem(uint64_t localId) noexcept {
        for (GeneratedItemState& state : g_generatedItems) {
            if (state.item && state.localId == localId)
                return &state;
        }
        return nullptr;
    }

    GeneratedItemState* FindGeneratedItemByItemId(uint64_t itemId) noexcept {
        for (GeneratedItemState& state : g_generatedItems) {
            if (state.item && state.itemId == itemId)
                return &state;
        }
        return nullptr;
    }

    GeneratedItemState* FindFreeGeneratedItem() noexcept {
        for (GeneratedItemState& state : g_generatedItems) {
            if (!state.item) return &state;
        }
        return nullptr;
    }

    LoadoutOverrideState* FindLoadoutOverride(
        int team, int loadoutSlot) noexcept {
        for (LoadoutOverrideState& state : g_loadoutOverrides) {
            if (state.active && state.team == team &&
                state.loadoutSlot == loadoutSlot)
                return &state;
        }
        return nullptr;
    }

    LoadoutOverrideState* FindFreeLoadoutOverride() noexcept {
        for (LoadoutOverrideState& state : g_loadoutOverrides) {
            if (!state.active) return &state;
        }
        return nullptr;
    }

    bool DestructEconItem(void* item) noexcept {
        DestructEconItemFn function =
            GetVirtualFunction<DestructEconItemFn>(item, 1);
        if (!function) return false;
        __try {
            function(item, true);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return true;
    }

    bool ResolveNextItemIdentity(
        uint64_t localId, bool unacknowledged, uint64_t& itemId,
        uint32_t& inventoryPosition) noexcept {
        itemId = 0;
        inventoryPosition = 0;
        int count = 0;
        uintptr_t data = 0;
        if (!Read(g_diagnostics.itemTypeCache + 0x8, count) || count < 0 ||
            count > 8192 || !Read(g_diagnostics.itemTypeCache + 0x10, data))
            return false;

        uint64_t highestItemId = 0;
        for (int index = 0; index < count; ++index) {
            uintptr_t item = 0;
            uint64_t currentId = 0;
            if (!Read(data + static_cast<uintptr_t>(index) *
                    sizeof(uintptr_t), item) || !item ||
                !Read(item + 0x10, currentId))
                continue;
            if ((currentId & 0xF000000000000000ull) == 0)
                highestItemId = currentId > highestItemId
                    ? currentId : highestItemId;
        }
        itemId = highestItemId + 1;
        if (itemId == 0) itemId = 1;

        // A zero ItemIDHigh is accepted by the loadout/HUD path but the
        // material resolver can treat it as a non-SOC fallback item. Keep
        // generated objects in a valid synthetic 64-bit range when the
        // account has no genuine inventory objects to provide a higher ID.
        constexpr uint64_t kSyntheticItemIdBase = 1ull << 32;
        if (itemId < kSyntheticItemIdBase) {
            itemId = kSyntheticItemIdBase + localId;
            if (itemId < kSyntheticItemIdBase)
                itemId = kSyntheticItemIdBase;
        }
        constexpr uint32_t kUnacknowledgedFlag = 1u << 30;
        uint32_t stablePosition = static_cast<uint32_t>(
            localId & (kUnacknowledgedFlag - 1));
        if (stablePosition == 0) stablePosition = 1;
        // CS2 uses the exact high-bit sentinel while an item is pending. The
        // native unacknowledged-item list ignores values that also contain a
        // normal inventory position in the low bits.
        inventoryPosition = unacknowledged
            ? kUnacknowledgedFlag : stablePosition;
        return true;
    }

    void LogDiagnostics(
        HMODULE client, const PatternResult& managerPattern,
        const PatternResult& econFactory, const PatternResult& setAttribute,
        const PatternResult& cacheCaller) noexcept {
        char message[640]{};
        const uintptr_t base = reinterpret_cast<uintptr_t>(client);
        StringCchPrintfA(message, _countof(message),
            "SOCache diagnostic: manager=%s inventory=%s socache=%s "
            "type1=%s items=%d manager_rva=0x%llX inventory=0x%llX "
            "socache=0x%llX typecache=0x%llX factory=%lu legacy_attrs=%lu "
            "compact_attrs=%s write=%s cache_caller=%lu "
            "client_econ_rva=0x%llX.",
            g_diagnostics.managerResolved ? "yes" : "no",
            g_diagnostics.inventoryResolved ? "yes" : "no",
            g_diagnostics.socacheResolved ? "yes" : "no",
            g_diagnostics.itemTypeCacheResolved ? "yes" : "no",
            g_diagnostics.itemCount,
            static_cast<unsigned long long>(managerPattern.first
                ? managerPattern.first - base : 0),
            static_cast<unsigned long long>(g_diagnostics.inventory),
            static_cast<unsigned long long>(g_diagnostics.socache),
            static_cast<unsigned long long>(g_diagnostics.itemTypeCache),
            static_cast<unsigned long>(econFactory.count),
            static_cast<unsigned long>(setAttribute.count),
            g_diagnostics.attributeSettersResolved ? "yes" : "no",
            g_diagnostics.writeReady ? "ready" : "blocked",
            static_cast<unsigned long>(cacheCaller.count),
            static_cast<unsigned long long>(
                g_diagnostics.clientEconAccessor && base
                    ? g_diagnostics.clientEconAccessor - base : 0));
        AppendLog(message);
    }
}

bool InitializeInventorySocacheDiagnostics(void* clientInterface) noexcept {
    g_diagnostics = {};
    HMODULE client = GetModuleHandleW(L"client.dll");
    if (!client) {
        AppendLog("SOCache diagnostic: client.dll no disponible.");
        return false;
    }

    constexpr unsigned char managerBytes[] = {
        0x48, 0x8D, 0x05, 0, 0, 0, 0, 0xC3,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
        0x8B, 0x91, 0, 0, 0, 0, 0xB8
    };
    constexpr unsigned char econFactoryBytes[] = {
        0x48, 0x83, 0xEC, 0x28, 0xB9, 0x48, 0, 0, 0,
        0xE8, 0, 0, 0, 0, 0x48, 0x85
    };
    constexpr unsigned char setAttributeBytes[] = {
        0x48, 0x89, 0x6C, 0x24, 0x00, 0x57, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x81, 0xEC, 0x00, 0x00, 0x00,
        0x00, 0x48, 0x8B, 0xFA, 0xC7, 0x44, 0x24, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x4D, 0x8B, 0xF8, 0x4C,
        0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B,
        0xE9, 0x4C, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x33, 0xD2, 0x48, 0x8B, 0x4F, 0x00, 0xE8, 0x00,
        0x00, 0x00, 0x00, 0x4C, 0x8B, 0xF0, 0x48, 0x85,
        0xC0, 0x0F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x48,
        0x8B, 0x55, 0x00, 0x48, 0x89, 0x9C, 0x24, 0x00,
        0x00, 0x00, 0x00, 0x0F, 0x29, 0xB4, 0x24, 0x00,
        0x00, 0x00, 0x00, 0x48, 0x89, 0xB4, 0x24, 0x00,
        0x00, 0x00, 0x00, 0x48, 0x85, 0xD2, 0x74, 0x00,
        0x0F, 0xB7, 0x4A, 0x00, 0x48, 0x8D, 0x5A, 0x00,
        0x0F, 0xB6, 0xF1, 0x48, 0xC1, 0xE6, 0x00, 0x48,
        0x03, 0xF3, 0x48, 0x3B, 0xDE, 0x73, 0x00, 0x90,
        0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x74, 0x00, 0x48, 0x83, 0xC3,
        0x00, 0x48, 0x3B, 0xDE, 0x72, 0x00, 0xEB, 0x00,
        0x48, 0x85, 0xDB, 0x75, 0x00, 0x48, 0x8B, 0xCD,
        0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD8
    };
    constexpr unsigned char allocateAttributeBytes[] = {
        0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x48, 0x8B, 0x79, 0x20, 0x48, 0x8B,
        0xF1, 0x48, 0x85, 0xFF, 0x75, 0x32, 0x48, 0x8B, 0x05
    };
    constexpr unsigned char cacheCallerBytes[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
        0x49, 0x68, 0xBB, 0xE8, 0x03, 0, 0, 0x48, 0x85,
        0xC9, 0x74, 0x34, 0xBA, 0x07, 0, 0, 0, 0xE8, 0, 0, 0, 0
    };
    constexpr unsigned char equipItemBytes[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x89,
        0x54, 0x24, 0x10, 0x57, 0x41, 0x54, 0x41, 0x55,
        0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0,
        0x0F, 0xB7, 0
    };
    constexpr unsigned char copyItemViewBytes[] = {
        0x40, 0x53, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x48,
        0x89, 0x6C, 0x24, 0x58, 0x48, 0x8B, 0xFA, 0x48,
        0x89, 0x74, 0x24, 0x60, 0x48, 0x8B, 0xD9, 0xE8,
        0, 0, 0, 0, 0x0F, 0xB7, 0x87, 0xBA, 0x01, 0, 0
    };
    constexpr unsigned char clearItemViewBytes[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x80, 0xB9, 0xE8, 0x01, 0, 0,
        0, 0x48, 0x8B, 0xD9, 0x74, 0x07, 0xC6, 0x81,
        0xE8, 0x01, 0, 0, 0
    };
    constexpr unsigned char getStaticDataBytes[] = {
        0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89,
        0x5C, 0x24, 0x30, 0x48, 0x8B, 0xF1, 0x48, 0x8B,
        0x1D, 0, 0, 0, 0, 0x48, 0x85, 0xDB, 0x75, 0x60
    };
    constexpr unsigned char rejectItemViewBytes[] = {
        0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x01, 0xFF,
        0x90, 0x80, 0, 0, 0, 0x48, 0x85, 0xC0,
        0x74, 0x0E, 0x48, 0x8B, 0x10, 0x48, 0x8B, 0xC8,
        0x48, 0x83, 0xC4, 0x28, 0x48, 0xFF, 0x62, 0x78
    };
    constexpr unsigned char gloveReapplyCallsiteBytes[] = {
        0x45, 0x38, 0xAE, 0xE8, 0x01, 0, 0, 0x0F,
        0x84, 0, 0, 0, 0, 0x49, 0x8B, 0xCE,
        0xE8, 0, 0, 0, 0, 0x84, 0xC0, 0x0F,
        0x85, 0, 0, 0, 0, 0x49, 0x8B, 0xCE,
        0xE8, 0, 0, 0, 0, 0x48, 0x8B, 0xD0
    };

    const PatternResult managerPattern = ScanExecutableSections(
        client, managerBytes, "xxx????xxxxxxxxxxx????x");
    const PatternResult econFactory = ScanExecutableSections(
        client, econFactoryBytes, "xxxxxxxxxx????xx");
    const PatternResult setAttribute = ScanExecutableSections(
        client, setAttributeBytes,
        "xxxx?xxxxxxxx????xxxxxx?????xxxxxx????xxxxxx????xxxxx?x????"
        "xxxxxxxx????xxx?xxxx????xxxx????xxxx????xxxx?xxx?xxx?xxxxxx?"
        "xxxxxxx?x???xxx?????x?xxx?xxxx?x?xxxx?xxxx????xxx");
    const PatternResult allocateAttribute = ScanExecutableSections(
        client, allocateAttributeBytes, "xxxxxxxxxxxxxxxxxxxxxxxxx");
    const PatternResult cacheCaller = ScanExecutableSections(
        client, cacheCallerBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxx????");
    const PatternResult equipItem = ScanExecutableSections(
        client, equipItemBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?xx?");
    const PatternResult copyItemView = ScanExecutableSections(
        client, copyItemViewBytes, "xxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxx");
    const PatternResult clearItemView = ScanExecutableSections(
        client, clearItemViewBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    const PatternResult getStaticData = ScanExecutableSections(
        client, getStaticDataBytes, "xxxxxxxxxxxxxxxxx????xxxxx");
    const PatternResult rejectItemView = ScanExecutableSections(
        client, rejectItemViewBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    const PatternResult gloveReapplyCallsite = ScanExecutableSections(
        client, gloveReapplyCallsiteBytes,
        "xxxxxxxxx????xxxx????xxxx????xxxx????xxx");

    g_diagnostics.econItemFactoryResolved = econFactory.count == 1;
    g_createEconItem = econFactory.count == 1
        ? reinterpret_cast<CreateEconItemFn>(econFactory.first) : nullptr;
    g_allocateAttribute = allocateAttribute.count == 1
        ? reinterpret_cast<AllocateAttributeFn>(allocateAttribute.first)
        : nullptr;
    g_setDynamicAttributeValue = setAttribute.count == 1
        ? reinterpret_cast<SetDynamicAttributeValueFn>(setAttribute.first)
        : nullptr;
    g_equipItemInLoadout = equipItem.count == 1
        ? reinterpret_cast<EquipItemInLoadoutFn>(equipItem.first) : nullptr;
    g_copyEconItemView = copyItemView.count == 1
        ? reinterpret_cast<CopyEconItemViewFn>(copyItemView.first) : nullptr;
    g_clearEconItemView = clearItemView.count == 1
        ? reinterpret_cast<ClearEconItemViewFn>(clearItemView.first) : nullptr;
    uintptr_t getStaticDataAddress = getStaticData.count == 1
        ? getStaticData.first : 0;
    if (!getStaticDataAddress && gloveReapplyCallsite.count == 1) {
        int32_t displacement = 0;
        if (Read(gloveReapplyCallsite.first + 33, displacement))
            getStaticDataAddress = gloveReapplyCallsite.first + 37 +
                displacement;
    }
    uintptr_t getPaintDataAddress = 0;
    if (gloveReapplyCallsite.count == 1) {
        int32_t displacement = 0;
        if (Read(gloveReapplyCallsite.first + 0x39, displacement))
            getPaintDataAddress = gloveReapplyCallsite.first + 0x3D +
                displacement;
    }
    g_getEconItemStaticData = IsExecutable(
        reinterpret_cast<const void*>(getStaticDataAddress))
        ? reinterpret_cast<GetEconItemStaticDataFn>(getStaticDataAddress)
        : nullptr;
    g_rejectEconItemView = rejectItemView.count == 1
        ? reinterpret_cast<RejectEconItemViewFn>(rejectItemView.first)
        : nullptr;
    g_getEconItemPaintData = IsExecutable(
        reinterpret_cast<const void*>(getPaintDataAddress))
        ? reinterpret_cast<GetEconItemPaintDataFn>(getPaintDataAddress)
        : nullptr;
    SIZE_T itemSystemVtableIndex = 0;
    if (clientInterface) {
        constexpr SIZE_T itemSystemCandidates[] = { 122, 128 };
        for (const SIZE_T candidate : itemSystemCandidates) {
            const auto getEconItemSystem =
                GetVirtualFunction<GetEconItemSystemFn>(
                    clientInterface, candidate);
            void* itemSystem = nullptr;
            if (getEconItemSystem) {
                __try {
                    itemSystem = getEconItemSystem(clientInterface);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    itemSystem = nullptr;
                }
            }
            void* schema = nullptr;
            if (itemSystem)
                (void)Read(reinterpret_cast<uintptr_t>(itemSystem) + 0x8,
                    schema);
            if (schema && GetVirtualFunction<GetAttributeDefinitionFn>(
                    schema, 27)) {
                g_itemSchema = schema;
                itemSystemVtableIndex = candidate;
                break;
            }
        }
    }
    g_diagnostics.attributeSettersResolved =
        g_setDynamicAttributeValue && g_itemSchema;
    if (g_diagnostics.attributeSettersResolved) {
        char message[160]{};
        StringCchPrintfA(message, _countof(message),
            "SOCache attribute schema: client_vfunc=%llu schema=0x%llX.",
            static_cast<unsigned long long>(itemSystemVtableIndex),
            static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(g_itemSchema)));
        AppendLog(message);
    }
    g_diagnostics.itemViewCopyResolved = g_copyEconItemView != nullptr;
    g_diagnostics.itemViewClearResolved = g_clearEconItemView != nullptr;

    if (managerPattern.count == 1) {
        int32_t displacement = 0;
        if (Read(managerPattern.first + 3, displacement)) {
            g_diagnostics.manager = managerPattern.first + 7 + displacement;
            g_diagnostics.managerResolved = IsReadable(
                reinterpret_cast<const void*>(g_diagnostics.manager),
                sizeof(uintptr_t));
        }
    }

    if (g_diagnostics.managerResolved) {
        g_diagnostics.inventoryResolved = ResolveInventoryFromManager(
            g_diagnostics.manager, g_diagnostics.inventory,
            g_diagnostics.socache, g_diagnostics.itemTypeCache,
            g_diagnostics.itemCount);
        g_diagnostics.socacheResolved =
            g_diagnostics.inventoryResolved && g_diagnostics.socache != 0;
        g_diagnostics.itemTypeCacheResolved =
            g_diagnostics.inventoryResolved &&
            g_diagnostics.itemTypeCache != 0;
    }

    g_diagnostics.writeReady =
        g_diagnostics.itemTypeCacheResolved && g_createEconItem &&
        (g_diagnostics.attributeSettersResolved || g_allocateAttribute) &&
        g_equipItemInLoadout &&
        GetVirtualFunction<AddObjectFn>(reinterpret_cast<void*>(
            g_diagnostics.itemTypeCache), 1) &&
        GetVirtualFunction<RemoveObjectFn>(reinterpret_cast<void*>(
            g_diagnostics.itemTypeCache), 3) &&
        GetVirtualFunction<SOEventFn>(reinterpret_cast<void*>(
            g_diagnostics.inventory), 0) &&
        GetVirtualFunction<SOEventFn>(reinterpret_cast<void*>(
            g_diagnostics.inventory), 2);

    uintptr_t clientVtable = 0;
    constexpr SIZE_T kEconAccessorIndex = 122;
    if (clientInterface &&
        Read(reinterpret_cast<uintptr_t>(clientInterface), clientVtable) &&
        clientVtable && Read(clientVtable + kEconAccessorIndex *
            sizeof(uintptr_t), g_diagnostics.clientEconAccessor) &&
        IsExecutable(reinterpret_cast<const void*>(
            g_diagnostics.clientEconAccessor))) {
        unsigned char code[3]{};
        if (IsReadable(reinterpret_cast<const void*>(
                g_diagnostics.clientEconAccessor), sizeof(code)))
            CopyMemory(code, reinterpret_cast<const void*>(
                g_diagnostics.clientEconAccessor), sizeof(code));
        g_diagnostics.clientEconAccessorCandidate =
            !(code[0] == 0x32 && code[1] == 0xC0 && code[2] == 0xC3);

        char header[192]{};
        StringCchPrintfA(header, _countof(header),
            "SOCache client ABI: interface=0x%llX vtable=0x%llX.",
            static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(clientInterface)),
            static_cast<unsigned long long>(clientVtable));
        AppendLog(header);
        const uintptr_t clientBase = reinterpret_cast<uintptr_t>(client);
        for (SIZE_T index = 116; index <= 132; ++index) {
            uintptr_t function = 0;
            unsigned char bytes[12]{};
            if (!Read(clientVtable + index * sizeof(uintptr_t), function) ||
                !IsExecutable(reinterpret_cast<const void*>(function)) ||
                !IsReadable(reinterpret_cast<const void*>(function),
                    sizeof(bytes)))
                continue;
            CopyMemory(bytes, reinterpret_cast<const void*>(function),
                sizeof(bytes));
            char line[256]{};
            StringCchPrintfA(line, _countof(line),
                "SOCache client vfunc[%llu]=client+0x%llX bytes="
                "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                static_cast<unsigned long long>(index),
                static_cast<unsigned long long>(function - clientBase),
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11]);
            AppendLog(line);
        }
    }

    LogDiagnostics(client, managerPattern, econFactory, setAttribute,
        cacheCaller);
    char equipMessage[192]{};
    StringCchPrintfA(equipMessage, _countof(equipMessage),
        "SOCache loadout: equip_pattern=%lu equip_rva=0x%llX "
        "view_copy=%lu copy_rva=0x%llX view_clear=%lu clear_rva=0x%llX.",
        static_cast<unsigned long>(equipItem.count),
        static_cast<unsigned long long>(equipItem.first
            ? equipItem.first - reinterpret_cast<uintptr_t>(client) : 0),
        static_cast<unsigned long>(copyItemView.count),
        static_cast<unsigned long long>(copyItemView.first
            ? copyItemView.first - reinterpret_cast<uintptr_t>(client) : 0),
        static_cast<unsigned long>(clearItemView.count),
        static_cast<unsigned long long>(clearItemView.first
            ? clearItemView.first - reinterpret_cast<uintptr_t>(client) : 0));
    AppendLog(equipMessage);
    return g_diagnostics.inventoryResolved &&
        g_diagnostics.itemTypeCacheResolved;
}

InventorySocacheDiagnostics GetInventorySocacheDiagnostics() noexcept {
    return g_diagnostics;
}

bool CopyInventorySocacheItemView(
    uintptr_t destination, uintptr_t source) noexcept {
    if (!g_copyEconItemView || !destination || !source ||
        destination == source ||
        !IsWritable(reinterpret_cast<void*>(destination), 0x1E9) ||
        !IsReadable(reinterpret_cast<const void*>(source), 0x1E9))
        return false;
    __try {
        (void)g_copyEconItemView(reinterpret_cast<void*>(destination),
            reinterpret_cast<const void*>(source));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

bool ClearInventorySocacheItemView(uintptr_t itemView) noexcept {
    if (!g_clearEconItemView || !itemView ||
        !IsWritable(reinterpret_cast<void*>(itemView), 0x1E9))
        return false;
    __try {
        g_clearEconItemView(reinterpret_cast<void*>(itemView));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

void LogInventorySocacheItemViewDiagnostics(
    uintptr_t destination, uintptr_t source) noexcept {
    uint16_t destinationDefinition = 0;
    uint16_t sourceDefinition = 0;
    uint8_t destinationInitialized = 0;
    uint8_t sourceInitialized = 0;
    int destinationAttributes = -1;
    int sourceAttributes = -1;
    int destinationNetworkedAttributes = -1;
    int sourceNetworkedAttributes = -1;
    int destinationPaintKit = -1;
    int sourcePaintKit = -1;
    (void)Read(destination + 0x1BA, destinationDefinition);
    (void)Read(source + 0x1BA, sourceDefinition);
    (void)Read(destination + 0x1E8, destinationInitialized);
    (void)Read(source + 0x1E8, sourceInitialized);
    (void)Read(destination + 0x218, destinationAttributes);
    (void)Read(source + 0x218, sourceAttributes);
    (void)Read(destination + 0x290, destinationNetworkedAttributes);
    (void)Read(source + 0x290, sourceNetworkedAttributes);

    void* destinationStaticData = nullptr;
    void* sourceStaticData = nullptr;
    void* destinationPaintData = nullptr;
    void* sourcePaintData = nullptr;
    bool destinationRejected = false;
    bool sourceRejected = false;
    __try {
        if (g_getEconItemStaticData) {
            destinationStaticData = g_getEconItemStaticData(
                reinterpret_cast<void*>(destination));
            sourceStaticData = g_getEconItemStaticData(
                reinterpret_cast<void*>(source));
        }
        if (g_rejectEconItemView) {
            destinationRejected = g_rejectEconItemView(
                reinterpret_cast<void*>(destination));
            sourceRejected = g_rejectEconItemView(
                reinterpret_cast<void*>(source));
        }
        if (g_getEconItemPaintData) {
            destinationPaintData = g_getEconItemPaintData(
                reinterpret_cast<void*>(destination));
            sourcePaintData = g_getEconItemPaintData(
                reinterpret_cast<void*>(source));
        }
        const auto destinationPaint =
            GetVirtualFunction<GetEconItemPaintKitFn>(
                reinterpret_cast<void*>(destination), 3);
        const auto sourcePaint = GetVirtualFunction<GetEconItemPaintKitFn>(
            reinterpret_cast<void*>(source), 3);
        if (destinationPaint)
            destinationPaintKit = destinationPaint(
                reinterpret_cast<void*>(destination));
        if (sourcePaint)
            sourcePaintKit = sourcePaint(reinterpret_cast<void*>(source));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        AppendLog("SOCache view diagnostic: native inspection faulted.");
        return;
    }

    char message[560]{};
    StringCchPrintfA(message, _countof(message),
        "SOCache view diagnostic: destination=0x%llX def=%u init=%u "
        "paint=%d attrs=%d/%d model=0x%llX paint_data=0x%llX rejected=%s "
        "source=0x%llX def=%u init=%u paint=%d attrs=%d/%d "
        "model=0x%llX paint_data=0x%llX rejected=%s "
        "model_fn=%s paint_fn=%s reject_fn=%s.",
        static_cast<unsigned long long>(destination),
        static_cast<unsigned int>(destinationDefinition),
        static_cast<unsigned int>(destinationInitialized),
        destinationPaintKit, destinationAttributes,
        destinationNetworkedAttributes,
        static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(destinationStaticData)),
        static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(destinationPaintData)),
        destinationRejected ? "yes" : "no",
        static_cast<unsigned long long>(source),
        static_cast<unsigned int>(sourceDefinition),
        static_cast<unsigned int>(sourceInitialized),
        sourcePaintKit, sourceAttributes, sourceNetworkedAttributes,
        static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(sourceStaticData)),
        static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(sourcePaintData)),
        sourceRejected ? "yes" : "no",
        g_getEconItemStaticData ? "yes" : "no",
        g_getEconItemPaintData ? "yes" : "no",
        g_rejectEconItemView ? "yes" : "no");
    AppendLog(message);
}

namespace {
bool RestoreLoadoutOverride(
    LoadoutOverrideState& overrideState, const char* reason) noexcept {
    if (!overrideState.active) return true;
    uint64_t currentItemId = 0;
    const bool read = ReadLoadoutItemId(
        overrideState.team, overrideState.loadoutSlot,
        overrideState.itemViewItemIdOffset, currentItemId);
    const bool restored = !read ||
        currentItemId != overrideState.currentItemId ||
        EquipLoadoutItem(overrideState.team, overrideState.loadoutSlot,
            overrideState.originalItemId);
    char message[256]{};
    StringCchPrintfA(message, _countof(message),
        "SOCache loadout restore: reason=%s team=%d slot=%d "
        "current=%llu original=%llu restored=%s.",
        reason ? reason : "restore", overrideState.team,
        overrideState.loadoutSlot,
        static_cast<unsigned long long>(overrideState.currentItemId),
        static_cast<unsigned long long>(overrideState.originalItemId),
        restored ? "yes" : "no");
    AppendLog(message);
    if (restored) overrideState = {};
    return restored;
}

bool EquipGeneratedItem(
    GeneratedItemState& generated, const InventorySocacheItemSpec& spec,
    uintptr_t& loadoutItemView) noexcept {
    loadoutItemView = 0;
    if (!IsValidLoadoutContext(spec.team, spec.loadoutSlot) ||
        !spec.itemViewItemIdOffset)
        return false;

    uint64_t currentItemId = 0;
    if (!ReadLoadoutItemId(spec.team, spec.loadoutSlot,
            spec.itemViewItemIdOffset, currentItemId, &loadoutItemView))
        return false;

    LoadoutOverrideState* overrideState = FindLoadoutOverride(
        spec.team, spec.loadoutSlot);
    if (!overrideState) {
        overrideState = FindFreeLoadoutOverride();
        if (!overrideState) return false;
        overrideState->active = true;
        overrideState->team = spec.team;
        overrideState->loadoutSlot = spec.loadoutSlot;
        overrideState->itemViewItemIdOffset = spec.itemViewItemIdOffset;
        overrideState->originalItemId = currentItemId;
    }

    if (currentItemId != generated.itemId &&
        !EquipLoadoutItem(spec.team, spec.loadoutSlot, generated.itemId))
        return false;

    overrideState->currentItemId = generated.itemId;
    uint64_t equippedItemId = 0;
    return ReadLoadoutItemId(spec.team, spec.loadoutSlot,
        spec.itemViewItemIdOffset, equippedItemId, &loadoutItemView) &&
        equippedItemId == generated.itemId;
}

bool RemoveGeneratedItem(
    GeneratedItemState& state, const char* reason) noexcept {
    if (!state.item) return true;
    void* item = state.item;
    const bool wasPresent = IsItemInTypeCache(item);
    bool removed = !wasPresent;
    bool notified = true;
    bool loadoutRestored = true;
    for (LoadoutOverrideState& overrideState : g_loadoutOverrides) {
        if (overrideState.active &&
            overrideState.currentItemId == state.itemId)
            loadoutRestored = RestoreLoadoutOverride(
                overrideState, reason) && loadoutRestored;
    }
    if (wasPresent) {
        notified = InvokeInventoryEvent(2, item);
        removed = InvokeRemoveObject(item) && !IsItemInTypeCache(item);
    }
    const bool destroyed = removed ? DestructEconItem(item) : false;

    char message[320]{};
    StringCchPrintfA(message, _countof(message),
        "SOCache item remove: reason=%s id=%llu local=%llu slot=%d "
        "notified=%s removed=%s destroyed=%s loadout_restored=%s.",
        reason ? reason : "cleanup",
        static_cast<unsigned long long>(state.itemId),
        static_cast<unsigned long long>(state.localId), state.loadoutSlot,
        notified ? "yes" : "no", removed ? "yes" : "no",
        destroyed ? "yes" : "no",
        loadoutRestored ? "yes" : "no");
    AppendLog(message);

    if (removed) state = {};
    return removed;
}
}

void RemoveInventorySocacheItem(const char* reason) noexcept {
    for (GeneratedItemState& state : g_generatedItems)
        (void)RemoveGeneratedItem(state, reason);
}

void RemoveInventorySocacheItemsForSlot(
    int loadoutSlot, const char* reason) noexcept {
    RestoreInventorySocacheLoadoutSlot(loadoutSlot, reason);
    for (GeneratedItemState& state : g_generatedItems) {
        if (state.item && state.loadoutSlot == loadoutSlot)
            (void)RemoveGeneratedItem(state, reason);
    }
}

void RestoreInventorySocacheLoadoutSlot(
    int loadoutSlot, const char* reason) noexcept {
    for (LoadoutOverrideState& overrideState : g_loadoutOverrides) {
        if (overrideState.active &&
            overrideState.loadoutSlot == loadoutSlot)
            (void)RestoreLoadoutOverride(overrideState, reason);
    }
}

void PruneInventorySocacheCollection(
    const uint64_t* localIds, int count, int loadoutSlot,
    const char* reason) noexcept {
    if (count < 0) count = 0;
    for (GeneratedItemState& state : g_generatedItems) {
        if (!state.item || state.loadoutSlot != loadoutSlot) continue;
        bool keep = false;
        for (int index = 0; index < count; ++index) {
            if (localIds && localIds[index] == state.localId) {
                keep = true;
                break;
            }
        }
        if (!keep) (void)RemoveGeneratedItem(state, reason);
    }
}

bool ReadInventorySocacheLoadoutSelection(
    int team, int loadoutSlot, uintptr_t itemViewItemIdOffset,
    InventorySocacheLoadoutSelection& selection) noexcept {
    selection = {};
    if (!ReadLoadoutItemId(team, loadoutSlot, itemViewItemIdOffset,
            selection.itemId))
        return false;
    GeneratedItemState* generated = FindGeneratedItemByItemId(
        selection.itemId);
    if (generated) {
        selection.generated = true;
        selection.localId = generated->localId;
    }
    return true;
}

bool ResolveInventorySocacheGeneratedItemId(
    uint64_t localId, uint64_t& itemId) noexcept {
    itemId = 0;
    GeneratedItemState* generated = FindGeneratedItem(localId);
    if (!generated || !IsItemInTypeCache(generated->item)) return false;
    itemId = generated->itemId;
    return true;
}

bool ResolveInventorySocacheGeneratedLocalId(
    uint64_t itemId, uint64_t& localId) noexcept {
    localId = 0;
    GeneratedItemState* generated = FindGeneratedItemByItemId(itemId);
    if (!generated || !IsItemInTypeCache(generated->item)) return false;
    localId = generated->localId;
    return localId != 0;
}

bool ReadInventorySocacheItemUnacknowledged(
    uint64_t localId, bool& unacknowledged) noexcept {
    unacknowledged = false;
    GeneratedItemState* generated = FindGeneratedItem(localId);
    if (!generated || !IsItemInTypeCache(generated->item)) return false;
    uint32_t inventoryPosition = 0;
    if (!Read(reinterpret_cast<uintptr_t>(generated->item) + 0x2C,
            inventoryPosition))
        return false;
    constexpr uint32_t kUnacknowledgedFlag = 1u << 30;
    unacknowledged = (inventoryPosition & kUnacknowledgedFlag) != 0;
    return true;
}

bool EnsureInventorySocacheItem(
    const InventorySocacheItemSpec& spec,
    InventorySocacheItemIdentity& identity) noexcept {
    identity = {};
    if (!g_diagnostics.writeReady || spec.localId == 0 ||
        (spec.equip && !IsValidLoadoutContext(
            spec.team, spec.loadoutSlot)) ||
        spec.loadoutSlot < 0 || spec.loadoutSlot > 63 ||
        spec.itemViewItemIdOffset == 0 ||
        spec.definitionIndex <= 0 || spec.definitionIndex > 0xFFFF)
        return false;

    GeneratedItemState* generated = FindGeneratedItem(spec.localId);
    const bool sameGeneratedItem = generated &&
        generated->definition == spec.definitionIndex &&
        generated->musicKitId == spec.musicKitId &&
        generated->paintKit == spec.paintKit &&
        generated->seed == spec.seed && generated->wear == spec.wear &&
        generated->statTrak == spec.statTrak &&
        generated->statTrakType == spec.statTrakType &&
        generated->variantAttributeDefinition ==
            spec.variantAttributeDefinition &&
        generated->variantAttributeValue == spec.variantAttributeValue &&
        generated->quality == spec.quality &&
        generated->rarity == spec.rarity &&
        generated->loadoutSlot == spec.loadoutSlot;
    if (sameGeneratedItem && IsItemInTypeCache(generated->item)) {
        constexpr uint32_t kUnacknowledgedFlag = 1u << 30;
        const bool scoreChanged = spec.statTrak &&
            generated->statTrakCount != spec.statTrakCount;
        if (scoreChanged) {
            int statTrakCount = spec.statTrakCount;
            if (!AddEconAttribute(generated->item, 80, &statTrakCount,
                    static_cast<uint32_t>(statTrakCount)))
                return false;
        }
        uint32_t inventoryPosition = 0;
        if (!Read(reinterpret_cast<uintptr_t>(generated->item) + 0x2C,
                inventoryPosition))
            return false;
        const uint32_t desiredPosition = spec.unacknowledged
            ? kUnacknowledgedFlag
            : static_cast<uint32_t>(spec.localId &
                (kUnacknowledgedFlag - 1));
        const bool positionChanged = desiredPosition != inventoryPosition;
        if (positionChanged &&
            !Write(reinterpret_cast<uintptr_t>(generated->item) + 0x2C,
                desiredPosition))
            return false;
        if ((scoreChanged || positionChanged) &&
            !InvokeInventoryEvent(1, generated->item))
            return false;
        if (scoreChanged) {
            generated->statTrakCount = spec.statTrakCount;
            char message[192]{};
            StringCchPrintfA(message, _countof(message),
                "SOCache item update: local=%llu StatTrak=%d in-place.",
                static_cast<unsigned long long>(spec.localId),
                spec.statTrakCount);
            AppendLog(message);
        }
        generated->unacknowledged = spec.unacknowledged;
        identity.object = reinterpret_cast<uintptr_t>(generated->item);
        if (spec.equip && !EquipGeneratedItem(
                *generated, spec, identity.loadoutItemView))
            return false;
        identity.itemId = generated->itemId;
        identity.accountId = generated->accountId;
        return true;
    }

    if (generated) {
        if (!RemoveGeneratedItem(*generated, "selection-changed"))
            return false;
    }
    generated = FindFreeGeneratedItem();
    if (!generated) {
        AppendLog("SOCache item create: no hay slots temporales libres.");
        return false;
    }

    void* item = nullptr;
    uint64_t itemId = 0;
    uint32_t inventoryPosition = 0;
    SOID owner{};
    if (!CreateEconItem(item) ||
        !ResolveNextItemIdentity(spec.localId, spec.unacknowledged, itemId,
            inventoryPosition) ||
        !Read(g_diagnostics.inventory + 0x10, owner)) {
        if (item) (void)DestructEconItem(item);
        return false;
    }

    const uint32_t accountId = static_cast<uint32_t>(owner.id);
    const uint16_t definition = static_cast<uint16_t>(spec.definitionIndex);
    uint16_t packed = 0;
    (void)Read(reinterpret_cast<uintptr_t>(item) + 0x32, packed);
    constexpr uint16_t kQualityMask = 0xFu << 5;
    constexpr uint16_t kRarityMask = 0xFu << 11;
    packed = static_cast<uint16_t>((packed &
        ~(kQualityMask | kRarityMask)) |
        ((spec.quality & 0xFu) << 5) |
        ((spec.rarity & 0xFu) << 11));
    const uint64_t originalId = 0;
    bool prepared = Write(reinterpret_cast<uintptr_t>(item) + 0x10, itemId) &&
        Write(reinterpret_cast<uintptr_t>(item) + 0x18, originalId) &&
        Write(reinterpret_cast<uintptr_t>(item) + 0x28, accountId) &&
        Write(reinterpret_cast<uintptr_t>(item) + 0x2C,
            inventoryPosition) &&
        Write(reinterpret_cast<uintptr_t>(item) + 0x30, definition) &&
        Write(reinterpret_cast<uintptr_t>(item) + 0x32, packed);

    if (prepared && spec.musicKitId > 0) {
        int musicKitId = spec.musicKitId;
        prepared = AddEconAttribute(item, 166, &musicKitId,
            static_cast<uint32_t>(musicKitId));
    }

    float wear = spec.wear;
    if (wear < 0.000001f) wear = 0.000001f;
    if (wear > 1.0f) wear = 1.0f;
    if (prepared && spec.paintKit > 0) {
        float paintKit = static_cast<float>(spec.paintKit);
        float paintSeed = static_cast<float>(spec.seed);
        prepared = AddEconAttribute(item, 6, &paintKit,
                FloatBits(paintKit)) &&
            AddEconAttribute(item, 7, &paintSeed,
                FloatBits(paintSeed)) &&
            AddEconAttribute(item, 8, &wear, FloatBits(wear));
    }
    if (prepared && spec.statTrak) {
        int statTrakCount = spec.statTrakCount;
        int statTrakType = spec.statTrakType;
        prepared = AddEconAttribute(item, 80, &statTrakCount,
                static_cast<uint32_t>(statTrakCount)) &&
            AddEconAttribute(item, 81, &statTrakType,
                static_cast<uint32_t>(statTrakType));
    }
    if (prepared && spec.variantAttributeDefinition > 0) {
        uint32_t variantValue = spec.variantAttributeValue;
        prepared = AddEconAttribute(item,
            spec.variantAttributeDefinition, &variantValue, variantValue);
    }

    if (!prepared || !InvokeAddObject(item) || !IsItemInTypeCache(item)) {
        if (IsItemInTypeCache(item)) {
            (void)InvokeRemoveObject(item);
        }
        if (!IsItemInTypeCache(item)) (void)DestructEconItem(item);
        AppendLog("SOCache item create: preparacion o AddObject fallo.");
        return false;
    }
    if (!InvokeInventoryEvent(0, item)) {
        (void)InvokeRemoveObject(item);
        if (!IsItemInTypeCache(item)) (void)DestructEconItem(item);
        AppendLog("SOCache item create: SOCreated fallo; rollback aplicado.");
        return false;
    }
    uintptr_t loadoutItemView = 0;

    generated->item = item;
    generated->localId = spec.localId;
    generated->itemId = itemId;
    generated->accountId = accountId;
    generated->definition = spec.definitionIndex;
    generated->musicKitId = spec.musicKitId;
    generated->paintKit = spec.paintKit;
    generated->seed = spec.seed;
    generated->wear = spec.wear;
    generated->statTrak = spec.statTrak;
    generated->statTrakCount = spec.statTrakCount;
    generated->statTrakType = spec.statTrakType;
    generated->variantAttributeDefinition =
        spec.variantAttributeDefinition;
    generated->variantAttributeValue = spec.variantAttributeValue;
    generated->quality = spec.quality;
    generated->rarity = spec.rarity;
    generated->unacknowledged = spec.unacknowledged;
    generated->loadoutSlot = spec.loadoutSlot;
    generated->itemViewItemIdOffset = spec.itemViewItemIdOffset;
    const bool loadoutVerified = !spec.equip || EquipGeneratedItem(
        *generated, spec, loadoutItemView);
    if (!loadoutVerified) {
        (void)RemoveGeneratedItem(*generated, "equip-failed");
        AppendLog("SOCache item create: equipar loadout fallo; rollback aplicado.");
        return false;
    }
    identity.object = reinterpret_cast<uintptr_t>(item);
    identity.loadoutItemView = loadoutItemView;
    identity.itemId = itemId;
    identity.accountId = accountId;

    char message[320]{};
    StringCchPrintfA(message, _countof(message),
        "SOCache item create: object=0x%llX local=%llu id=%llu def=%d "
        "music=%d paint=%d seed=%d wear=%.6f stattrak=%s score=%d "
        "score_type=%d attr=%d:%u cache_count=%d team=%d slot=%d "
        "new=%s equip=%s "
        "loadout=%s previous=%llu.",
        static_cast<unsigned long long>(identity.object),
        static_cast<unsigned long long>(spec.localId),
        static_cast<unsigned long long>(itemId), spec.definitionIndex,
        spec.musicKitId, spec.paintKit, spec.seed, wear,
        spec.statTrak ? "yes" : "no", spec.statTrakCount,
        spec.statTrakType, spec.variantAttributeDefinition,
        spec.variantAttributeValue,
        g_diagnostics.itemCount + 1,
        spec.team, spec.loadoutSlot,
        spec.unacknowledged ? "yes" : "no",
        spec.equip ? "yes" : "no",
        loadoutVerified ? "verified" : "unverified",
        0ull);
    AppendLog(message);
    return true;
}

void ShutdownInventorySocache() noexcept {
    RemoveInventorySocacheItem("bridge-shutdown");
    for (LoadoutOverrideState& state : g_loadoutOverrides) state = {};
    g_createEconItem = nullptr;
    g_allocateAttribute = nullptr;
    g_setDynamicAttributeValue = nullptr;
    g_itemSchema = nullptr;
    g_equipItemInLoadout = nullptr;
    g_copyEconItemView = nullptr;
    g_clearEconItemView = nullptr;
    g_getEconItemStaticData = nullptr;
    g_rejectEconItemView = nullptr;
    g_getEconItemPaintData = nullptr;
    g_diagnostics = {};
}
