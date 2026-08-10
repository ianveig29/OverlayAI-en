#pragma once

// ============================================================
// WorldTransform.h
// Coordinate transform function declarations.
// ============================================================

#include "Types.h"

// Converts a 3D world position to 2D screen coordinates
bool WorldToScreen(const Vector3& world, Vector3& screen, const Matrix4x4& matrix, int width, int height,
    int* modeOut = nullptr, float* clipOut = nullptr, float* ndcXOut = nullptr, float* ndcYOut = nullptr);
bool ReadViewMatrix(Matrix4x4& out);
