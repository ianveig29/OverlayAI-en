#include "BridgeLogging.h"

#include <windows.h>

namespace {
    constexpr wchar_t kBridgeLogFileName[] = L"OverlayAI.InventoryBridge.log";
    constexpr char kLineEnding[] = "\r\n";
    constexpr DWORD kCombinedLineCapacity = 4096;

    SRWLOCK g_logLock = SRWLOCK_INIT;
    HANDLE g_logFile = INVALID_HANDLE_VALUE;
    wchar_t g_logPath[MAX_PATH]{};
    bool g_logPathInitialized = false;

    bool EnsureLogPathLocked() noexcept {
        if (g_logPathInitialized)
            return g_logPath[0] != L'\0';

        g_logPathInitialized = true;
        wchar_t tempPath[MAX_PATH]{};
        const DWORD tempLength = GetTempPathW(_countof(tempPath), tempPath);
        if (tempLength == 0 || tempLength >= _countof(tempPath))
            return false;

        if (lstrlenW(tempPath) + lstrlenW(kBridgeLogFileName) >= MAX_PATH)
            return false;

        lstrcpynW(g_logPath, tempPath, _countof(g_logPath));
        lstrcatW(g_logPath, kBridgeLogFileName);
        return true;
    }

    constexpr DWORD kMaxLogSizeBytes = 512 * 1024; // Cap at 512 KB

    bool EnsureLogFileLocked() noexcept {
        if (g_logFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size{};
            if (GetFileSizeEx(g_logFile, &size) && size.QuadPart > kMaxLogSizeBytes) {
                SetFilePointer(g_logFile, 0, nullptr, FILE_BEGIN);
                SetEndOfFile(g_logFile);
            }
            return true;
        }
        if (!EnsureLogPathLocked())
            return false;

        // Truncate on each fresh injection session so old logs don't accumulate indefinitely
        g_logFile = CreateFileW(g_logPath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return g_logFile != INVALID_HANDLE_VALUE;
    }

    bool WriteBytesLocked(const void* data, DWORD size) noexcept {
        if (!data || size == 0)
            return true;
        if (!EnsureLogFileLocked())
            return false;

        DWORD written = 0;
        return WriteFile(g_logFile, data, size, &written, nullptr) &&
            written == size;
    }

    bool WriteLineLocked(const char* text, DWORD textLength) noexcept {
        if (!EnsureLogFileLocked())
            return false;

        // Most diagnostic messages are short. Combining the payload and CRLF
        // into one write avoids an extra kernel transition for every log line.
        // Preserve the old two-write path for unusually large payloads such as
        // a complete inventory snapshot.
        if (textLength + (sizeof(kLineEnding) - 1) <= kCombinedLineCapacity) {
            char line[kCombinedLineCapacity]{};
            CopyMemory(line, text, textLength);
            CopyMemory(line + textLength, kLineEnding,
                sizeof(kLineEnding) - 1);
            return WriteBytesLocked(line,
                textLength + static_cast<DWORD>(sizeof(kLineEnding) - 1));
        }

        return WriteBytesLocked(text, textLength) &&
            WriteBytesLocked(kLineEnding,
                static_cast<DWORD>(sizeof(kLineEnding) - 1));
    }

    void ResetLogFileLocked() noexcept {
        if (g_logFile != INVALID_HANDLE_VALUE) {
            CloseHandle(g_logFile);
            g_logFile = INVALID_HANDLE_VALUE;
        }
    }
}

void AppendLog(const char* text) noexcept {
    if (!text) return;

    const DWORD textLength = static_cast<DWORD>(lstrlenA(text));
    AcquireSRWLockExclusive(&g_logLock);

    // Keep the append handle open across log calls. The old implementation did
    // GetTempPath/CreateFile/two writes/CloseHandle for every single diagnostic
    // line, which is especially costly from hooks running inside the game.
    if (!WriteLineLocked(text, textLength)) {
        // A stale/invalidated handle is uncommon, but recover once without
        // changing the caller-visible fire-and-forget logging contract.
        ResetLogFileLocked();
        (void)WriteLineLocked(text, textLength);
    }

    ReleaseSRWLockExclusive(&g_logLock);
}
