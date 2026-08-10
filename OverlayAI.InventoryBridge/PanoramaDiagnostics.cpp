// ============================================================
// PanoramaDiagnostics.cpp
// Diagnostics for the mounted Panorama system. Verifies that interfaces work correctly.
// ============================================================

#include "PanoramaDiagnostics.h"

#include <windows.h>
#include <strsafe.h>

#include <cstdint>

#include "BridgeLogging.h"

namespace {
    struct PatternResult {
        DWORD count = 0;
        uintptr_t first = 0;
    };

    SIZE_T ImageSize(HMODULE module) noexcept {
        if (!module) return 0;
        const auto* base = reinterpret_cast<const unsigned char*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE
            ? nt->OptionalHeader.SizeOfImage : 0;
    }

    bool ReadPointer(uintptr_t address, uintptr_t& value) noexcept {
        value = 0;
        SIZE_T bytesRead = 0;
        return address != 0 && ReadProcessMemory(
            GetCurrentProcess(), reinterpret_cast<const void*>(address),
            &value, sizeof(value), &bytesRead) != FALSE &&
            bytesRead == sizeof(value);
    }

    bool IsExecutableAddress(uintptr_t address) noexcept {
        MEMORY_BASIC_INFORMATION info{};
        if (!address || !VirtualQuery(reinterpret_cast<const void*>(address),
            &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (info.Protect & executable) != 0;
    }

    PatternResult ScanExecutableSections(
        HMODULE module, const unsigned char* bytes, const char* mask) noexcept {
        PatternResult result;
        if (!module || !bytes || !mask) return result;
        const SIZE_T patternLength = static_cast<SIZE_T>(lstrlenA(mask));
        if (!patternLength) return result;

        const auto* base = reinterpret_cast<const unsigned char*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return result;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return result;

        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections;
            ++index, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;
            const SIZE_T sectionSize = section->Misc.VirtualSize;
            if (sectionSize < patternLength) continue;
            const unsigned char* start = base + section->VirtualAddress;
            for (SIZE_T offset = 0; offset <= sectionSize - patternLength;
                ++offset) {
                bool matches = true;
                for (SIZE_T byte = 0; byte < patternLength; ++byte) {
                    if (mask[byte] == 'x' &&
                        start[offset + byte] != bytes[byte]) {
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

    uintptr_t ResolveRelative(uintptr_t instruction, SIZE_T displacementOffset) noexcept {
        std::int32_t displacement = 0;
        SIZE_T bytesRead = 0;
        const uintptr_t displacementAddress = instruction + displacementOffset;
        if (!ReadProcessMemory(GetCurrentProcess(),
            reinterpret_cast<const void*>(displacementAddress), &displacement,
            sizeof(displacement), &bytesRead) || bytesRead != sizeof(displacement))
            return 0;
        return displacementAddress + sizeof(displacement) + displacement;
    }

    const char* ModuleLabel(
        uintptr_t address, HMODULE client, HMODULE panorama,
        uintptr_t& relative) noexcept {
        relative = 0;
        const uintptr_t clientBase = reinterpret_cast<uintptr_t>(client);
        const uintptr_t panoramaBase = reinterpret_cast<uintptr_t>(panorama);
        const SIZE_T clientSize = ImageSize(client);
        const SIZE_T panoramaSize = ImageSize(panorama);
        if (address >= clientBase && address < clientBase + clientSize) {
            relative = address - clientBase;
            return "client";
        }
        if (address >= panoramaBase && address < panoramaBase + panoramaSize) {
            relative = address - panoramaBase;
            return "panorama";
        }
        return "other";
    }

    bool IsAddressInModule(uintptr_t address, HMODULE module) noexcept {
        const uintptr_t base = reinterpret_cast<uintptr_t>(module);
        const SIZE_T size = ImageSize(module);
        return address >= base && address < base + size;
    }

    bool ReadExecutableVtable(uintptr_t object, uintptr_t& vtable) noexcept {
        uintptr_t firstMethod = 0;
        return ReadPointer(object, vtable) &&
            ReadPointer(vtable, firstMethod) &&
            IsExecutableAddress(firstMethod);
    }
}

void LogPanoramaReadOnlyDiagnostics(void* panoramaInterface) noexcept {
    HMODULE client = GetModuleHandleW(L"client.dll");
    HMODULE panorama = GetModuleHandleW(L"panorama.dll");
    if (!panoramaInterface || !client || !panorama) {
        AppendLog("Panorama RO: modulos o interfaz no disponibles.");
        return;
    }

    uintptr_t uiEngine = 0;
    uintptr_t uiEngineVtable = 0;
    const bool uiEngineValid = ReadPointer(
        reinterpret_cast<uintptr_t>(panoramaInterface) + 0x28, uiEngine) &&
        ReadExecutableVtable(uiEngine, uiEngineVtable);

    constexpr unsigned char mainMenuObjectPattern[] = {
        0x48, 0x89, 0x35, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B,
        0x4E, 0x08, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x78
    };
    const PatternResult mainMenuResult = ScanExecutableSections(
        client, mainMenuObjectPattern, "xxx????xxxxxxxxxx");
    const uintptr_t mainMenuGlobal = mainMenuResult.count == 1
        ? ResolveRelative(mainMenuResult.first, 3) : 0;

    uintptr_t panelWrapper = 0;
    uintptr_t rootPanel = 0;
    uintptr_t panelWrapperVtable = 0;
    uintptr_t rootPanelVtable = 0;
    const bool panelWrapperValid = ReadPointer(
        mainMenuGlobal, panelWrapper) &&
        ReadExecutableVtable(panelWrapper, panelWrapperVtable) &&
        IsAddressInModule(panelWrapperVtable, client);
    const bool rootPanelValid = panelWrapperValid &&
        ReadPointer(panelWrapper + 0x8, rootPanel) &&
        ReadExecutableVtable(rootPanel, rootPanelVtable) &&
        IsAddressInModule(rootPanelVtable, panorama);
    const bool uiEngineModuleValid = uiEngineValid &&
        IsAddressInModule(uiEngineVtable, panorama);

    char message[384]{};
    StringCchPrintfA(message, _countof(message),
        "Panorama RO: signature=%lu global=client+0x%llX "
        "uiEngine=%s panelWrapper=%s rootPanel=%s.",
        static_cast<unsigned long>(mainMenuResult.count),
        static_cast<unsigned long long>(mainMenuGlobal
            ? mainMenuGlobal - reinterpret_cast<uintptr_t>(client) : 0),
        uiEngineModuleValid ? "valid" : "invalid",
        panelWrapperValid ? "valid" : "invalid",
        rootPanelValid ? "valid" : "invalid");
    AppendLog(message);

    uintptr_t uiEngineRva = 0;
    uintptr_t panelWrapperRva = 0;
    uintptr_t rootPanelRva = 0;
    const char* uiEngineModule = ModuleLabel(
        uiEngineVtable, client, panorama, uiEngineRva);
    const char* panelWrapperModule = ModuleLabel(
        panelWrapperVtable, client, panorama, panelWrapperRva);
    const char* rootPanelModule = ModuleLabel(
        rootPanelVtable, client, panorama, rootPanelRva);
    StringCchPrintfA(message, _countof(message),
        "Panorama RO vtables: uiEngine=%s+0x%llX "
        "panelWrapper=%s+0x%llX rootPanel=%s+0x%llX.",
        uiEngineModule, static_cast<unsigned long long>(uiEngineRva),
        panelWrapperModule,
        static_cast<unsigned long long>(panelWrapperRva),
        rootPanelModule, static_cast<unsigned long long>(rootPanelRva));
    AppendLog(message);
}
