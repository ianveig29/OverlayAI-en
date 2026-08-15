#pragma once

#include <cstdint>

struct GlowDiagnostics {
    bool enabled = false;
    int activePawns = 0;
    uint64_t samples = 0;
    uint64_t gameDisabledGlow = 0;
    uint64_t alteredProperties = 0;
    uint64_t readFailures = 0;
    uint64_t writeFailures = 0;
    uint64_t snapshotGaps = 0;
    uintptr_t lastAffectedPawn = 0;
    float lastGlowTime = 0.0f;
    float lastGlowStartTime = 0.0f;
    bool lastFlashing = false;
    bool lastEligible = false;
};

void RunGlow();
void ShutdownGlow();
void SetGlowDiagnosticsEnabled(bool enabled);
void ResetGlowDiagnostics();
const GlowDiagnostics& GetGlowDiagnostics();
