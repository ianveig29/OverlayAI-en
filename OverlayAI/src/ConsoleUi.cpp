#include "ConsoleUi.h"

#include "DumperValidator.h"
#include "InventoryChanger.h"
#include "Localization.h"
#include "Memory.h"
#include "Offsets.h"
#include "resource.h"

#include <windows.h>
#include <conio.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace {
    enum class Tone {
        Default,
        Muted,
        Accent,
        Success,
        Warning,
        Error,
        Info
    };

    HANDLE g_output = INVALID_HANDLE_VALUE;
    WORD g_defaultAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    std::mutex g_consoleMutex;
    std::string g_input;
    bool g_interactive = false;
    bool g_promptVisible = false;
    std::atomic_bool g_shutdownRequested = false;

    WORD AttributesFor(Tone tone) {
        constexpr WORD bright = FOREGROUND_INTENSITY;
        switch (tone) {
        case Tone::Muted: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        case Tone::Accent: return FOREGROUND_GREEN | FOREGROUND_BLUE | bright;
        case Tone::Success: return FOREGROUND_GREEN | bright;
        case Tone::Warning: return FOREGROUND_RED | FOREGROUND_GREEN | bright;
        case Tone::Error: return FOREGROUND_RED | bright;
        case Tone::Info: return FOREGROUND_BLUE | FOREGROUND_GREEN | bright;
        default: return g_defaultAttributes;
        }
    }

    void SetTone(Tone tone) {
        if (g_output != INVALID_HANDLE_VALUE)
            SetConsoleTextAttribute(g_output, AttributesFor(tone));
    }

    void ClearPromptLocked() {
        if (!g_promptVisible) return;

        CONSOLE_SCREEN_BUFFER_INFO info{};
        const int fallbackWidth = 120;
        const int width = GetConsoleScreenBufferInfo(g_output, &info)
            ? (std::max)(1, static_cast<int>(info.dwSize.X) - 1)
            : fallbackWidth;
        std::cout << '\r' << std::string(static_cast<size_t>(width), ' ') << '\r';
        g_promptVisible = false;
    }

    void RenderPromptLocked() {
        if (!g_interactive || g_shutdownRequested.load()) return;
        SetTone(Tone::Accent);
        std::cout << "overlay";
        SetTone(Tone::Muted);
        std::cout << "> ";
        SetTone(Tone::Default);
        std::cout << g_input << std::flush;
        g_promptVisible = true;
    }

    void WriteLine(const std::string& text, Tone tone = Tone::Default) {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        ClearPromptLocked();
        SetTone(tone);
        std::cout << text << '\n';
        SetTone(Tone::Default);
        RenderPromptLocked();
    }

    void WriteStatus(const char* label, const std::string& detail, bool ok) {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        ClearPromptLocked();
        SetTone(ok ? Tone::Success : Tone::Warning);
        std::cout << (ok ? "[ OK ] " : "[ .. ] ");
        SetTone(Tone::Default);
        std::cout << label;
        if (!detail.empty()) {
            SetTone(Tone::Muted);
            std::cout << " | " << detail;
        }
        SetTone(Tone::Default);
        std::cout << '\n';
        RenderPromptLocked();
    }

    std::string LowerAndTrim(std::string value) {
        const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        });
        const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }).base();
        if (first >= last) return {};
        value = std::string(first, last);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    const char* BackendName(InventoryRuntimeBackend backend) {
        switch (backend) {
        case InventoryRuntimeBackend::ExternalOverlay:
            return Localized("Overlay externo", "External Overlay");
        case InventoryRuntimeBackend::InjectedBridge:
            return Localized("Bridge inyectado", "Injected Bridge");
        default: return Localized("Desactivado", "Disabled");
        }
    }

    bool OffsetsAreValidated(const OffsetUpdateStatus& status) {
        return status.loaded &&
            status.globalOffsetsFound > 0 &&
            status.schemaOffsetsFound > 0;
    }

    bool ParseOffsetGroup(const std::string& command, OffsetGroup& group, bool& filter) {
        filter = command != "offsets" && command != "offsets all";
        if (!filter) return true;
        if (command == "offsets core") group = OffsetGroup::Core;
        else if (command == "offsets inventory") group = OffsetGroup::Inventory;
        else if (command == "offsets visuals") group = OffsetGroup::Visuals;
        else return false;
        return true;
    }

    void PrintOffsets(const std::string& command) {
        OffsetGroup requestedGroup = OffsetGroup::Core;
        bool filter = false;
        if (!ParseOffsetGroup(command, requestedGroup, filter)) {
            WriteLine(Localized("Uso: offsets [all|core|inventory|visuals]",
                "Usage: offsets [all|core|inventory|visuals]"), Tone::Warning);
            return;
        }

        const OffsetUpdateStatus& status = GetOffsetUpdateStatus();
        const auto entries = GetLoadedOffsetsSnapshot();
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        ClearPromptLocked();
        SetTone(Tone::Accent);
        std::cout << "\nOFFSETS";
        SetTone(Tone::Muted);
        std::cout << Localized(
            " | verde=validado, amarillo=cache/valor, rojo=faltante\n",
            " | green=validated, yellow=cache/value, red=missing\n");
        for (const LoadedOffsetEntry& entry : entries) {
            if (filter && entry.group != requestedGroup) continue;
            SetTone(Tone::Default);
            std::cout << "  " << std::left << std::setw(31) << entry.name;
            SetTone(Tone::Muted);
            std::cout << " = ";
            if (entry.value == 0) {
                SetTone(Tone::Error);
                std::cout << Localized("FALTANTE", "MISSING");
            } else {
                SetTone(OffsetsAreValidated(status) ? Tone::Success : Tone::Warning);
                std::cout << "0x" << std::uppercase << std::hex << entry.value
                    << std::nouppercase << std::dec;
            }
            std::cout << '\n';
        }
        SetTone(Tone::Default);
        RenderPromptLocked();
    }

    void PrintStatus() {
        const OffsetUpdateStatus& offsets = GetOffsetUpdateStatus();
        const InventoryChangerStatus& inventory = GetInventoryChangerStatus();
        std::ostringstream process;
        process << "PID " << mem.pid << " | client.dll 0x" << std::hex << mem.clientModule;
        WriteStatus("CS2", process.str(), mem.pid != 0 && mem.clientModule != 0);

        std::ostringstream offsetDetail;
        offsetDetail << (offsets.build.empty()
                ? Localized("build desconocida", "unknown build")
                : "build " + offsets.build)
            << Localized(" | globales ", " | globals ") << offsets.globalOffsetsFound
            << " | schema " << offsets.schemaOffsetsFound;
        WriteStatus("Offsets", offsetDetail.str(), OffsetsAreValidated(offsets));

        std::ostringstream inventoryDetail;
        inventoryDetail << BackendName(inventory.backend)
            << " | bridge " << (inventory.bridgeActive ? "online" : "offline");
        WriteStatus(Localized("Inventario", "Inventory"), inventoryDetail.str(),
            inventory.backend != InventoryRuntimeBackend::Disabled);
    }

    void ClearConsole() {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (!GetConsoleScreenBufferInfo(g_output, &info)) return;
        const DWORD cells = static_cast<DWORD>(info.dwSize.X) * info.dwSize.Y;
        DWORD written = 0;
        const COORD home{ 0, 0 };
        FillConsoleOutputCharacterA(g_output, ' ', cells, home, &written);
        FillConsoleOutputAttribute(g_output, g_defaultAttributes, cells, home, &written);
        SetConsoleCursorPosition(g_output, home);
        g_promptVisible = false;
        RenderPromptLocked();
    }

    void ExecuteCommand(const std::string& rawCommand) {
        const std::string command = LowerAndTrim(rawCommand);
        if (command.empty()) return;
        if (command == "help" || command == "ayuda" || command == "?") {
            WriteLine(Localized("Comandos disponibles", "Available commands"), Tone::Accent);
            WriteLine(Localized("  status / estado        Estado de CS2, offsets e inventario",
                "  status / estado        CS2, offsets and inventory status"));
            WriteLine(Localized("  offsets                Todos los offsets cargados",
                "  offsets                All loaded offsets"));
            WriteLine(Localized("  offsets core           Entidades, camara y pawn",
                "  offsets core           Entities, camera and pawn"));
            WriteLine(Localized("  offsets inventory      Inventario, skins y StatTrak",
                "  offsets inventory      Inventory, skins and StatTrak"));
            WriteLine(Localized("  offsets visuals        Glow, flash, smoke y scope",
                "  offsets visuals        Glow, flash, smoke and scope"));
            WriteLine(Localized("  validator / validador  Revisar/ejecutar A2x cs2-dumper",
                "  validator / validador  Check/run A2x cs2-dumper"));
            WriteLine(Localized("  language es/en         Cambiar idioma de Overlay y consola",
                "  language es/en         Change Overlay and console language"));
            WriteLine(Localized("  clear / limpiar        Limpiar la consola",
                "  clear / limpiar        Clear the console"));
            WriteLine(Localized("  exit / panic           Cierre limpio de OverlayAI",
                "  exit / panic           Clean OverlayAI shutdown"));
        } else if (command == "language es" || command == "idioma es") {
            SetUiLanguage(UiLanguage::Spanish);
            WriteLine("Idioma cambiado a Espanol.", Tone::Success);
        } else if (command == "language en" || command == "idioma en") {
            SetUiLanguage(UiLanguage::English);
            WriteLine("Language changed to English.", Tone::Success);
        } else if (command == "language" || command == "idioma") {
            WriteLine(Localized("Idioma actual: Espanol.",
                "Current language: English."), Tone::Info);
        } else if (command == "status" || command == "estado") {
            PrintStatus();
        } else if (command == "validator" || command == "validador") {
            if (EnsureDumperAvailableInteractive()) {
                WriteLine(Localized(
                    "Ejecutando A2x cs2-dumper y validando su salida...",
                    "Running A2x cs2-dumper and validating its output..."), Tone::Info);
                (void)RunOffsetAutoUpdate();
                ConsoleUi::ReportOffsetsLoaded();
            }
        } else if (command.rfind("offsets", 0) == 0) {
            PrintOffsets(command);
        } else if (command == "clear" || command == "cls" || command == "limpiar") {
            ClearConsole();
        } else if (command == "exit" || command == "quit" || command == "panic") {
            WriteLine(Localized("Cierre limpio solicitado desde la consola.",
                "Clean shutdown requested from the console."), Tone::Warning);
            g_shutdownRequested.store(true);
        } else {
            WriteLine(Localized(
                "Comando desconocido. Escribe ayuda para ver las opciones.",
                "Unknown command. Type help to see the options."), Tone::Warning);
        }
    }
}

namespace ConsoleUi {
    void Initialize() {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        SetInventoryConsoleSink(&WriteInventoryLogLine);
        g_output = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(g_output, &info))
            g_defaultAttributes = info.wAttributes;

        SetConsoleTitleW(L"OverlayAI Runtime");
        const HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_OVERLAYAI_APP));
        const HWND consoleWindow = GetConsoleWindow();
        if (icon && consoleWindow) {
            SendMessageW(consoleWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            SendMessageW(consoleWindow, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        }

        SetTone(Tone::Accent);
        std::cout << "\n  OVERLAYAI // RUNTIME\n";
        SetTone(Tone::Muted);
        std::cout << Localized("  Consola de estado y diagnostico\n\n",
            "  Status and diagnostics console\n\n");
        SetTone(Tone::Default);
    }

    void ReportWaitingForGame() {
        WriteStatus("CS2", Localized("esperando cs2.exe", "waiting for cs2.exe"), false);
    }

    void ReportGameAttached(uint32_t pid, uintptr_t clientModule) {
        std::ostringstream detail;
        detail << "PID " << pid << " | client.dll 0x" << std::hex << clientModule;
        WriteStatus("CS2", detail.str(), pid != 0 && clientModule != 0);
    }

    void ReportOffsetsLoaded() {
        const OffsetUpdateStatus& status = GetOffsetUpdateStatus();
        std::ostringstream detail;
        detail << (status.build.empty()
                ? Localized("build desconocida", "unknown build")
                : "build " + status.build)
            << " | " << status.globalOffsetsFound << Localized(" globales", " globals")
            << " | " << status.schemaOffsetsFound << " schema";
        WriteStatus("Offsets", detail.str(), OffsetsAreValidated(status));
    }

    void ReportIpcStarted(bool started) {
        WriteStatus("Inventory IPC", started
            ? Localized("servidor listo", "server ready")
            : Localized("no pudo iniciarse", "failed to start"), started);
    }

    void ReportValidatorStatus(bool ok, const char* detail) {
        WriteStatus("Validator", detail ? detail : "", ok);
    }

    bool PromptValidatorYesNo(const char* question) {
        for (;;) {
            std::lock_guard<std::mutex> lock(g_consoleMutex);
            ClearPromptLocked();
            SetTone(Tone::Accent);
            std::cout << "[Validator] ";
            SetTone(Tone::Default);
            std::cout << (question ? question : "") << " [Y/N]: " << std::flush;
            std::string answer;
            if (!std::getline(std::cin, answer)) return false;
            answer = LowerAndTrim(answer);
            if (answer == "y" || answer == "yes" || answer == "s" || answer == "si")
                return true;
            if (answer == "n" || answer == "no") return false;
            SetTone(Tone::Warning);
            std::cout << Localized("Responde Y o N.\n", "Answer Y or N.\n");
            SetTone(Tone::Default);
        }
    }

    std::string PromptValidatorText(const char* question) {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        ClearPromptLocked();
        SetTone(Tone::Accent);
        std::cout << "[Validator] ";
        SetTone(Tone::Default);
        std::cout << (question ? question : "") << ": " << std::flush;
        std::string answer;
        (void)std::getline(std::cin, answer);
        return answer;
    }

    void BeginInteractiveMode() {
        WriteLine(Localized("Runtime activo. Escribe ayuda para ver los comandos.",
            "Runtime active. Type help to see commands."), Tone::Muted);
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        g_interactive = true;
        RenderPromptLocked();
    }

    void PollInput() {
        while (_kbhit()) {
            const int key = _getch();
            if (key == 0 || key == 224) {
                if (_kbhit()) (void)_getch();
                continue;
            }
            if (key == '\r') {
                std::string command;
                {
                    std::lock_guard<std::mutex> lock(g_consoleMutex);
                    std::cout << '\n';
                    command = g_input;
                    g_input.clear();
                    g_promptVisible = false;
                }
                ExecuteCommand(command);
                std::lock_guard<std::mutex> lock(g_consoleMutex);
                if (!g_promptVisible) RenderPromptLocked();
            } else if (key == '\b') {
                std::lock_guard<std::mutex> lock(g_consoleMutex);
                if (!g_input.empty()) {
                    g_input.pop_back();
                    std::cout << "\b \b" << std::flush;
                }
            } else if (key >= 32 && key <= 126) {
                std::lock_guard<std::mutex> lock(g_consoleMutex);
                if (g_input.size() < 96) {
                    g_input.push_back(static_cast<char>(key));
                    std::cout << static_cast<char>(key) << std::flush;
                }
            }
        }
    }

    bool IsShutdownRequested() {
        return g_shutdownRequested.load();
    }

    void WriteInventoryLogLine(
        InventoryLogCategory category,
        InventoryLogLevel level,
        const char* message) {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        ClearPromptLocked();
        SetTone(Tone::Muted);
        std::cout << "[IPC][";
        if (level == InventoryLogLevel::Error) SetTone(Tone::Error);
        else if (level == InventoryLogLevel::Warning) SetTone(Tone::Warning);
        else if (category == InventoryLogCategory::Action) SetTone(Tone::Success);
        else SetTone(Tone::Info);
        std::cout << GetInventoryLogCategoryName(category);
        SetTone(Tone::Muted);
        std::cout << "] ";
        SetTone(level == InventoryLogLevel::Error ? Tone::Error : Tone::Default);
        std::cout << (message ? message : "") << '\n';
        SetTone(Tone::Default);
        RenderPromptLocked();
    }
}
