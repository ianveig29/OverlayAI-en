#pragma once

// ============================================================
// SmokeColor.h
// Smoke color function declarations.
// ============================================================

#include "Types.h"

void UpdateSmokeColors(bool enabled, const Vector3& rgb255);
void RestoreSmokeColors();

int GetDetectedSmokeProjectileCount();
int GetTintedSmokeProjectileCount();
