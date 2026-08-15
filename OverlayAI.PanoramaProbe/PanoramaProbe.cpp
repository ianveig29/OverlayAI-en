#include <Windows.h>

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
    using CreateInterfaceFn = void* (*)(const char*, int*);

    struct PatternResult {
        std::size_t count = 0;
        std::uintptr_t first = 0;
    };

    std::vector<int> ParsePattern(const char* text) {
        std::vector<int> pattern;
        std::istringstream input{text ? text : ""};
        std::string token;
        while (input >> token) {
            if (token == "?" || token == "??")
                pattern.push_back(-1);
            else
                pattern.push_back(std::stoi(token, nullptr, 16));
        }
        return pattern;
    }

    PatternResult ScanExecutableSections(HMODULE module, const char* text) {
        PatternResult result;
        const std::vector<int> pattern = ParsePattern(text);
        if (!module || pattern.empty()) return result;

        const auto* base = reinterpret_cast<const unsigned char*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return result;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return result;

        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (WORD sectionIndex = 0; sectionIndex < nt->FileHeader.NumberOfSections;
             ++sectionIndex, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 ||
                section->Misc.VirtualSize < pattern.size())
                continue;
            const auto* start = base + section->VirtualAddress;
            const std::size_t size = section->Misc.VirtualSize;
            for (std::size_t offset = 0; offset <= size - pattern.size(); ++offset) {
                bool match = true;
                for (std::size_t index = 0; index < pattern.size(); ++index) {
                    if (pattern[index] >= 0 &&
                        start[offset + index] != pattern[index]) {
                        match = false;
                        break;
                    }
                }
                if (!match) continue;
                ++result.count;
                if (!result.first)
                    result.first = reinterpret_cast<std::uintptr_t>(start + offset);
            }
        }
        return result;
    }

    void PrintPattern(HMODULE module, const char* name, const char* pattern,
                      std::size_t displacementOffset = 0) {
        const PatternResult result = ScanExecutableSections(module, pattern);
        const auto moduleBase = reinterpret_cast<std::uintptr_t>(module);
        const auto rva = result.first ? result.first - moduleBase : 0;
        std::cout << "PATTERN name=" << name << " count=" << result.count
                  << " rva=0x" << std::hex << std::uppercase << rva;
        if (result.count == 1 && displacementOffset != 0) {
            const auto displacementAddress = result.first + displacementOffset;
            const auto displacement = *reinterpret_cast<const std::int32_t*>(
                displacementAddress);
            const auto target = displacementAddress + sizeof(displacement) +
                displacement;
            std::cout << " target_rva=0x" << (target - moduleBase);
        }
        std::cout << std::dec << std::nouppercase << "\n";
    }

    void PrintIntegerPattern(HMODULE module, const char* name,
                             const char* pattern, std::size_t valueOffset) {
        const PatternResult result = ScanExecutableSections(module, pattern);
        const auto moduleBase = reinterpret_cast<std::uintptr_t>(module);
        const auto rva = result.first ? result.first - moduleBase : 0;
        std::cout << "PATTERN name=" << name << " count=" << result.count
                  << " rva=0x" << std::hex << std::uppercase << rva;
        if (result.count == 1) {
            const auto value = *reinterpret_cast<const std::int32_t*>(
                result.first + valueOffset);
            std::cout << " value=0x" << value;
        }
        std::cout << std::dec << std::nouppercase << "\n";
    }

    void ProbePanoramaPatterns(HMODULE engine, HMODULE gameClient) {
        PrintPattern(gameClient, "UiEnginePointer",
            "48 89 78 ? 48 89 0D ? ? ? ?", 7);
        PrintPattern(gameClient, "MainMenuPanelPointer",
            "EC ? 48 8B 05 ? ? ? ? 48 8D 15 ? ? ? ? 48", 5);
        PrintPattern(gameClient, "MainMenuCandidateA",
            "48 83 EC ? 48 8B 05 ? ? ? ? 48 8D 15 ? ? ? ? 48", 7);
        PrintPattern(gameClient, "MainMenuCandidateB",
            "48 8B 05 ? ? ? ? 48 8D 15 ? ? ? ? 48", 3);
        PrintPattern(gameClient, "MainMenuCandidateC",
            "48 8B 0D ? ? ? ? 48 8D 15 ? ? ? ? 48", 3);
        PrintPattern(gameClient, "MainMenuCandidateD",
            "48 8B 05 ? ? ? ? 48 8D 0D ? ? ? ? 48", 3);
        PrintPattern(gameClient, "MainMenuObjectPointer",
            "48 89 35 ? ? ? ? 48 8B 4E 08 48 8B 01 FF 50 78", 3);
        PrintPattern(gameClient, "HudPanelPointer",
            "48 89 35 ? ? ? ? E8 ? ? ? ? 48 85", 3);
        PrintIntegerPattern(gameClient, "GetAttributeStringOffset",
            "12 48 8B 01 FF 90 ? ? ? ? 48 8B ? 48 85 C0 74 ? 80 38 00 74 ? 48 8D 4C", 6);
        PrintIntegerPattern(gameClient, "SetAttributeStringOffset",
            "FF 90 ? ? ? ? 48 83 C6 ? 48 3B ? 75 ? 4C", 2);
        PrintPattern(engine, "GetPanelPointer",
            "4C 63 0A 4C 8B DA");
        PrintPattern(engine, "RunScript",
            "48 89 5C 24 ? 4C 89 4C 24 ? ? 89 ? 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C");
        PrintPattern(engine, "MakeSymbol",
            "40 55 56 48 83 EC ? 48 63");
        PrintPattern(engine, "OnDeletePanel",
            "48 85 D2 0F 84 ? ? ? ? 48 89 ? 24 ? 57 48 83 EC ? 48");
        PrintPattern(engine, "RegisterEventHandler",
            "48 89 5C 24 ? 66 89 54 24 ? 55 56 57 41 56 41 57 48 83 EC ? 48 8D 05 ? ? ? ? 48 C7 44 24 28 ? ? ? ? 48 89 44 24 ? 4D");
    }

    std::wstring FormatWindowsError(DWORD error) {
        wchar_t* buffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::wstring message = length && buffer ? std::wstring(buffer, length) : L"Unknown error";
        if (buffer) LocalFree(buffer);
        return message;
    }

    HMODULE LoadGameModule(const std::filesystem::path& modulePath) {
        SetLastError(ERROR_SUCCESS);
        HMODULE module = LoadLibraryExW(
            modulePath.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS |
                LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            const DWORD error = GetLastError();
            std::wcerr << L"MODULE " << modulePath.filename().wstring()
                       << L" load=failed error=" << error << L" "
                       << FormatWindowsError(error) << L"\n";
        }
        else {
            std::wcout << L"MODULE " << modulePath.filename().wstring() << L" load=ok\n";
        }
        return module;
    }

    bool IsExecutableAddress(const void* address) {
        MEMORY_BASIC_INFORMATION info{};
        if (!address || !VirtualQuery(address, &info, sizeof(info)) ||
            info.State != MEM_COMMIT)
            return false;
        const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return (info.Protect & executable) != 0;
    }

    void DumpVtable(const char* interfaceName, void* instance,
                    std::size_t entryCount) {
        if (!instance || entryCount == 0) return;
        void** vtable = *reinterpret_cast<void***>(instance);
        if (!vtable) return;

        for (std::size_t index = 0; index < entryCount; ++index) {
            void* function = vtable[index];
            HMODULE owner = nullptr;
            const BOOL hasOwner = GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<const wchar_t*>(function), &owner);
            wchar_t ownerPath[MAX_PATH]{};
            if (hasOwner && owner)
                GetModuleFileNameW(owner, ownerPath, MAX_PATH);
            const auto rva = owner
                ? reinterpret_cast<std::uintptr_t>(function) -
                    reinterpret_cast<std::uintptr_t>(owner)
                : 0;

            const bool executable = IsExecutableAddress(function);
            std::cout << "VTABLE interface=" << interfaceName
                      << " index=" << index
                      << " executable=" << (executable ? "yes" : "no")
                      << " rva=0x" << std::hex << std::uppercase << rva
                      << std::dec << std::nouppercase << " module=";
            if (ownerPath[0])
                std::wcout << std::filesystem::path(ownerPath).filename().wstring();
            else
                std::cout << "<unknown>";
            std::cout << "\n";
            if (!executable) break;
        }
    }

    void* ReadVerifiedUiEngineMember(void* panoramaInterface) {
        if (!panoramaInterface) return nullptr;
        // PanoramaUIEngine001 index 13 is a four-instruction accessor in this
        // build: mov rax, [rcx+28h]; ret. Reading the member avoids invoking it.
        return *reinterpret_cast<void**>(
            static_cast<unsigned char*>(panoramaInterface) + 0x28);
    }

    bool ProbeInterface(HMODULE module, const wchar_t* moduleName, const char* interfaceName,
                        bool expected, void** output = nullptr) {
        auto createInterface = reinterpret_cast<CreateInterfaceFn>(
            GetProcAddress(module, "CreateInterface"));
        if (!createInterface) {
            std::wcerr << L"FACTORY " << moduleName << L" available=no\n";
            return false;
        }

        int returnCode = -1;
        void* instance = createInterface(interfaceName, &returnCode);
        const bool available = instance != nullptr;
        std::cout << "INTERFACE module=";
        std::wcout << moduleName;
        std::cout << " name=" << interfaceName
                  << " available=" << (available ? "yes" : "no")
                  << " return_code=" << returnCode << "\n";
        if (output) *output = instance;
        return available == expected;
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) {
        std::wcerr << L"Usage: OverlayAI.PanoramaProbe.exe <CS2 game\\bin\\win64> "
                      L"[CS2 game\\csgo\\bin\\win64]\n";
        return 2;
    }

    const std::filesystem::path binDirectory = std::filesystem::absolute(argv[1]);
    const std::filesystem::path clientBinDirectory = argc == 3
        ? std::filesystem::absolute(argv[2])
        : binDirectory.parent_path().parent_path() / L"csgo" / L"bin" / L"win64";
    if (!std::filesystem::is_directory(binDirectory)) {
        std::wcerr << L"Invalid bin directory: " << binDirectory.wstring() << L"\n";
        return 2;
    }
    if (!std::filesystem::is_directory(clientBinDirectory)) {
        std::wcerr << L"Invalid client bin directory: "
                   << clientBinDirectory.wstring() << L"\n";
        return 2;
    }

    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                                  LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        std::wcerr << L"SetDefaultDllDirectories failed.\n";
        return 3;
    }
    DLL_DIRECTORY_COOKIE cookie = AddDllDirectory(binDirectory.c_str());
    if (!cookie) {
        std::wcerr << L"AddDllDirectory failed.\n";
        return 3;
    }

    HMODULE engine = LoadGameModule(binDirectory / L"panorama.dll");
    HMODULE uiClient = LoadGameModule(binDirectory / L"panoramauiclient.dll");
    HMODULE gameClient = LoadGameModule(clientBinDirectory / L"client.dll");
    if (!engine || !uiClient || !gameClient) {
        RemoveDllDirectory(cookie);
        return 4;
    }

    bool valid = true;
    void* engineInterface = nullptr;
    void* clientInterface = nullptr;
    valid &= ProbeInterface(engine, L"panorama.dll", "PanoramaUIEngine001", true,
                            &engineInterface);
    valid &= ProbeInterface(engine, L"panorama.dll", "OverlayAI_Invalid_Interface", false);
    valid &= ProbeInterface(uiClient, L"panoramauiclient.dll", "PanoramaUIClient001", true,
                            &clientInterface);
    valid &= ProbeInterface(uiClient, L"panoramauiclient.dll", "OverlayAI_Invalid_Interface", false);

    DumpVtable("PanoramaUIEngine001", engineInterface, 32);
    DumpVtable("PanoramaUIClient001", clientInterface, 32);
    void* uiEngine = ReadVerifiedUiEngineMember(engineInterface);
    std::cout << "UIENGINE member=" << (uiEngine ? "available" : "null") << "\n";
    DumpVtable("CUIEngineSource2", uiEngine, 160);
    ProbePanoramaPatterns(engine, gameClient);

    RemoveDllDirectory(cookie);
    std::cout << "PROBE result=" << (valid ? "verified" : "unexpected") << "\n";
    return valid ? 0 : 5;
}
