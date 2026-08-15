#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class InventoryLogCategory {
    Transport,
    Protocol,
    Action
};

enum class InventoryLogLevel {
    Info,
    Warning,
    Error
};

struct InventoryLogEntry {
    uint64_t timestampMs = 0;
    InventoryLogCategory category = InventoryLogCategory::Transport;
    InventoryLogLevel level = InventoryLogLevel::Info;
    std::string message;
};

using InventoryConsoleSink = void(*)(
    InventoryLogCategory category,
    InventoryLogLevel level,
    const char* message);

void WriteInventoryLog(
    InventoryLogCategory category, InventoryLogLevel level,
    const char* format, ...);
void SetInventoryConsoleSink(InventoryConsoleSink sink);
std::vector<InventoryLogEntry> GetInventoryLogSnapshot();
const char* GetInventoryLogCategoryName(InventoryLogCategory category);
