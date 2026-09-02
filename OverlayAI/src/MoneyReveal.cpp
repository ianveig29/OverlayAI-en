#include "MoneyReveal.h"
#include "Types.h"
#include "Memory.h"
#include "Stats.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {
    // ---- Patch state --------------------------------------------------------
    //
    // Unlike ThirdPerson (which changes 1 byte), here we patch 3 bytes
    // of the is_hltv function prologue in client.dll.
    //
    // Original:  48 83 EC  (sub rsp, 28h)
    // Patch:      B0 01 C3  (mov al, 1 / ret)
    //
    // We save the 3 original bytes so we can restore them on shutdown.

    struct MoneyRevealState {
        uintptr_t funcAddress = 0;      // Address of is_hltv in client.dll
        uint8_t   originalBytes[3] = {}; // 3 original bytes (48 83 EC)
        bool      applied = false;        // true if the patch is active
        bool      scanAttempted = false;  // true if we already tried scanning
    };

    MoneyRevealState g_mr;

    // ---- Byte pattern to find is_hltv ----------------------------------------
    //
    // The is_hltv function in client.dll starts with:
    //
    //   48 83 EC 28            sub rsp, 28h           <- prologue (this is what we patch)
    //   48 8B 0D ?? ?? ?? ??   mov rcx, [rip+...]     <- pointer to something (RIP-relative, wildcard)
    //   48 8B 01               mov rax, [rcx]         <- vtable
    //   FF 90 ?? ?? ?? ??      call [rax+...]          <- virtual call (offset RIP-relative, wildcard)
    //   84 C0                  test al, al            <- check result
    //   75 0D                  jnz +0x0D             <- skip if not HLTV
    //
    // The ?? bytes are RIP-relative addresses that change between updates.
    // They are marked as wildcards so the pattern stays valid.
    //
    // Pattern: 48 83 EC 28 48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 84 C0 75 0D
    // Total: 24 bytes, 8 wildcards.

    struct PatternByte { uint8_t value; bool wildcard; };

    constexpr std::array<PatternByte, 24> g_pattern = {{
        {0x48, false}, {0x83, false}, {0xEC, false}, {0x28, false},  // sub rsp, 28h
        {0x48, false}, {0x8B, false}, {0x0D, false},               // mov rcx, [rip+...]
        {0x00, true},  {0x00, true},  {0x00, true},  {0x00, true},  // RIP-relative (wildcard)
        {0x48, false}, {0x8B, false}, {0x01, false},               // mov rax, [rcx]
        {0xFF, false}, {0x90, false},                             // call [rax+...]
        {0x00, true},  {0x00, true},  {0x00, true},  {0x00, true},  // call offset (wildcard)
        {0x84, false}, {0xC0, false},                             // test al, al
        {0x75, false}, {0x0D, false}                              // jnz +0x0D
    }};

    // Fallback pattern: in case a previous run patched but didn't restore.
    // The first 3 bytes would be B0 01 C3 instead of 48 83 EC.
    constexpr std::array<PatternByte, 24> g_patternPatched = {{
        {0xB0, false}, {0x01, false}, {0xC3, false}, {0x28, false},  // our patch + rest of prologue
        {0x48, false}, {0x8B, false}, {0x0D, false},
        {0x00, true},  {0x00, true},  {0x00, true},  {0x00, true},
        {0x48, false}, {0x8B, false}, {0x01, false},
        {0xFF, false}, {0x90, false},
        {0x00, true},  {0x00, true},  {0x00, true},  {0x00, true},
        {0x84, false}, {0xC0, false},
        {0x75, false}, {0x0D, false}
    }};

    // ---- Module scan --------------------------------------------------------
    // Reads the entire client.dll into a local buffer and searches for the
    // pattern with wildcards. Returns the address of the first byte (start
    // of the function) if there is exactly 1 match, or 0 if none or multiple.
    uintptr_t FindIsHltvByPattern(const std::array<PatternByte, 24>& pattern) {
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
        const size_t patternLen = pattern.size();

        for (size_t i = 0; i + patternLen <= bytesRead; ++i) {
            bool ok = true;
            for (size_t j = 0; j < patternLen; ++j) {
                if (!pattern[j].wildcard && module[i + j] != pattern[j].value) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;

            // Match found. The function start address is:
            // module base + i
            match = mem.clientModule + i;
            ++matchCount;
            if (matchCount > 1) return 0;  // ambiguous, don't use
        }

        return matchCount == 1 ? match : 0;
    }

    // ---- Write N bytes to executable section --------------------------------
    // Like WriteExecutableByte from ThirdPerson but for 3 bytes.
    // Handles VirtualProtectEx (the .text section is read-only by default) and
    // FlushInstructionCache so the CPU sees the new instructions.
    bool WriteExecutableBytes(uintptr_t address,
                              const uint8_t* expected,
                              const uint8_t* replacement,
                              size_t size) {
        // Read the current bytes to confirm they match what we expect.
        std::vector<uint8_t> current(size);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(mem.hProcess,
                reinterpret_cast<LPCVOID>(address),
                current.data(), size, &bytesRead) ||
            bytesRead != size)
            return false;

        for (size_t i = 0; i < size; ++i) {
            if (current[i] != expected[i])
                return false;  // unexpected byte, don't touch
        }

        DWORD oldProtect = 0;
        if (!VirtualProtectEx(mem.hProcess,
                reinterpret_cast<LPVOID>(address), size,
                PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        SIZE_T bytesWritten = 0;
        bool ok = WriteProcessMemory(mem.hProcess,
                reinterpret_cast<LPVOID>(address),
                replacement, size,
                &bytesWritten) && bytesWritten == size;

        if (ok) {
            FlushInstructionCache(mem.hProcess,
                reinterpret_cast<LPCVOID>(address), size);
            Stats::rpmWriteCount.fetch_add(1);
        }

        // Restore the original page protection.
        DWORD ignored = 0;
        VirtualProtectEx(mem.hProcess,
            reinterpret_cast<LPVOID>(address), size, oldProtect, &ignored);

        return ok;
    }
}

// ---- Public API ----------------------------------------------------------

bool RunMoneyReveal() {
    if (!mem.hProcess) return false;
    if (g_mr.applied) return true;  // already active this session
    if (!mem.clientModule) return false;

    // Resolve the is_hltv address:
    // 1) Try normal pattern (unpatched function).
    // 2) If that fails, try fallback pattern (function already patched by
    //    a previous run that didn't restore).
    if (!g_mr.funcAddress && !g_mr.scanAttempted) {
        g_mr.scanAttempted = true;

        // Attempt 1: normal pattern (48 83 EC ...).
        uintptr_t found = FindIsHltvByPattern(g_pattern);
        if (found) {
            g_mr.funcAddress = found;
        } else {
            // Attempt 2: fallback pattern (B0 01 C3 ...).
            // If it matches, the function is already patched. Use it anyway
            // and set the theoretical original bytes (48 83 EC).
            found = FindIsHltvByPattern(g_patternPatched);
            if (found) {
                g_mr.funcAddress = found;
                g_mr.originalBytes[0] = 0x48;
                g_mr.originalBytes[1] = 0x83;
                g_mr.originalBytes[2] = 0xEC;
                g_mr.applied = true;  // already active, no need to re-patch
                return true;
            }
        }
    }

    if (!g_mr.funcAddress) return false;

    // Read the 3 current bytes at the function start.
    uint8_t current[3] = {};
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(mem.hProcess,
            reinterpret_cast<LPCVOID>(g_mr.funcAddress),
            current, 3, &bytesRead) || bytesRead != 3)
        return false;

    // Case 1: normal function (48 83 EC) -> patch it.
    // Case 2: already patched (B0 01 C3) -> already done.
    const uint8_t patchBytes[3] = { 0xB0, 0x01, 0xC3 };

    if (current[0] == 0xB0 && current[1] == 0x01 && current[2] == 0xC3) {
        // Already patched (likely by a previous run).
        g_mr.originalBytes[0] = 0x48;
        g_mr.originalBytes[1] = 0x83;
        g_mr.originalBytes[2] = 0xEC;
        g_mr.applied = true;
        return true;
    }

    if (current[0] != 0x48 || current[1] != 0x83 || current[2] != 0xEC) {
        // Unexpected bytes - don't touch.
        return false;
    }

    // Save the 3 original bytes so we can restore them.
    g_mr.originalBytes[0] = current[0];
    g_mr.originalBytes[1] = current[1];
    g_mr.originalBytes[2] = current[2];

    // Patch: 48 83 EC -> B0 01 C3.
    if (!WriteExecutableBytes(g_mr.funcAddress, current, patchBytes, 3))
        return false;

    g_mr.applied = true;
    return true;
}

void RestoreMoneyReveal() {
    if (!g_mr.applied || !mem.hProcess) return;
    if (!g_mr.funcAddress) return;

    // Restore the 3 original bytes.
    // If they are already restored (someone closed CS2 and reopened), do nothing.
    uint8_t current[3] = {};
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(mem.hProcess,
            reinterpret_cast<LPCVOID>(g_mr.funcAddress),
            current, 3, &bytesRead) && bytesRead == 3) {
        bool alreadyRestored = (current[0] == g_mr.originalBytes[0] &&
                                current[1] == g_mr.originalBytes[1] &&
                                current[2] == g_mr.originalBytes[2]);
        if (!alreadyRestored) {
            WriteExecutableBytes(g_mr.funcAddress,
                                 current, g_mr.originalBytes, 3);
        }
    }

    g_mr.applied = false;
}

bool IsMoneyRevealActive() {
    return g_mr.applied;
}
