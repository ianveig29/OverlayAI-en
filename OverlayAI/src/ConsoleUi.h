#pragma once

#include "InventoryLog.h"

#include <cstdint>
#include <string>

namespace ConsoleUi {
    void Initialize();
    void ReportWaitingForGame();
    void ReportGameAttached(uint32_t pid, uintptr_t clientModule);
    void ReportOffsetsLoaded();
    void ReportIpcStarted(bool started);
    void ReportValidatorStatus(bool ok, const char* detail);
    bool PromptValidatorYesNo(const char* question);
    std::string PromptValidatorText(const char* question);
    void BeginInteractiveMode();
    void PollInput();
    bool IsShutdownRequested();
    void WriteInventoryLogLine(
        InventoryLogCategory category,
        InventoryLogLevel level,
        const char* message);
}
