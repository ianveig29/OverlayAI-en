#pragma once

#include <cstdint>

// ============================================================================
// RECOIL CONTROL SYSTEM (RCS)
// ----------------------------------------------------------------------------
// WHAT RCS IS:
// When you spray with a weapon, the game deflects your bullets following
// the weapon's recoil pattern (they climb and drift sideways). The RCS
// compensates that deflection in real time by moving your aim backwards
// by the same amount the recoil pushed it forward. Result: bullets land
// where you aim, even while emptying the magazine.
//
// HOW IT WORKS (delta compensation):
// The current recoil lives in the pawn's aim punch services (the same
// offsets used by "Quit Aim Punch"). Every frame:
//
//   1) We read the total punch P (predictable + unpredictable).
//   2) We compare it with the previous frame's punch (P_prev).
//   3) The delta (how much the recoil grew) is SUBTRACTED from your view
//      angles. If the recoil climbed 2 degrees, your aim goes down 2
//      degrees.
//
// Since the delta accumulates frame by frame, by the end of the spray
// your aim has moved down by exactly the amount the pattern climbed.
// When you stop firing the punch decays back to 0 with negative deltas,
// so your aim returns by itself to the original point (the classic RCS
// recovery behavior).
//
// DIFFERENCE FROM "QUIT AIM PUNCH":
// - Quit Aim Punch only HIDES the visual recoil: the crosshair stays
//   still but the bullets still deflect (the crosshair lies).
// - The RCS MOVES your aim to truly compensate: bullets land where you
//   aim (the crosshair tells the truth). This is the one that actually
//   helps you play, which is why it is double-edged: more effective =
//   more visible in demos to other players.
//
// STRENGTH (slider):
// 100% = perfect compensation (robotic, noticeable in demos).
~ 60% = compensates most of it but looks more human.
//
// WARNING: unlike reading ESP, this WRITES to the game's view angles
// every frame while you shoot. It is the most "active" feature in the
// project. It is still external, but know the risk you are taking.
// ============================================================================

// Called every frame from the main loop. If RCS is enabled, it
// compensates this frame's recoil delta into your view angles.
void RunRCS();

// How many frames had to be compensated since it was enabled (for the menu).
unsigned GetRCSCompensationCount();
