#pragma once

// ============================================================================
// Anti View Punch -camera kick when taking damage-
// ============================================================================
//
// WHAT "VIEW PUNCH" IS:
// When you take damage, CS2 kicks your camera slightly, disrupting your
// aim while you are being shot at. How fast that kick dissolves is
// controlled by the "view_punch_decay" ConVar (default value: 18). That
// ConVar is cheat-flagged: it can only be changed from the console with
// sv_cheats 1.
//
// HOW WE SOLVE IT (found with Cheat Engine on 12/09/2026):
// We do not touch the punch system (we already tried that: the game
// re-derives punch every tick, which was the "Quit Aim Punch" failure).
// Instead we change the ConVar's VALUE by writing it directly to memory,
// bypassing the console and the cheat flag entirely:
//
//   client.dll + dwViewPunchDecayConVar  ->  pointer to the ConVar object (heap)
//   ConVar object + 0x58                 ->  float with the current value
//
// Writing 999 there makes the camera kick dissolve almost instantly.
//
// IMPORTANT (MM matches): the server ALSO reads this ConVar from its own
// copy (in MM that copy lives on Valve's machine, with its own 18). This
// feature fixes YOUR local camera. If in MM bullets still kick server-side,
// the crosshair would lie to you: test on bots/local server first, then
// carefully in MM.
//
// IF IT BREAKS AFTER A GAME UPDATE (recovery procedure):
// The dwViewPunchDecayConVar offset is a custom key: the a2x dumper does
// NOT update it and it goes stale on every update (same as
// dwThirdPersonValue). To re-derive it with Cheat Engine:
//   1. Local server with sv_cheats 1: set view_punch_decay 1234 and scan
//      "Exact Value / Float" = 1234. Change to another value and filter.
//   2. Once you find the float's address: subtract 0x58 to get the ConVar
//      object base, then run a Pointer Scan (max level 1) to that base.
//      The result is "client.dll"+XXXXXX.
//   3. Update "dwViewPunchDecayConVar" in OverlayAI/offsets.json with the
//      XXXXXX in decimal and you are done. The internal +0x58 rarely changes.
// ============================================================================

struct AntiViewPunchStatus {
    int   failReason = 0;     // 0 = ok, see ViewPunch.cpp for the codes
    float currentValue = 0.f; // current decay value (what the game uses)
    float originalValue = 0.f; // original value captured before touching anything
    bool  modified = false;   // true if we wrote a different value
    bool  active = false;     // true if the feature is applied right now
};

void UpdateAntiViewPunch();      // call every frame of the main loop
void ShutdownAntiViewPunch();    // call on exit: restores the original value
AntiViewPunchStatus GetAntiViewPunchStatus(); // status for the menu line
