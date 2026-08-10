#pragma once

// ============================================================
// OtherGlow.h
// Object glow function declarations.
// ============================================================

#include "Skeleton.h"
#include <d3d11.h>

bool InitializeOtherGlow(ID3D11Device* device, ID3D11DeviceContext* context);
void ShutdownOtherGlow();
void ReleaseOtherGlowRenderTargets();
void BeginOtherGlowFrame(int screenWidth, int screenHeight);
void SubmitOtherGlowPose(const SkeletonPose& pose, const Matrix4x4& viewMatrix,
    int screenWidth, int screenHeight, ImU32 color, float bodyHeight, float bodyScale);
void RenderOtherGlow(ID3D11RenderTargetView* target, float softness, int qualityPasses);
