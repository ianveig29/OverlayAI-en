#pragma once

#include <cstdint>
#include <string>

struct ModelDiagnostics {
    bool enabled = false;
    uint64_t samples = 0;
    uint64_t readFailures = 0;
    uintptr_t pawn = 0;
    uintptr_t sceneNode = 0;
    uintptr_t modelHandle = 0;
    uintptr_t modelData = 0;
    int hitboxSet = -1;
    int validCandidates = 0;
    int modelBoneCount = 0;
    int externalPartCount = 0;
    uintptr_t hitboxListRoot = 0;
    int hitboxListOffset = -1;
    int modelHitboxSetCount = 0;
    int modelHitboxCount = 0;
    std::string firstHitboxBone;
    bool boneRotationValid = false;
    float boneRotationNorm = 0.0f;
    std::string modelName;
    std::string resourceName;
};

void SetModelDiagnosticsEnabled(bool enabled);
void ResetModelDiagnostics();
void UpdateModelDiagnostics();
const ModelDiagnostics& GetModelDiagnostics();
