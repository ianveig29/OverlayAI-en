#pragma once

// Third-person camera mode by patching two things in client.dll:
//
// 1) Writes 256 at client.dll + dwCSGOInput + 0x228 (camera value:
//    0 = first person, 256 = third person).
//
// 2) Changes the JE byte (0x74) to JNE (0x75) at client.dll + dwThirdPersonPatch
//    to invert the engine check and allow third-person mode.
//
// WARNING: Does not work on official competitive servers.
// Only for bot matches or sv_cheats 1.
bool RunThirdPerson();

// Restore camera value to 0 and the original byte (0x74).
void RestoreThirdPerson();

// Returns true while the patch is currently applied.
bool IsThirdPersonActive();

// Captures the key the user presses to assign the third-person
// keybind (same as PollBhopKeyBind).
void PollThirdPersonKeyBind();
