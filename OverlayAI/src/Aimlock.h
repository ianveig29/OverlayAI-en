#pragma once

// ============================================================
// Aimlock.h
// Aimlock function declarations.
// ============================================================

#include "Types.h"

void PollAimKeyBind();
void RunAimlock(int screenWidth, int screenHeight);
float GetAimFovDegreesForScope(int scopeLevel);
float CalculateAimFovRadiusPixels(const Matrix4x4& viewMatrix,
    int screenWidth, int screenHeight, float fovDegrees);
