// ============================================================
// InventoryLog.cpp
// Logging system for the inventory module. Records which items are applied and errors.
// ============================================================

#include "InventoryLog.h"

#include <windows.h>

#include <cstdarg>
#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>

namespace {
    constexpr std::size_t kMaxInventoryLogEntries = 128;
    std::mutex g_logMutex;
    std::deque<InventoryLogEntry> g_logEntries;
    std::atomic<InventoryConsoleSink> g_consoleSink = nullptr;
}

const char* GetInventoryLogCategoryName(InventoryLogCategory category) {
    switch (category) {
    case InventoryLogCategory::Transport: return "Transport";
    case InventoryLogCategory::Protocol: return "Protocol";
    case InventoryLogCategory::Action: return "Action";
    default: return "Unknown";
    }
}

void WriteInventoryLog(
    InventoryLogCategory category, InventoryLogLevel level,
    const char* format, ...) {
    char message[256]{};
    va_list arguments;
    va_start(arguments, format);
    vsnprintf_s(message, sizeof(message), _TRUNCATE, format ? format : "", arguments);
    va_end(arguments);

    InventoryLogEntry entry;
    entry.timestampMs = GetTickCount64();
    entry.category = category;
    entry.level = level;
    entry.message = message;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logEntries.push_back(entry);
        if (g_logEntries.size() > kMaxInventoryLogEntries)
            g_logEntries.pop_front();
    }
    const InventoryConsoleSink sink = g_consoleSink.load();
    if (sink) {
        sink(category, level, message);
    } else {
        std::printf("[InventoryIPC][%s] %s\n",
            GetInventoryLogCategoryName(category), message);
    }
}

void SetInventoryConsoleSink(InventoryConsoleSink sink) {
    g_consoleSink.store(sink);
}

std::vector<InventoryLogEntry> GetInventoryLogSnapshot() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return { g_logEntries.begin(), g_logEntries.end() };
}
