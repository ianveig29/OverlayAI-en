#pragma once

// Locate, validate, and apply the client smoke-render patch.
bool RunAntiSmoke();

// Restore the exact instruction byte changed by RunAntiSmoke.
void RestoreAntiSmoke();

bool IsAntiSmokeActive();

// Returns smoke overlay alpha (0.0 - 1.0)
float GetLocalSmokeOverlayAlpha();

// Convenience: is local inside smoke using threshold (default 0.5)
bool IsLocalInSmoke(float threshold = 0.5f);
