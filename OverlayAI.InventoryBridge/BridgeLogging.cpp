// ============================================================
// BridgeLogging.cpp
// Logging system specific to the Inventory Bridge.
// ============================================================

#include "BridgeLogging.h"

#include <windows.h>

void AppendLog(const char* text) noexcept {
    if (!text) return;

    wchar_t tempPath[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempPath)) return;

    wchar_t logPath[MAX_PATH]{};
    lstrcpynW(logPath, tempPath, MAX_PATH);
    lstrcatW(logPath, L"OverlayAI.InventoryBridge.log");

    HANDLE file = CreateFileW(logPath, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}

