#pragma once

#include <cstdint>

// ============================================================================
// QUIT PUNCHVIEW (CAMERA KICK)
// ----------------------------------------------------------------------------
// WHAT "VIEW PUNCH" IS:
// In CS2, when you take damage or something explodes near you, the camera
// "jumps" to one side for a moment. That kick is called view punch and the
// game stores it on your character (pawn) as an angle:
//
//   pawn -> m_pCameraServices (pointer to the camera services)
//                     -> m_vecCsViewPunchAngle (the kick angle)
//
// WHAT THIS MODULE DOES:
// While enabled, it reads that angle every frame and, if it is not zero,
// writes (0, 0, 0) on top of it. Result: the camera no longer kicks when
// you get hit. Since we only write to your OWN client's local memory, it
// works on any server (official, competitive, etc.) without patching any
// game bytes.
//
// IMPORTANT (to avoid confusion):
// - This ONLY removes the camera kick from DAMAGE (bullets, explosions).
// - It does NOT remove the recoil of your own shots: that is "aim punch"
//   and it lives somewhere else. If we hid it, your crosshair would stay
//   still but the bullets would still follow the server's real pattern
//   (the crosshair would lie). That is why we do not touch it.
// - It is a 100% visual change: your health, your damage and the real
//   bullets are calculated the same way on the server.
// ============================================================================

// Called every frame from the main loop. If "Quit Punchview" is enabled,
// it neutralizes the local player's view punch angle.
void RunQuitPunchview();

// How many corrections have been made since it was enabled (shown in the
// menu as confirmation that the module is working).
unsigned GetPunchViewCorrectionCount();

// ============================================================================
// QUIT AIM PUNCH (VISUAL RECOIL OF YOUR SHOTS)
// ----------------------------------------------------------------------------
// WHAT "AIM PUNCH" IS:
// When you shoot, the recoil raises your camera following the weapon's
// pattern. That recoil angle is stored in the pawn's "aim punch services":
//
//   pawn -> m_pAimPunchServices
//                     -> m_predictableBaseAngle   (predictable part)
//                     -> m_predictableBaseAngleVel (its velocity)
//                     -> m_unpredictableBaseAngle (unpredictable part)
//
// WHAT IT DOES: while enabled, it zeroes those angles every frame.
// Result: the crosshair barely moves when firing.
//
// BIG WARNING (read it twice):
// BULLETS still follow the REAL recoil pattern the server computes.
// This module only hides the visual part on YOUR screen. Consequence:
// if you empty a magazine, the crosshair looks still but the bullets
// climb according to the weapon's pattern. To control the spray you
// must know the pattern "from memory", not by watching the crosshair.
// Demos/replays recorded from your client will not show the recoil either.
// ============================================================================

// Called every frame from the main loop. If "Quit Aim Punch" is enabled,
// it neutralizes the local player's recoil angle.
void RunQuitAimPunch();

// How many times the aim punch was neutralized since it was enabled (for the menu).
unsigned GetAimPunchCorrectionCount();
