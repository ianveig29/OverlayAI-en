#include "AntiFlash.h"
#include "Entity.h"
#include "Memory.h"
#include "Offsets.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
    struct FlashOverrideState {
        uintptr_t pawn = 0;
        float maxAlpha = 0.0f;
        bool captured = false;
    };

    std::mutex g_flashMutex;
    FlashOverrideState g_override;
    float g_observedFlashAlpha = 0.0f;
    uintptr_t g_observedPawn = 0;
    ULONGLONG g_flashUntilMs = 0;
    ULONGLONG g_lastObservationMs = 0;

    uintptr_t GetLocalPawnFromSnapshot() {
        const uintptr_t pawn = GetCurrentFrameSnapshot().localPawn;
        if (IsValidPtr(pawn)) return pawn;
        const uintptr_t directPawn = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerPawn);
        return IsValidPtr(directPawn) ? directPawn : 0;
    }

    float ReadFiniteFloat(uintptr_t address) {
        const float value = mem.Read<float>(address);
        return std::isfinite(value) ? value : 0.0f;
    }

    void CaptureOverrideState(uintptr_t pawn) {
        g_override = {};
        g_override.pawn = pawn;
        const float current = ReadFiniteFloat(pawn + Offsets::m_flFlashMaxAlpha);
        g_override.maxAlpha = (current > 0.0f && current <= 255.0f) ? current : 255.0f;
        g_override.captured = true;
    }

    void RestoreCapturedState(uintptr_t currentPawn) {
        if (!g_override.captured) return;

        // Never write through an old pawn pointer after death, respawn, or a
        // transition to spectator/bot control.
        if (g_override.pawn == currentPawn && IsValidPtr(currentPawn)) {
            (void)mem.Write<float>(currentPawn + Offsets::m_flFlashMaxAlpha, g_override.maxAlpha);
        }
        g_override = {};
    }
}

void UpdateFlashState() {
    if (!mem.clientModule) return;
    const uintptr_t pawn = GetLocalPawnFromSnapshot();
    const ULONGLONG nowMs = GetTickCount64();

    float overlayAlpha = 0.0f;
    float duration = 0.0f;
    if (IsValidPtr(pawn)) {
        overlayAlpha = (std::max)(0.0f, ReadFiniteFloat(pawn + Offsets::m_flFlashOverlayAlpha));
        duration = (std::max)(0.0f, ReadFiniteFloat(pawn + Offsets::m_flFlashDuration));
    }

    std::lock_guard<std::mutex> lock(g_flashMutex);
    if (pawn != g_observedPawn) {
        g_observedPawn = pawn;
        g_flashUntilMs = 0;
    }
    g_observedFlashAlpha = overlayAlpha;
    g_lastObservationMs = nowMs;
    if (duration > 0.0f && duration <= 10.0f) {
        const ULONGLONG observedEnd = nowMs + static_cast<ULONGLONG>(duration * 1000.0f);
        g_flashUntilMs = (std::max)(g_flashUntilMs, observedEnd);
    }
    if (!IsValidPtr(pawn)) g_flashUntilMs = 0;
}

float GetLocalFlashOverlayAlpha() {
    const ULONGLONG nowMs = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_flashMutex);
        if (g_lastObservationMs != 0 && nowMs - g_lastObservationMs < 100)
            return g_observedFlashAlpha;
    }
    UpdateFlashState();
    std::lock_guard<std::mutex> lock(g_flashMutex);
    return g_observedFlashAlpha;
}

bool IsLocalFlashed(float threshold) {
    const float alpha = GetLocalFlashOverlayAlpha();
    std::lock_guard<std::mutex> lock(g_flashMutex);
    return alpha > threshold || GetTickCount64() < g_flashUntilMs;
}

void RunAntiFlash(float opacityPercent) {
    if (!mem.hProcess || !mem.clientModule) return;
    const uintptr_t pawn = GetLocalPawnFromSnapshot();
    if (!IsValidPtr(pawn)) return;

    std::lock_guard<std::mutex> lock(g_flashMutex);
    if (g_override.captured && g_override.pawn != pawn)
        g_override = {};
    if (!g_override.captured)
        CaptureOverrideState(pawn);

    const float clampedPercent = (std::clamp)(opacityPercent, 0.0f, 100.0f);
    const float retained = clampedPercent / 100.0f;
    const float targetMaxAlpha = 255.0f * retained;
    const float currentMaxAlpha = ReadFiniteFloat(pawn + Offsets::m_flFlashMaxAlpha);

    if (std::fabs(currentMaxAlpha - targetMaxAlpha) > 0.01f)
        (void)mem.Write<float>(pawn + Offsets::m_flFlashMaxAlpha, targetMaxAlpha);

    if (clampedPercent <= 0.01f) {
        const float currentDuration = ReadFiniteFloat(pawn + Offsets::m_flFlashDuration);
        if (currentDuration > 0.0f)
            (void)mem.Write<float>(pawn + Offsets::m_flFlashDuration, 0.0f);
    }
}

void RestoreAntiFlashOverrides() {
    if (!mem.hProcess || !mem.clientModule) return;
    const uintptr_t currentPawn = GetLocalPawnFromSnapshot();
    std::lock_guard<std::mutex> lock(g_flashMutex);
    RestoreCapturedState(currentPawn);
}
