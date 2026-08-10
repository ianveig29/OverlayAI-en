#pragma once

#include <cstdint>

struct BhopTelemetry {
    float horizontalSpeed = 0.0f;
    float verticalSpeed = 0.0f;
    uint32_t successfulJumps = 0;
    uint32_t groundRetries = 0;
    int jumpResponseMs = -1;
    int strafeDirection = 0;
    bool onGround = false;
    bool valid = false;
};

void RunBhop();
void PollBhopKeyBind();
void ShutdownBhop();
BhopTelemetry GetBhopTelemetry();
