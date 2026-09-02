#include "ThirdPerson.h"
#include "Types.h"
#include "Config.h"
#include "Memory.h"
#include "Offsets.h"
#include "PatchRestore.h"
#include "Stats.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {
    // ---- Patch state --------------------------------------------------------
    //
    // Third-person in CS2 works with TWO things:
    //
    // 1) A 4-byte value at client.dll + dwCSGOInput + 0x228 that the engine
    //    uses to know if the camera is in first (0) or third person (256).
    //    We write 256 to force third-person mode.
    //    dwCSGOInput is auto-updated from the a2x dumper; only the +0x228 is fixed.
    //
    // 2) A JE check at client.dll + dwThirdPersonPatch that controls whether
    //    the third-person code runs. We change 0x74 (JE) to 0x75 (JNE) to
    //    invert the check so the code always executes.
    //
    // Without step 1, patching the JE alone does nothing visible because the
    // camera value stays at 0 (first person).

    // Relative offset inside CCSGOInput for the third-person camera value.
    // dwCSGOInput + 0x228 = CameraThirdPersonValue (0 = first, 256 = third).
    // This offset (0x228) is stable: it only changes if Valve restructures CCSGOInput.
    // dwCSGOInput IS in the a2x dumper and auto-updates with each game update.
    constexpr uintptr_t kThirdPersonValueSubOffset = 0x228;

    struct ThirdPersonPatchState {
        uintptr_t patchAddress = 0;     // Address of the JE byte to patch
        uintptr_t valueAddress = 0;     // Address of the camera value (0 or 256)
        uint8_t   originalByte = 0;     // Original JE byte (0x74)
        bool      applied = false;       // true if the patch is active
        bool      scanAttempted = false; // true if we already tried scanning
    };

    ThirdPersonPatchState g_tp;

    // ---- Byte pattern to find the JE ----------------------------------------
    //
    // Disassembly around the target (client.dll+B19EB1):
    //
    //   B19EA7: 48 8B 05 9A 6A 8A 01   mov rax, [rip+0x018A6A9A]
    //   B19EAE: 48 85 C0               test rax, rax
    //   B19EB1: 74 40                  je +0x40    <-- TARGET (74 -> 75)
    //
    // The middle 4 bytes (9A 6A 8A 01) are a RIP-relative address that changes
    // between game updates. They are marked as wildcards (??) so the pattern
    // stays valid after any recompile.
    //
    // Full pattern: 48 8B 05 ?? ?? ?? ?? 48 85 C0 74 40
    // The 0x74 byte (target) is at index 10 of the pattern.

    struct PatternByte { uint8_t value; bool wildcard; };

    constexpr std::array<PatternByte, 12> g_pattern = {{
        {0x48, false}, {0x8B, false}, {0x05, false},
        {0x00, true},  {0x00, true},  {0x00, true},  {0x00, true},  // RIP-relative (wildcard)
        {0x48, false}, {0x85, false}, {0xC0, false},               // test rax, rax
        {0x74, false},                                                // JE (target)
        {0x40, false}                                                 // jump offset
    }};

    // Index of the 0x74 byte within g_pattern (the one we patch).
    constexpr size_t g_targetIndex = 10;

    // ---- Module scan --------------------------------------------------------
    // Reads the entire client.dll into a local buffer and searches for the
    // pattern with wildcards. Returns the address of the 0x74 byte if there
    // is exactly 1 match, or 0 if there are none or multiple (ambiguous).
    uintptr_t FindThirdPersonPatchByPattern() {
        if (!mem.clientModule || mem.clientModuleSize == 0) return 0;

        // Read the entire module into a local buffer.
        std::vector<uint8_t> module(mem.clientModuleSize);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(mem.hProcess,
                reinterpret_cast<LPCVOID>(mem.clientModule),
                module.data(), module.size(), &bytesRead) || bytesRead < 16)
            return 0;
        Stats::rpmReadCount.fetch_add(1);

        uintptr_t match = 0;
        size_t matchCount = 0;
        const size_t patternLen = g_pattern.size();

        for (size_t i = 0; i + patternLen <= bytesRead; ++i) {
            bool ok = true;
            for (size_t j = 0; j < patternLen; ++j) {
                if (!g_pattern[j].wildcard && module[i + j] != g_pattern[j].value) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;

            // Match found. The address of the 0x74 (target) is:
            // module base + i + target index within the pattern.
            match = mem.clientModule + i + g_targetIndex;
            ++matchCount;
            if (matchCount > 1) return 0;  // ambiguous, don't use
        }

        return matchCount == 1 ? match : 0;
    }

    // ---- Byte writer --------------------------------------------------------
    // Changes a single byte in the remote process. Handles VirtualProtectEx
    // (the .text section is read-only by default) and FlushInstructionCache
    // so the CPU sees the new instruction.
    bool WriteExecutableByte(uintptr_t address,
                             uint8_t expected, uint8_t replacement) {
        uint8_t current = 0;
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(mem.hProcess,
                reinterpret_cast<LPCVOID>(address),
                &current, sizeof(current), &bytesRead) ||
            bytesRead != sizeof(current) ||
            current != expected)
            return false;

        DWORD oldProtect = 0;
        if (!VirtualProtectEx(mem.hProcess,
                reinterpret_cast<LPVOID>(address), 1,
                PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        SIZE_T bytesWritten = 0;
        bool ok = WriteProcessMemory(mem.hProcess,
                reinterpret_cast<LPVOID>(address),
                &replacement, sizeof(replacement),
                &bytesWritten) && bytesWritten == sizeof(replacement);

        if (ok) {
            FlushInstructionCache(mem.hProcess,
                reinterpret_cast<LPCVOID>(address), 1);
            Stats::rpmWriteCount.fetch_add(1);
        }

        // Restore the original page protection.
        DWORD ignored = 0;
        VirtualProtectEx(mem.hProcess,
            reinterpret_cast<LPVOID>(address), 1, oldProtect, &ignored);

        return ok;
    }

    // ---- Camera value writer ------------------------------------------------
    // Writes a 4-byte integer (0 or 256) at the engine's third-person camera
    // value address. No VirtualProtectEx needed because this address is in
    // the .data section (read/write).
    bool WriteThirdPersonValue(uintptr_t address, int value) {
        SIZE_T bytesWritten = 0;
        bool ok = WriteProcessMemory(mem.hProcess,
                reinterpret_cast<LPVOID>(address),
                &value, sizeof(int),
                &bytesWritten) && bytesWritten == sizeof(int);

        if (ok) Stats::rpmWriteCount.fetch_add(1);
        return ok;
    }
}

// ---- Public API -----------------------------------------------------------

bool RunThirdPerson() {
    if (!mem.hProcess) return false;
    if (g_tp.applied) return true;  // already active this session
    if (!mem.clientModule) return false;

    // Resolve the patch and value addresses:
    // 1) Try byte pattern scan (update-resistant).
    // 2) Fall back to JSON offsets (auto-update system).
    if (!g_tp.patchAddress && !g_tp.scanAttempted) {
        g_tp.scanAttempted = true;

        // Attempt 1: byte pattern scan with wildcards for the JE.
        uintptr_t found = FindThirdPersonPatchByPattern();
        if (found) {
            g_tp.patchAddress = found;
        } else {
            // Attempt 2: fixed offset from JSON / auto-update (fallback).
            const uintptr_t offset = Offsets::dwThirdPersonPatch;
            if (offset == 0) return false;
            if (mem.clientModuleSize != 0 && offset >= mem.clientModuleSize)
                return false;
            g_tp.patchAddress = mem.clientModule + offset;
        }

        // Resolve the camera value address (0 or 256).
        // Derived from dwCSGOInput (auto-updated by the dumper) + 0x228.
        // Previously this was a hardcoded absolute offset (dwThirdPersonValue = 0x23DBE98).
        // Now it's calculated dynamically: client.dll + dwCSGOInput + 0x228.
        const uintptr_t inputValue = Offsets::dwCSGOInput;
        if (inputValue == 0) return false;
        const uintptr_t valueOffset = inputValue + kThirdPersonValueSubOffset;
        if (mem.clientModuleSize != 0 && valueOffset >= mem.clientModuleSize)
            return false;
        g_tp.valueAddress = mem.clientModule + valueOffset;
    }

    if (!g_tp.patchAddress || !g_tp.valueAddress) return false;

    // Read the current JE byte to confirm it's the correct instruction.
    uint8_t current = 0;
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(mem.hProcess,
            reinterpret_cast<LPCVOID>(g_tp.patchAddress),
            &current, 1, &bytesRead) || bytesRead != 1)
        return false;

    // Expected: 0x74 (JE). If it's already 0x75 someone (or a previous run)
    // already patched it - treat as success.
    if (current == 0x75) {
        g_tp.originalByte = 0x74;
    } else if (current != 0x74) {
        return false;  // unexpected byte - don't touch
    } else {
        g_tp.originalByte = current;
    }

    // Step 1: Write 256 at the camera value address.
    // This tells the engine "the camera is in third person".
    if (!WriteThirdPersonValue(g_tp.valueAddress, 256))
        return false;

    // Step 2: Patch JE (0x74) to JNE (0x75).
    // If the byte is already 0x75, no need to write again.
    if (current == 0x74) {
        if (!WriteExecutableByte(g_tp.patchAddress, 0x74, 0x75))
            return false;
    }

    g_tp.applied = true;
    return true;
}

void RestoreThirdPerson() {
    if (!g_tp.applied || !mem.hProcess) return;

    // Step 1: Write 0 at the camera value address (back to first person).
    if (g_tp.valueAddress)
        WriteThirdPersonValue(g_tp.valueAddress, 0);

    // Step 2: Restore the original JE byte (0x74).
    if (g_tp.patchAddress)
        WriteExecutableByte(g_tp.patchAddress, 0x75, g_tp.originalByte);

    g_tp.applied = false;
}

bool IsThirdPersonActive() {
    return g_tp.applied;
}

// ---- Keybind capture -----------------------------------------------------
// Same as PollBhopKeyBind: when the user clicks "Change key" in the menu,
// waitingForThirdPersonKey is set to true. This function scans all keys
// until the user presses one, then saves it as the toggle keybind.
void PollThirdPersonKeyBind() {
    if (!g_Esp.waitingForThirdPersonKey) return;
    for (int vk = 1; vk < 256; ++vk) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
        if (GetAsyncKeyState(vk) & 0x8000) {
            g_Esp.thirdPersonKeyVk = vk;
            g_Esp.waitingForThirdPersonKey = false;
            break;
        }
    }
}
