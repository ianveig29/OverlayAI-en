#pragma once

// ============================================================
// AntiFlash.h
// Anti-flash function declarations.
// ============================================================

#include <cstdint>

// Returns flash overlay alpha for local player (0.0 - 1.0 or higher depending on engine)
float GetLocalFlashOverlayAlpha();

// Capture the real flash state before Anti Flash neutralizes visual fields.
void UpdateFlashState();

// Convenience: is local player considered flashed using threshold (default 0.5)
bool IsLocalFlashed(float threshold = 0.5f);

// Limit the local flash visual while preserving its gameplay duration/state.
void RunAntiFlash(float opacityPercent);

// Restore the exact transient values captured before RunAntiFlash changed them.
void RestoreAntiFlashOverrides();
