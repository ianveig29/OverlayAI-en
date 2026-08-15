#pragma once

#include "Types.h"

bool WorldToScreen(const Vector3& world, Vector3& screen, const Matrix4x4& matrix, int width, int height,
    int* modeOut = nullptr, float* clipOut = nullptr, float* ndcXOut = nullptr, float* ndcYOut = nullptr);
bool ReadViewMatrix(Matrix4x4& out);
