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
        // Pattern-scan retry control: instead of scanning only once per
        // session, allow a retry every 5 seconds while the feature is on.
        ULONGLONG nextScanMs = 0;
        // True when the dumper offset pointed at an invalid byte: stop
        // trusting it and rely on the pattern scan instead.
        bool      offsetAddressInvalid = false;
        // True when patchAddress came from the dumper offset (vs. pattern).
        bool      patchFromOffset = false;
        // Last failure reason for the menu status line (0 = all ok).
        int       lastFailReason = 0;
        // Diagnostics of the last pattern scan (for the status line).
        int       scanAttempts = 0;   // scans done this session
        int       scanMatchCount = 0; // matches found by the last scan
        bool      scanReadOk = false;  // true if the last scan read memory
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
    // Full pattern, taken from the recreated disassembly of 09/12/2026
    // (client.dll+B1EB87..B1EB96):
    //   48 8B 05 ?? ?? ?? ??   mov rax,[client.dll+23C72E8]  (dwCSGOInput)
    //   48 85 C0               test rax,rax                  (null check)
    //   74 40                  je client.dll+B1EBD3          (TARGET: 74->75)
    //   8B 40 30               mov eax,[rax+30]
    // The "mov rax,[rip+...] / test rax,rax" is the signature of the input
    // pointer null check; the 3 extra bytes (8B 40 30, the load right after
    // the jump) make the pattern more specific and prevent false positives
    // during the scan. The 0x74 byte (target) is at index 10 of the pattern.

    struct PatternByte { uint8_t value; bool wildcard; };

    constexpr std::array<PatternByte, 15> g_pattern = {{
        {0x48, false}, {0x8B, false}, {0x05, false},
        {0x00, true},  {0x00, true},  {0x00, true},  {0x00, true},  // RIP-relative (wildcard)
        {0x48, false}, {0x85, false}, {0xC0, false},               // test rax, rax
        {0x74, false},                                                // JE (target)
        {0x40, false},                                                // jump offset
        {0x8B, false}, {0x40, false}, {0x30, false}                 // mov eax,[rax+30]
    }};

    // Index of the 0x74 byte within g_pattern (the one we patch).
    constexpr size_t g_targetIndex = 10;

    // ---- Module scan --------------------------------------------------------
    // Searches a region of the module for the pattern, reading in 1 MB
    // chunks: a single unreadable page only ruins its own chunk. The old
    // version read the whole client.dll in one go, so one unreadable
    // page cancelled the entire scan, which failed forever without saying why.
    struct PatternScanResult {
        std::vector<uintptr_t> matches;
        bool readOk = false;
    };

    PatternScanResult ScanThirdPersonPattern(size_t startOffset, size_t size) {
        PatternScanResult result;
        if (!mem.clientModule || mem.clientModuleSize == 0) return result;
        if (startOffset >= mem.clientModuleSize) return result;
        if (size > mem.clientModuleSize - startOffset)
            size = mem.clientModuleSize - startOffset;

        const size_t kChunk = 1024 * 1024;
        const size_t patternLen = g_pattern.size();
        std::vector<uint8_t> buffer(kChunk + patternLen);

        for (size_t base = 0; base < size; base += kChunk) {
            const size_t chunk = kChunk < size - base ? kChunk : size - base;
            // Overlap with the next chunk: a match split at the edge is not lost.
            size_t toRead = chunk + patternLen;
            if (toRead > size - base) toRead = size - base;
            SIZE_T bytesRead = 0;
            if (!ReadProcessMemory(mem.hProcess,
                    reinterpret_cast<LPCVOID>(
                        mem.clientModule + startOffset + base),
                    buffer.data(), toRead, &bytesRead) ||
                bytesRead < patternLen)
                continue;  // unreadable chunk: move on to the next one
            result.readOk = true;
            Stats::rpmReadCount.fetch_add(1);

            for (size_t i = 0; i + patternLen <= bytesRead && i < chunk; ++i) {
                bool ok = true;
                for (size_t j = 0; j < patternLen; ++j) {
                    if (!g_pattern[j].wildcard && buffer[i + j] != g_pattern[j].value) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) continue;
                result.matches.push_back(
                    mem.clientModule + startOffset + base + i + g_targetIndex);
            }
        }
        return result;
    }

    uintptr_t FindThirdPersonPatchByPattern() {
        if (!mem.clientModule || mem.clientModuleSize == 0) return 0;
        ++g_tp.scanAttempts;

        // 1) Window of +/- 8 MB around the dumper offset, EVEN if that offset
        //    was already marked invalid: game updates move this code very
        //    little (the last one shifted it ~2.8 KB), so the real JE almost
        //    certainly falls inside the window. And it is far cheaper than
        //    reading the entire module.
        const uintptr_t hint = Offsets::dwThirdPersonPatch;
        if (hint != 0 && hint < mem.clientModuleSize) {
            const size_t kWindow = 8 * 1024 * 1024;
            const size_t start = hint > kWindow ? (size_t)(hint - kWindow) : 0;
            PatternScanResult r = ScanThirdPersonPattern(start, 2 * kWindow);
            g_tp.scanMatchCount = (int)r.matches.size();
            g_tp.scanReadOk = r.readOk;
            if (!r.matches.empty()) {
                if (r.matches.size() == 1) return r.matches[0];
                // Varias coincidencias: quedarse con la mas cercana al offset
                // del dumper (el patron es especifico, un falso positivo mas
                // cerca que el JE real es muy improbable).
                uintptr_t best = r.matches[0];
                uintptr_t bestDist = best > hint ? best - hint : hint - best;
                for (size_t k = 1; k < r.matches.size(); ++k) {
                    const uintptr_t m = r.matches[k];
                    const uintptr_t d = m > hint ? m - hint : hint - m;
                    if (d < bestDist) { bestDist = d; best = m; }
                }
                return best;
            }
        }

        // Sin offset de referencia (o ventana vacia): escanear el modulo
        // completo por chunks. Aqui exigimos una unica coincidencia: sin
        // punto de referencia, elegir entre varias seria adivinar.
        PatternScanResult r = ScanThirdPersonPattern(0, mem.clientModuleSize);
        g_tp.scanMatchCount = (int)r.matches.size();
        g_tp.scanReadOk = r.readOk;
        return r.matches.size() == 1 ? r.matches[0] : 0;
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
    // 1) Prefer the dwThirdPersonPatch offset from the auto-updater (JSON):
    //    the dumper validates it on every game update, so it is the ground
    //    truth. 2) Fall back to the byte pattern scan ONLY if the offset is
    //    missing. The pattern is generic (mov/test/je) and after a game
    //    recompile it can match a DIFFERENT instruction and silently patch
    //    the wrong JE - that is why it is the fallback, not the first choice.
    // Resolve the JE address: 1) the auto-updater offset (source of
    // truth), but if it already proved invalid (unexpected byte) never
    // reuse it: it would point at the same wrong place forever. 2) the
    // pattern scan, retried every 5 seconds instead of once per session:
    // a later retry can win when the first attempt ran while the module
    // was still loading or raced an offset refresh.
    if (!g_tp.patchAddress && !g_tp.offsetAddressInvalid) {
        const uintptr_t offset = Offsets::dwThirdPersonPatch;
        if (offset != 0 &&
            (mem.clientModuleSize == 0 || offset < mem.clientModuleSize)) {
            g_tp.patchAddress = mem.clientModule + offset;
            g_tp.patchFromOffset = true;
        }
    }
    if (!g_tp.patchAddress) {
        const ULONGLONG nowMs = GetTickCount64();
        if (nowMs >= g_tp.nextScanMs) {
            g_tp.nextScanMs = nowMs + 5000;
            g_tp.patchAddress = FindThirdPersonPatchByPattern();
            g_tp.patchFromOffset = false;
        }
    }

    // Resolve the camera value address (0 or 256).
    // IMPORTANT: the dumper's dwCSGOInput is the GLOBAL VARIABLE that
    // HOLDS the pointer to the CCSGOInput object (the "mov rax,[client.dll+...]"
    // in the disassembly). It must be dereferenced: read the pointer stored
    // in that variable and only then add the 0x228 sub-offset. The code used
    // to add 0x228 straight on top of client.dll, so the 256 landed on a
    // random address and the camera never changed even with the JE patched.
    // Verified against the 09/11 disassembly: pointer global at
    // client.dll+23C72E8, CCSGOInput object, camera value (0 or 256 as a
    // 4-byte integer) at object + 0x228.
    const uintptr_t inputValue = Offsets::dwCSGOInput;
    if (inputValue == 0) { g_tp.lastFailReason = 1; return false; }
    if (mem.clientModuleSize != 0 && inputValue >= mem.clientModuleSize)
        return false;
    const uintptr_t inputInstance =
        mem.Read<uintptr_t>(mem.clientModule + inputValue);
    if (!IsValidPtr(inputInstance)) return false;
    g_tp.valueAddress = inputInstance + kThirdPersonValueSubOffset;

    if (!g_tp.patchAddress || !g_tp.valueAddress) {
        g_tp.lastFailReason = !g_tp.patchAddress
            ? (g_tp.offsetAddressInvalid ? 2 : 3) : 1;
        return false;
    }

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
        // Unexpected byte: this address is not the JE we want. Drop the
        // cached address so the next call re-resolves (updated offset or
        // pattern scan) instead of failing silently for the whole session.
        g_tp.patchAddress = 0;
        // Mark the source as bad so we never retry the same wrong address
        // forever: if it came from the dumper offset, the next attempt goes
        // straight to the pattern scan.
        if (g_tp.patchFromOffset) g_tp.offsetAddressInvalid = true;
        g_tp.lastFailReason = 4;
        return false;
    } else {
        g_tp.originalByte = current;
    }

    // Step 1: Write 256 at the camera value address.
    // This tells the engine "the camera is in third person".
    if (!WriteThirdPersonValue(g_tp.valueAddress, 256)) {
        g_tp.lastFailReason = 5;
        return false;
    }

    // Step 2: Patch JE (0x74) to JNE (0x75).
    // If the byte is already 0x75, no need to write again.
    if (current == 0x74) {
        if (!WriteExecutableByte(g_tp.patchAddress, 0x74, 0x75)) {
            g_tp.lastFailReason = 5;
            return false;
        }
    }

    g_tp.applied = true;
    g_tp.lastFailReason = 0;
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

int GetThirdPersonStatus() {
    // 0 = applied, 1 = waiting for base/offsets, 2 = invalid dumper
    // offset, 3 = pattern not found, 4 = unexpected byte, 5 = write
    // failure, 6 = pattern: unreadable memory, 7 = pattern: 0 matches,
    // 8 = pattern: ambiguous. Shown in the menu so a failure is never
    // invisible again.
    if (g_tp.applied) return 0;
    if (!mem.clientModule || Offsets::dwCSGOInput == 0) return 1;
    // If the dumper offset proved invalid and the pattern scan already
    // ran, the scan is now in charge: report its result instead of
    // repeating "invalid offset" forever.
    if (!g_tp.patchAddress && g_tp.scanAttempts > 0) {
        if (!g_tp.scanReadOk) return 6;
        if (g_tp.scanMatchCount == 0) return 7;
        if (g_tp.scanMatchCount > 1) return 8;
    }
    if (g_tp.lastFailReason != 0) return g_tp.lastFailReason;
    return 1;
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
