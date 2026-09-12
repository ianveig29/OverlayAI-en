#include "ChatSpam.h"
#include "Config.h"
#include "Types.h"
#include "Memory.h"
#include "Offsets.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace {
    // Typing sequence timings (in milliseconds). They are the same delays
    // a fast human typist has: we give the game time to register each key
    // before the next one.
    constexpr ULONGLONG kDelayAfterChatKeyMs = 80;
    constexpr ULONGLONG kDelayAfterPasteMs = 80;

    // State machine phases (for the menu debug line).
    enum Phase {
        kPhaseIdle = 0,       // nothing pending
        kPhaseChatKeyPressed, // we opened chat (Y/U), waiting for it to appear
        kPhasePasted,         // text pasted, waiting before Enter
        kPhaseCooldown        // message sent, waiting for the next one
    };

    struct ChatSpamState {
        int      phase = kPhaseIdle;
        ULONGLONG phaseDeadline = 0;   // when the current phase continues
        ULONGLONG nextSpamMs = 0;      // when the next loop message goes out
        int      spamIndex = 0;       // which loop message is next
        bool     killSayQueued = false; // a kill message is pending
        int      messagesSent = 0;
        int      killsDetected = 0;
        int      killsBaseline = -1;   // round kills when we started watching
        bool     gameFocused = false;
    };
    ChatSpamState g_cs;

    // The key that opens chat: Y = team, U = all (CS2 default).
    WORD ChatKeyVk() {
        return g_ChatSpam.useTeamChat ? 'Y' : 'U';
    }

    // true if the GAME WINDOW has focus. We only type in that case: if
    // the overlay menu is open or focus is in another app, sending keys
    // would type in the wrong place.
    bool IsGameFocused() {
        const HWND foreground = GetForegroundWindow();
        if (!foreground) return false;
        DWORD foregroundPid = 0;
        GetWindowThreadProcessId(foreground, &foregroundPid);
        return foregroundPid == mem.pid;
    }

    // Sends one key (press + release) via SendInput.
    void SendKeyTap(WORD vk) {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = vk;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = vk;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        (void)SendInput(2, inputs, sizeof(INPUT));
    }

    // Sends Ctrl+V (Ctrl down, V down, V up, Ctrl up).
    void SendCtrlV() {
        INPUT inputs[4] = {};
        inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
        inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V';
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        (void)SendInput(4, inputs, sizeof(INPUT));
    }

    // Puts text into the Windows clipboard (as Unicode, so messages with
    // accents or special characters paste correctly).
    bool SetClipboardText(const char* text) {
        if (!text || !text[0]) return false;
        const int wideLen = MultiByteToWideChar(CP_UTF8, 0, text, -1,
            nullptr, 0);
        if (wideLen <= 0) return false;
        HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE,
            static_cast<SIZE_T>(wideLen) * sizeof(wchar_t));
        if (!handle) return false;
        wchar_t* target = static_cast<wchar_t*>(GlobalLock(handle));
        if (!target) { GlobalFree(handle); return false; }
        MultiByteToWideChar(CP_UTF8, 0, text, -1, target, wideLen);
        GlobalUnlock(handle);
        if (!OpenClipboard(nullptr)) { GlobalFree(handle); return false; }
        EmptyClipboard();
        const bool ok = SetClipboardData(CF_UNICODETEXT, handle) != nullptr;
        CloseClipboard();
        if (!ok) GlobalFree(handle);
        return ok;
    }

    // How many loop messages have content (empty ones are skipped).
    int CountActiveMessages() {
        int count = 0;
        for (const char* msg : g_ChatSpam.messages)
            if (msg[0]) ++count;
        return count;
    }

    // Loads the given message into the clipboard and starts typing it.
    // Returns false if there was nothing valid to send.
    bool StartWriting(const char* message) {
        if (!message || !message[0]) return false;
        if (!SetClipboardText(message)) return false;
        SendKeyTap(ChatKeyVk());
        g_cs.phase = kPhaseChatKeyPressed;
        g_cs.phaseDeadline = GetTickCount64() + kDelayAfterChatKeyMs;
        return true;
    }

    // Reads the local player's round kills following the chain:
    // dwLocalPlayerController -> m_pActionTrackingServices -> m_iNumRoundKills.
    // Returns -1 if the chain cannot be read (menu, map loading...).
    int ReadLocalRoundKills() {
        const uintptr_t controller =
            mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerController);
        if (!IsValidPtr(controller)) return -1;
        const uintptr_t actionServices =
            mem.Read<uintptr_t>(controller + Offsets::m_pActionTrackingServices);
        if (!IsValidPtr(actionServices)) return -1;
        return mem.Read<int>(actionServices + Offsets::m_iNumRoundKills);
    }
}

void UpdateChatSpam() {
    g_cs.gameFocused = IsGameFocused();

    // Golden rule: no keys are sent while the game is not focused or the
    // menu is open. A half-finished sequence is cancelled (the in-game
    // chat stays open but empty; the Enter is never sent).
    const bool inputAllowed = g_cs.gameFocused && !g_MenuOpen;
    if (!inputAllowed) {
        g_cs.phase = kPhaseIdle;
        g_cs.killSayQueued = false;
        return;
    }

    // ---- Kill detection (edge detection on the round counter) ----
    const int roundKills = ReadLocalRoundKills();
    if (roundKills >= 0) {
        if (g_cs.killsBaseline < 0) {
            // First valid read of the session: take the baseline.
            g_cs.killsBaseline = roundKills;
        } else if (roundKills > g_cs.killsBaseline) {
            // The counter went up: we killed someone.
            g_cs.killsDetected += roundKills - g_cs.killsBaseline;
            g_cs.killsBaseline = roundKills;
            if (g_ChatSpam.killSayEnabled && g_ChatSpam.killSayMessage[0])
                g_cs.killSayQueued = true;
        } else if (roundKills < g_cs.killsBaseline) {
            // The counter went down: a new round started. Reset the baseline.
            g_cs.killsBaseline = roundKills;
        }
    } else {
        // Unreadable chain (menu, map change): reset the baseline.
        g_cs.killsBaseline = -1;
    }

    const ULONGLONG now = GetTickCount64();

    // ---- Typing state machine ----
    switch (g_cs.phase) {
    case kPhaseIdle: {
        if (g_cs.killSayQueued) {
            // Kill say has priority over the spam loop.
            g_cs.killSayQueued = false;
            if (StartWriting(g_ChatSpam.killSayMessage)) {
                g_cs.messagesSent += 1;
                // Spam waits again so they do not overlap.
                g_cs.nextSpamMs = now + static_cast<ULONGLONG>(
                    g_ChatSpam.intervalSeconds) * 1000;
            }
            break;
        }
        if (!g_ChatSpam.spamEnabled || CountActiveMessages() == 0) break;
        if (now < g_cs.nextSpamMs) break;
        // Find the next non-empty loop message (we go at most one full
        // lap before giving up).
        for (int step = 0; step < 4; ++step) {
            const int index = g_cs.spamIndex % 4;
            g_cs.spamIndex = (g_cs.spamIndex + 1) % 4;
            if (g_ChatSpam.messages[index][0]) {
                if (StartWriting(g_ChatSpam.messages[index])) {
                    g_cs.messagesSent += 1;
                    g_cs.nextSpamMs = now + static_cast<ULONGLONG>(
                        g_ChatSpam.intervalSeconds) * 1000;
                }
                break;
            }
        }
        break;
    }
    case kPhaseChatKeyPressed:
        if (now < g_cs.phaseDeadline) break;
        SendCtrlV();
        g_cs.phase = kPhasePasted;
        g_cs.phaseDeadline = now + kDelayAfterPasteMs;
        break;
    case kPhasePasted:
        if (now < g_cs.phaseDeadline) break;
        SendKeyTap(VK_RETURN);
        g_cs.phase = kPhaseIdle;
        g_cs.nextSpamMs = now + static_cast<ULONGLONG>(
            g_ChatSpam.intervalSeconds) * 1000;
        break;
    default:
        g_cs.phase = kPhaseIdle;
        break;
    }
}

ChatSpamStatus GetChatSpamStatus() {
    ChatSpamStatus s;
    s.phase = g_cs.phase;
    s.messagesSent = g_cs.messagesSent;
    s.killsDetected = g_cs.killsDetected;
    s.busy = g_cs.phase == kPhaseChatKeyPressed || g_cs.phase == kPhasePasted;
    s.gameFocused = g_cs.gameFocused;
    return s;
}
