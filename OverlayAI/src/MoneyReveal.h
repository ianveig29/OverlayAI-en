#pragma once

// Reveals enemy money on CS2's native scoreboard.
//
// HOW IT WORKS:
// CS2 has an "is_hltv" function in client.dll that returns true when the
// game is in HLTV mode (spectator/replay). In that mode, the native
// scoreboard shows the money of ALL players, including enemies (because
// casters/spectators need to see the economy).
//
// What we do is patch the first 3 bytes of that function so it always
// returns true:
//
//   Original:  48 83 EC    (sub rsp, 28h  - normal function prologue)
//   Patch:     B0 01 C3    (mov al, 1 + ret - returns 1 and exits)
//
// This makes the game's own engine show enemy money on the scoreboard.
// We don't draw anything ourselves: the native HUD handles everything,
// including real-time updates and between-round updates.
//
// WARNING: Always call RestoreMoneyReveal() on shutdown to restore the
// 3 original bytes. If the CS2 process dies, the bytes stay in its
// memory which gets freed anyway.
//
// Technique originally discovered by rushensky (UnknownCheats).
// Reference: github.com/superyu1337/dma_cs2moneyreveal

// Applies the is_hltv patch to reveal enemy money on the scoreboard.
// Returns true if the patch was applied successfully.
bool RunMoneyReveal();

// Restores the 3 original bytes of is_hltv.
void RestoreMoneyReveal();

// Returns true if the patch is currently active.
bool IsMoneyRevealActive();
