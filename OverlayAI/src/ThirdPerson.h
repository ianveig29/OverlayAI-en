#pragma once

// Third-person camera mode by patching two things in client.dll:
//
// 1) Writes 256 at client.dll + dwThirdPersonValue (static input-array
//    address, 0 = first person, 256 = third person).
//
// 2) Changes the JE byte (0x74) to JNE (0x75) at client.dll + dwThirdPersonPatch
//    to invert the engine check and allow third-person mode.
//
// NOTE: this is a 100% client-side patch (client.dll only), it sends
// nothing to the server, so it behaves the same in every game mode.
bool RunThirdPerson();

// Restore camera value to 0 and the original byte (0x74).
void RestoreThirdPerson();

// Returns true while the patch is currently applied.
bool IsThirdPersonActive();

// Status code of the last attempt (0 = applied). Used by the menu to show
// WHY the apply failed instead of failing silently.
int GetThirdPersonStatus();

// Reads the current camera value (0 or 256) from the static address.
// Returns -1 when no address is resolved yet.
int ReadThirdPersonCameraValue();

// Captures the key the user presses to assign the third-person
// keybind (same as PollBhopKeyBind).
void PollThirdPersonKeyBind();
