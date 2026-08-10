// ============================================================
// WorldTransform.cpp
// Converts 3D world coordinates to 2D screen coordinates. Essential for drawing ESP.
// ============================================================

#include "WorldTransform.h"
#include "Memory.h"
#include "Offsets.h"
#include <cmath>

// Converts a 3D world position to 2D screen coordinates
bool WorldToScreen(const Vector3& world, Vector3& screen, const Matrix4x4& matrix, int width, int height,
    int* modeOut, float* clipOut, float* ndcXOut, float* ndcYOut)
{
    const float clipW = world.x * matrix.m[3][0] + world.y * matrix.m[3][1] +
        world.z * matrix.m[3][2] + matrix.m[3][3];
    const float clipX = world.x * matrix.m[0][0] + world.y * matrix.m[0][1] +
        world.z * matrix.m[0][2] + matrix.m[0][3];
    const float clipY = world.x * matrix.m[1][0] + world.y * matrix.m[1][1] +
        world.z * matrix.m[1][2] + matrix.m[1][3];

    // dwViewMatrix uses this layout. Trying the transposed layout per point made
    // entities behind the camera reappear as axis-dependent ghost projections.
    if (!std::isfinite(clipW) || !std::isfinite(clipX) || !std::isfinite(clipY) || clipW <= 0.01f)
        return false;

    float ndcX = clipX / clipW;
    float ndcY = clipY / clipW;
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) return false;

    screen.x = (width / 2.0f) + (ndcX * (width / 2.0f));
    screen.y = (height / 2.0f) - (ndcY * (height / 2.0f));

    if (modeOut) *modeOut = 0;
    if (clipOut) *clipOut = clipW;
    if (ndcXOut) *ndcXOut = ndcX;
    if (ndcYOut) *ndcYOut = ndcY;

    if (screen.x < -2000 || screen.x > width + 2000 || screen.y < -2000 || screen.y > height + 2000) return false;
    return true;
}

bool ReadViewMatrix(Matrix4x4& out) {
    Matrix4x4 m = mem.Read<Matrix4x4>(mem.clientModule + Offsets::dwViewMatrix);
    if (std::abs(m.m[0][0]) > 1e-5f && std::abs(m.m[3][3]) > 1e-5f) {
        out = m;
        return true;
    }
    m = mem.Read<Matrix4x4>(mem.clientModule + Offsets::dwViewRender);
    if (std::abs(m.m[0][0]) > 1e-5f && std::abs(m.m[3][3]) > 1e-5f) {
        out = m;
        return true;
    }
    return false;
}
