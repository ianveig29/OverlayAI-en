#include "BridgeRuntimeLogMonitor.h"

#include "InventoryLog.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <array>

namespace {
    constexpr ULONGLONG kPollIntervalMs = 250;
    constexpr DWORD kMaxReadBytes = 64 * 1024;

    bool g_initialized = false;
    uint64_t g_fileId = 0;
    uint64_t g_offset = 0;
    ULONGLONG g_nextPollAt = 0;
    std::string g_pendingLine;
    std::array<char, kMaxReadBytes> g_readBuffer{};

    uint64_t GetFileId(const BY_HANDLE_FILE_INFORMATION& info) {
        return (static_cast<uint64_t>(info.dwVolumeSerialNumber) << 32) ^
            (static_cast<uint64_t>(info.nFileIndexHigh) << 32) ^
            info.nFileIndexLow;
    }

    bool ShouldForward(const std::string& line) {
        // Suppress repetitive low-level weapon hook trace spam
        if (line.find("Weapon skin midpoint") != std::string::npos ||
            line.find("Weapon skin material refresh") != std::string::npos ||
            line.find("Weapon skin HUD:") != std::string::npos ||
            line.find("Weapon skin lifecycle:") != std::string::npos ||
            line.find("Knife identity:") != std::string::npos) {
            return false;
        }

        constexpr const char* prefixes[] = {
            "InventoryBridge",
            "Weapon skin",
            "Weapon StatTrak",
            "StatTrak",
            "SOCache item",
            "Native weapon loadout",
            "Glove SOCache",
            "Music Kit SOCache"
        };
        for (const char* prefix : prefixes) {
            if (line.rfind(prefix, 0) == 0) return true;
        }
        return false;
    }

    InventoryLogLevel GetLineLevel(const std::string& line) {
        if (line.find("faulted") != std::string::npos ||
            line.find("excepcion") != std::string::npos ||
            line.find("fallo") != std::string::npos ||
            line.find("missing") != std::string::npos ||
            line.find("resolved=no") != std::string::npos ||
            line.find("count=0") != std::string::npos)
            return InventoryLogLevel::Warning;
        return InventoryLogLevel::Info;
    }

    void ForwardCompleteLines() {
        std::size_t newline = 0;
        while ((newline = g_pendingLine.find('\n')) != std::string::npos) {
            std::string line = g_pendingLine.substr(0, newline);
            g_pendingLine.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && ShouldForward(line)) {
                WriteInventoryLog(InventoryLogCategory::Action,
                    GetLineLevel(line), "Bridge | %s", line.c_str());
            }
        }
        if (g_pendingLine.size() > kMaxReadBytes)
            g_pendingLine.erase(0, g_pendingLine.size() - kMaxReadBytes);
    }
}

void PumpBridgeRuntimeLog() {
    const ULONGLONG now = GetTickCount64();
    if (now < g_nextPollAt) return;
    g_nextPollAt = now + kPollIntervalMs;

    wchar_t tempPath[MAX_PATH]{};
    if (!GetTempPathW(_countof(tempPath), tempPath)) return;
    wchar_t logPath[MAX_PATH]{};
    lstrcpynW(logPath, tempPath, _countof(logPath));
    lstrcatW(logPath, L"OverlayAI.InventoryBridge.log");

    HANDLE file = CreateFileW(logPath, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file, &info) ||
        !GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        CloseHandle(file);
        return;
    }

    const uint64_t fileId = GetFileId(info);
    const uint64_t fileSize = static_cast<uint64_t>(size.QuadPart);
    if (!g_initialized) {
        g_initialized = true;
        g_fileId = fileId;
        g_offset = fileSize;
        CloseHandle(file);
        return;
    }
    if (fileId != g_fileId || fileSize < g_offset) {
        g_fileId = fileId;
        g_offset = 0;
        g_pendingLine.clear();
    }
    if (fileSize == g_offset) {
        CloseHandle(file);
        return;
    }

    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(g_offset);
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
        CloseHandle(file);
        return;
    }

    const uint64_t remaining = fileSize - g_offset;
    const DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(
        remaining, kMaxReadBytes));
    DWORD bytesRead = 0;
    const BOOL read = ReadFile(file, g_readBuffer.data(), toRead, &bytesRead, nullptr);
    CloseHandle(file);
    if (!read || bytesRead == 0) return;

    g_offset += bytesRead;
    g_pendingLine.append(g_readBuffer.data(), bytesRead);
    ForwardCompleteLines();
}

void ResetBridgeRuntimeLogMonitor() {
    g_initialized = false;
    g_fileId = 0;
    g_offset = 0;
    g_nextPollAt = 0;
    g_pendingLine.clear();
}
