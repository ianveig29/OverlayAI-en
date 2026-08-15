#include "Bhop.h"

#include "Config.h"
#include "Memory.h"
#include "Offsets.h"
#include "Stats.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>
#include <windows.h>

namespace {
    constexpr uint32_t FL_ONGROUND = 1;
    constexpr ULONGLONG kJumpPulseMs = 10;
    constexpr ULONGLONG kGroundRetryMs = 32;
    constexpr ULONGLONG kPawnRefreshMs = 100;
    constexpr DWORD kActivePollMs = 2;
    constexpr DWORD kIdlePollMs = 10;

    std::array<std::atomic_bool, 256> g_physicalKeyDown{};
    HHOOK g_keyboardHook = nullptr;

    std::atomic_bool g_workerStop{ false };
    std::atomic_bool g_workerStarted{ false };
    std::atomic_bool g_enabled{ false };
    std::atomic_bool g_requireHoldKey{ true };
    std::atomic_int g_holdKeyVk{ VK_SPACE };
    std::atomic_bool g_strafeAssist{ false };
    std::atomic_bool g_menuOpen{ false };
    std::atomic_bool g_gameInputActive{ false };
    std::thread g_worker;

    std::atomic<float> g_horizontalSpeed{ 0.0f };
    std::atomic<float> g_verticalSpeed{ 0.0f };
    std::atomic_uint32_t g_successfulJumps{ 0 };
    std::atomic_uint32_t g_groundRetries{ 0 };
    std::atomic_int g_jumpResponseMs{ -1 };
    std::atomic_int g_strafeDirection{ 0 };
    std::atomic_bool g_onGround{ false };
    std::atomic_bool g_telemetryValid{ false };

    bool IsMouseVirtualKey(int vk) {
        return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
            vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
    }

    LRESULT CALLBACK PhysicalKeyboardHook(int code, WPARAM wParam, LPARAM lParam) {
        if (code == HC_ACTION && lParam) {
            const KBDLLHOOKSTRUCT* event = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
            if (event->vkCode < g_physicalKeyDown.size() &&
                (event->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) == 0) {
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
                    g_physicalKeyDown[event->vkCode].store(true);
                else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
                    g_physicalKeyDown[event->vkCode].store(false);
            }
        }
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    }

    void EnsureKeyboardHook() {
        if (g_keyboardHook) return;
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, PhysicalKeyboardHook,
            GetModuleHandleW(nullptr), 0);
        if (!g_keyboardHook) return;

        for (int vk = 0; vk < static_cast<int>(g_physicalKeyDown.size()); ++vk) {
            if (!IsMouseVirtualKey(vk))
                g_physicalKeyDown[vk].store((GetAsyncKeyState(vk) & 0x8000) != 0);
        }
    }

    bool IsGameInputActive() {
        const HWND foregroundWindow = GetForegroundWindow();
        if (!foregroundWindow || IsIconic(foregroundWindow) || !IsWindowVisible(foregroundWindow))
            return false;

        DWORD foregroundPid = 0;
        GetWindowThreadProcessId(foregroundWindow, &foregroundPid);
        return foregroundPid == mem.pid;
    }

    bool IsActivationHeld() {
        if (!g_requireHoldKey.load()) return true;
        const int vk = g_holdKeyVk.load();
        if (vk <= 0 || vk >= static_cast<int>(g_physicalKeyDown.size())) return false;
        if (IsMouseVirtualKey(vk) || !g_keyboardHook)
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
        return g_physicalKeyDown[vk].load();
    }

    void SendKeyboardKey(int vk, bool down) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        input.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
        SendInput(1, &input, sizeof(input));
    }

    struct MovementState {
        uint32_t flags = 0;
        Vector3 velocity{};
        uint8_t moveType = 0;
        uint8_t actualMoveType = 0;
        float waterLevel = 0.0f;
    };

    bool ReadMovementState(uintptr_t pawn, MovementState& state) {
        constexpr size_t kMaxMovementSpan = 0x400;
        const uintptr_t firstOffset = Offsets::m_fFlags;
        const uintptr_t lastOffset = Offsets::m_flWaterLevel + sizeof(float);
        if (lastOffset <= firstOffset || lastOffset - firstOffset > kMaxMovementSpan) return false;

        std::array<uint8_t, kMaxMovementSpan> buffer{};
        const SIZE_T size = static_cast<SIZE_T>(lastOffset - firstOffset);
        SIZE_T bytesRead = 0;
        const bool read = ReadProcessMemory(mem.hProcess,
            reinterpret_cast<LPCVOID>(pawn + firstOffset), buffer.data(), size, &bytesRead) &&
            bytesRead == size;
        Stats::rpmReadCount.fetch_add(1);
        if (!read) return false;

        auto copyField = [&](uintptr_t offset, void* destination, size_t fieldSize) {
            const size_t relative = static_cast<size_t>(offset - firstOffset);
            if (relative + fieldSize > bytesRead) return false;
            memcpy(destination, buffer.data() + relative, fieldSize);
            return true;
        };
        return copyField(Offsets::m_fFlags, &state.flags, sizeof(state.flags)) &&
            copyField(Offsets::m_vecAbsVelocity, &state.velocity, sizeof(state.velocity)) &&
            copyField(Offsets::m_MoveType, &state.moveType, sizeof(state.moveType)) &&
            copyField(Offsets::m_nActualMoveType, &state.actualMoveType, sizeof(state.actualMoveType)) &&
            copyField(Offsets::m_flWaterLevel, &state.waterLevel, sizeof(state.waterLevel));
    }

    float NormalizeYawDelta(float delta) {
        while (delta > 180.0f) delta -= 360.0f;
        while (delta < -180.0f) delta += 360.0f;
        return delta;
    }

    void BhopWorker() {
        bool jumpDown = false;
        bool readyForLanding = true;
        bool pawnAlive = false;
        uintptr_t localPawn = 0;
        uintptr_t previousPawn = 0;
        ULONGLONG jumpDownMs = 0;
        ULONGLONG lastJumpAttemptMs = 0;
        ULONGLONG lastJumpInputMs = 0;
        ULONGLONG lastPawnRefreshMs = 0;
        int injectedStrafeVk = 0;
        float previousYaw = 0.0f;
        bool hasPreviousYaw = false;
        bool previousOnGround = true;

        auto releaseJump = [&]() {
            if (jumpDown) SendKeyboardKey(VK_SPACE, false);
            jumpDown = false;
            jumpDownMs = 0;
        };

        auto releaseStrafe = [&]() {
            if (injectedStrafeVk != 0 &&
                !g_physicalKeyDown[injectedStrafeVk].load())
                SendKeyboardKey(injectedStrafeVk, false);
            injectedStrafeVk = 0;
            g_strafeDirection.store(0);
        };

        auto resetState = [&]() {
            releaseJump();
            releaseStrafe();
            readyForLanding = true;
            lastJumpAttemptMs = 0;
            lastJumpInputMs = 0;
            previousPawn = 0;
            hasPreviousYaw = false;
            previousOnGround = true;
            g_onGround.store(false);
            g_horizontalSpeed.store(0.0f);
            g_verticalSpeed.store(0.0f);
            g_telemetryValid.store(false);
        };

        while (!g_workerStop.load()) {
            const ULONGLONG nowMs = GetTickCount64();
            if (jumpDown && nowMs - jumpDownMs >= kJumpPulseMs)
                releaseJump();

            const bool active = g_enabled.load() && !g_menuOpen.load() &&
                g_gameInputActive.load() && IsActivationHeld();
            if (!active || !mem.clientModule) {
                resetState();
                Sleep(kIdlePollMs);
                continue;
            }

            if (!IsValidPtr(localPawn) || nowMs - lastPawnRefreshMs >= kPawnRefreshMs) {
                localPawn = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerPawn);
                pawnAlive = IsValidPtr(localPawn) &&
                    mem.Read<uint8_t>(localPawn + Offsets::m_lifeState) == 0;
                lastPawnRefreshMs = nowMs;
            }
            if (!IsValidPtr(localPawn) || !pawnAlive) {
                resetState();
                Sleep(kIdlePollMs);
                continue;
            }

            if (previousPawn != localPawn) {
                releaseJump();
                previousPawn = localPawn;
                readyForLanding = true;
                lastJumpAttemptMs = 0;
                lastJumpInputMs = 0;
                hasPreviousYaw = false;
                previousOnGround = true;
            }

            MovementState movement{};
            if (!ReadMovementState(localPawn, movement)) {
                resetState();
                Sleep(kIdlePollMs);
                continue;
            }

            const float horizontalSpeed = std::hypot(movement.velocity.x, movement.velocity.y);
            const bool onGround = (movement.flags & FL_ONGROUND) != 0;
            const uint8_t effectiveMoveType = movement.actualMoveType != 0
                ? movement.actualMoveType
                : movement.moveType;
            const bool unsupportedMovement = effectiveMoveType == 8 || effectiveMoveType == 9 ||
                effectiveMoveType == 10 || movement.waterLevel > 0.5f;
            g_horizontalSpeed.store(std::isfinite(horizontalSpeed) ? horizontalSpeed : 0.0f);
            g_verticalSpeed.store(std::isfinite(movement.velocity.z) ? movement.velocity.z : 0.0f);
            g_onGround.store(onGround);
            g_telemetryValid.store(true);

            if (unsupportedMovement) {
                releaseJump();
                releaseStrafe();
                readyForLanding = true;
                previousOnGround = onGround;
                Sleep(kIdlePollMs);
                continue;
            }

            if (previousOnGround && !onGround && lastJumpInputMs != 0) {
                g_jumpResponseMs.store(static_cast<int>((std::min)(nowMs - lastJumpInputMs, 999ULL)));
                g_successfulJumps.fetch_add(1);
                lastJumpInputMs = 0;
            }

            if (!onGround) {
                readyForLanding = true;

                if (g_strafeAssist.load()) {
                    const bool physicalA = g_physicalKeyDown['A'].load();
                    const bool physicalD = g_physicalKeyDown['D'].load();
                    if (physicalA || physicalD) {
                        releaseStrafe();
                        g_strafeDirection.store(physicalA && !physicalD ? -1 :
                            (physicalD && !physicalA ? 1 : 0));
                        hasPreviousYaw = false;
                    } else {
                        const Vector3 viewAngles = mem.Read<Vector3>(mem.clientModule + Offsets::dwViewAngles);
                        if (std::isfinite(viewAngles.y)) {
                            int desiredVk = 0;
                            if (hasPreviousYaw) {
                                const float yawDelta = NormalizeYawDelta(viewAngles.y - previousYaw);
                                if (yawDelta > 0.015f) desiredVk = 'D';
                                else if (yawDelta < -0.015f) desiredVk = 'A';
                            }
                            previousYaw = viewAngles.y;
                            hasPreviousYaw = true;

                            if (desiredVk != injectedStrafeVk) {
                                releaseStrafe();
                                if (desiredVk != 0) {
                                    SendKeyboardKey(desiredVk, true);
                                    injectedStrafeVk = desiredVk;
                                    g_strafeDirection.store(desiredVk == 'A' ? -1 : 1);
                                }
                            }
                        }
                    }
                } else {
                    releaseStrafe();
                    hasPreviousYaw = false;
                }
                previousOnGround = false;
                Sleep(kActivePollMs);
                continue;
            }

            releaseStrafe();
            hasPreviousYaw = false;

            if (!jumpDown &&
                (readyForLanding || nowMs - lastJumpAttemptMs >= kGroundRetryMs)) {
                if (!readyForLanding) g_groundRetries.fetch_add(1);
                SendKeyboardKey(VK_SPACE, true);
                jumpDown = true;
                jumpDownMs = nowMs;
                lastJumpAttemptMs = nowMs;
                lastJumpInputMs = nowMs;
                readyForLanding = false;
            }
            previousOnGround = true;
            Sleep(kActivePollMs);
        }
        releaseJump();
        releaseStrafe();
    }

    void StartWorker() {
        if (g_workerStarted.exchange(true)) return;
        g_workerStop.store(false);
        g_worker = std::thread(BhopWorker);
    }
}

void PollBhopKeyBind() {
    if (!g_Bhop.waitingForHoldKey) return;
    for (int vk = 1; vk < 256; ++vk) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
        if (GetAsyncKeyState(vk) & 0x8000) {
            g_Bhop.holdKeyVk = vk;
            g_Bhop.waitingForHoldKey = false;
            break;
        }
    }
}

void RunBhop() {
    g_enabled.store(g_Bhop.enabled);
    g_requireHoldKey.store(g_Bhop.requireHoldKey);
    g_holdKeyVk.store(g_Bhop.holdKeyVk);
    g_strafeAssist.store(g_Bhop.strafeAssist);
    g_menuOpen.store(g_MenuOpen);
    g_gameInputActive.store(IsGameInputActive());

    if (!g_Bhop.enabled && !g_workerStarted.load()) return;
    EnsureKeyboardHook();
    StartWorker();
}

BhopTelemetry GetBhopTelemetry() {
    BhopTelemetry telemetry{};
    telemetry.horizontalSpeed = g_horizontalSpeed.load();
    telemetry.verticalSpeed = g_verticalSpeed.load();
    telemetry.successfulJumps = g_successfulJumps.load();
    telemetry.groundRetries = g_groundRetries.load();
    telemetry.jumpResponseMs = g_jumpResponseMs.load();
    telemetry.strafeDirection = g_strafeDirection.load();
    telemetry.onGround = g_onGround.load();
    telemetry.valid = g_telemetryValid.load();
    return telemetry;
}

void ShutdownBhop() {
    g_enabled.store(false);
    g_workerStop.store(true);
    if (g_worker.joinable()) g_worker.join();
    g_workerStarted.store(false);

    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
}
