#include "ViewPunch.h"
#include "Types.h"
#include "Memory.h"
#include "Offsets.h"
#include "Stats.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
    // Offset inside the ConVar object to the current-value float
    // (found with Cheat Engine on 12/09/2026: object + 0x58 = value).
    constexpr uintptr_t kConVarValueOffset = 0x58;
    // The ConVar's name: the anchor of the anti-update scan.
    constexpr char kConVarName[] = "view_punch_decay";
    constexpr size_t kConVarNameLen = sizeof(kConVarName); // includes the \0
    // Chunk size for reading memory while scanning client.dll.
    constexpr size_t kScanChunk = 1024 * 1024;
    // Scan retry every 5 seconds (same as the thirdperson module).
    constexpr ULONGLONG kScanRetryMs = 5000;

    // Internal module state. Anything captured once (the original value)
    // is stored here so we can restore it when the overlay shuts down.
    struct AntiViewPunchState {
        uintptr_t valueAddress = 0;  // address of the ConVar float
        float     originalValue = 0.f; // original value before writing
        bool      hasOriginal = false; // did we capture the original?
        bool      modified = false;    // did we write a different value?
        float     lastReadValue = -1.f; // last read (for the menu)
        int       lastFailReason = 0;   // last error (0 = none)
        // Anti-update resolution:
        //  refSlot: address of the static pointer that targets the ConVar
        //  object. We try the offsets.json key first; if that chain is
        //  broken (update), we scan client.dll for the ConVar name and
        //  rebuild the chain from scratch.
        uintptr_t refSlot = 0;
        int       resolveSource = 0;    // 0 = offsets key, 1 = scan
        ULONGLONG nextScanMs = 0;       // next scan retry
        // Diagnostics of the last scan (menu status line):
        int scanAttempts = 0;      // scans done in this session
        int scanStringMatches = 0; // times the name appeared in memory
        int scanRefCandidates = 0; // pointers to the name that were validated
    };
    AntiViewPunchState g_state;

    // Failure codes shown in the menu:
    //  1 = waiting for the game to be ready (not attached yet)
    //  2 = broken chain via the offsets key (scan runs as fallback)
    //  3 = the memory write failed
    //  4 = the float read does not look like a valid decay (dirty read)
    //  5 = scan: the ConVar name did not appear in client.dll
    //  6 = scan: name found but no valid reference
    //      (possible ConVar layout change: check the +0x58)

    // Reads a chunk of memory returning false if the page is unreadable.
    bool ReadChunk(uintptr_t address, uint8_t* buffer, size_t size) {
        SIZE_T read = 0;
        return ReadProcessMemory(mem.hProcess,
            reinterpret_cast<LPCVOID>(address), buffer, size, &read) &&
            read == size;
    }

    // Finds EVERY occurrence of the ConVar name inside a memory range.
    // Returns the addresses where the string starts.
    std::vector<uintptr_t> FindStringOccurrences(uintptr_t base, size_t size) {
        std::vector<uintptr_t> found;
        std::vector<uint8_t> buffer(kScanChunk + kConVarNameLen);
        for (size_t offset = 0; offset < size; offset += kScanChunk) {
            const size_t chunkSize = (kScanChunk + kConVarNameLen <= size - offset)
                ? (kScanChunk + kConVarNameLen) : (size - offset);
            if (chunkSize < kConVarNameLen) break;
            if (!ReadChunk(base + offset, buffer.data(), chunkSize)) continue;
            for (size_t i = 0; i + kConVarNameLen <= chunkSize; ++i) {
                if (std::memcmp(buffer.data() + i, kConVarName, kConVarNameLen) == 0) {
                    found.push_back(base + offset + i);
                    if (found.size() >= 16) return found; // sanity limit
                    i += kConVarNameLen - 1;
                }
            }
        }
        return found;
    }

    // Validates that "obj" is really the view_punch_decay ConVar object:
    // within its first 0x80 bytes there must be a pointer whose target
    // contains the ConVar name. This discards false positives (any other
    // structure pointing at the same place).
    bool ValidateConVarObject(uintptr_t obj) {
        if (!IsValidPtr(obj)) return false;
        uintptr_t words[16] = {};
        if (!ReadChunk(obj, reinterpret_cast<uint8_t*>(words), sizeof(words)))
            return false;
        char name[16];
        for (uintptr_t w : words) {
            if (!IsValidPtr(w)) continue;
            if (!ReadChunk(w, reinterpret_cast<uint8_t*>(name), sizeof(name)))
                continue;
            if (std::memcmp(name, kConVarName, sizeof(name)) == 0)
                return true;
        }
        return false;
    }

    // Anti-update scan: rebuilds the client.dll -> ConVar object -> value
    // chain without using the offsets.json key.
    //
    // How it works: the name "view_punch_decay" lives as a literal inside
    // client.dll. The client's ConVarRef registers a structure in the data
    // section containing [pointer to the name][pointer to the object].
    // We find the name, then look for pointers TO the name, and for each
    // candidate we read the neighboring slot (+/- 8 bytes) looking for the
    // object pointer. The object is validated by content (it contains
    // another pointer to the name) and the value float by sanity.
    //
    // Returns true if the chain was resolved.
    bool ResolveByScan() {
        g_state.scanAttempts += 1;
        const uintptr_t base = mem.clientModule;
        const size_t size = mem.clientModuleSize;
        if (!base || !size) return false;

        // Step 1: every occurrence of the name in client.dll.
        const std::vector<uintptr_t> names =
            FindStringOccurrences(base, size);
        g_state.scanStringMatches = static_cast<int>(names.size());
        if (names.empty()) { g_state.lastFailReason = 5; return false; }

        // Step 2: look for (8-aligned) pointers targeting the name.
        // Each one is a reference inside a registration structure.
        std::vector<uint8_t> buffer(kScanChunk + 8);
        for (size_t offset = 0; offset < size; offset += kScanChunk) {
            const size_t chunkSize = (kScanChunk + 8 <= size - offset)
                ? (kScanChunk + 8) : (size - offset);
            if (chunkSize < 8) break;
            if (!ReadChunk(base + offset, buffer.data(), chunkSize)) continue;
            for (size_t i = 0; i + 8 <= chunkSize; i += 8) {
                uintptr_t value = 0;
                std::memcpy(&value, buffer.data() + i, sizeof(value));
                bool isNamePtr = false;
                for (uintptr_t n : names)
                    if (value == n) { isNamePtr = true; break; }
                if (!isNamePtr) continue;
                // We found a pointer to the name at [base + offset + i].
                // The neighboring slot (+/- 8) should hold the pointer to
                // the ConVar object. We try both sides.
                const uintptr_t slotHere = base + offset + i;
                g_state.scanRefCandidates += 1;
                for (uintptr_t delta : { uintptr_t(8), uintptr_t(~uintptr_t(7)) }) {
                    const uintptr_t obj = mem.Read<uintptr_t>(slotHere + delta);
                    if (!ValidateConVarObject(obj)) continue;
                    // Object validated: store the slot targeting the object.
                    g_state.refSlot = slotHere + delta;
                    g_state.resolveSource = 1;
                    return true;
                }
            }
        }
        g_state.lastFailReason = 6;
        return false;
    }
}

void UpdateAntiViewPunch() {
    // No game attached yet: not an error, just wait.
    if (!mem.hProcess || !mem.clientModule) {
        g_state.lastFailReason = 1;
        return;
    }

    // Step 1: resolve the static pointer slot.
    // We try the offsets.json key first (fast); if the chain is broken by
    // an update we switch to the name-based scan (retry every 5s).
    if (g_state.refSlot == 0) {
        const uintptr_t keySlot = mem.clientModule + Offsets::dwViewPunchDecayConVar;
        const uintptr_t obj = mem.Read<uintptr_t>(keySlot);
        if (IsValidPtr(obj) && ValidateConVarObject(obj)) {
            g_state.refSlot = keySlot;
            g_state.resolveSource = 0;
        } else {
            const ULONGLONG now = GetTickCount64();
            if (now >= g_state.nextScanMs) {
                g_state.nextScanMs = now + kScanRetryMs;
                (void)ResolveByScan();
            }
            if (g_state.refSlot == 0) {
                if (g_state.lastFailReason != 5 && g_state.lastFailReason != 6)
                    g_state.lastFailReason = 2;
                return;
            }
        }
    }

    // Step 2: follow the chain refSlot -> object -> +0x58 -> float.
    const uintptr_t conVarObject = mem.Read<uintptr_t>(g_state.refSlot);
    if (!IsValidPtr(conVarObject)) {
        // The chain went stale mid-session (update or map): re-resolve.
        g_state.refSlot = 0;
        g_state.lastFailReason = 2;
        return;
    }
    const uintptr_t valueAddress = conVarObject + kConVarValueOffset;
    const float current = mem.Read<float>(valueAddress);
    // Validation: the default decay is 18 and nobody sets it to 0 or to
    // huge numbers. If we read anything weird, the address is not
    // trustworthy (better to not write at all than to corrupt the game).
    if (!(current > 0.f && current < 10000.f)) {
        g_state.lastFailReason = 4;
        return;
    }

    // Step 3: capture the ORIGINAL value once, so we can restore it later.
    // NOTE: if the decay was already modified by a previous session that
    // closed badly, this captures the modified value as "original".
    // Acceptable: it is the game's current value and still functional.
    if (!g_state.hasOriginal) {
        g_state.originalValue = current;
        g_state.hasOriginal = true;
    }
    g_state.valueAddress = valueAddress;
    g_state.lastReadValue = current;

    // Step 4: act based on the menu checkbox.
    if (!g_Aim.enableAntiViewPunch) {
        // Feature off: restore the original ONLY if WE were the ones who
        // changed it (if the player set it from the console on their own
        // sv_cheats server, we respect their decision).
        if (g_state.modified && current != g_state.originalValue) {
            if (mem.Write<float>(valueAddress, g_state.originalValue)) {
                Stats::rpmWriteCount.fetch_add(1);
                g_state.modified = false;
            } else {
                g_state.lastFailReason = 3;
                return;
            }
        }
        g_state.lastFailReason = 0;
        return;
    }

    // Feature on: write the target decay if it differs. Re-asserted every
    // frame, so if the game resets the ConVar (map change etc.) we put it
    // back without the player noticing anything.
    const float target = static_cast<float>(g_Aim.antiViewPunchDecay);
    if (current != target) {
        if (mem.Write<float>(valueAddress, target)) {
            Stats::rpmWriteCount.fetch_add(1);
            g_state.modified = true;
        } else {
            g_state.lastFailReason = 3;
            return;
        }
    }
    g_state.lastFailReason = 0;
}

void ShutdownAntiViewPunch() {
    // On overlay exit we leave the game as it was.
    if (g_state.modified && g_state.hasOriginal && g_state.valueAddress &&
        mem.hProcess) {
        (void)mem.Write<float>(g_state.valueAddress, g_state.originalValue);
        Stats::rpmWriteCount.fetch_add(1);
    }
    g_state = AntiViewPunchState{};
}

AntiViewPunchStatus GetAntiViewPunchStatus() {
    AntiViewPunchStatus s;
    s.failReason = g_state.lastFailReason;
    s.currentValue = g_state.lastReadValue;
    s.originalValue = g_state.originalValue;
    s.modified = g_state.modified;
    s.active = g_state.modified && g_state.lastFailReason == 0;
    s.resolveSource = g_state.resolveSource;
    s.scanStringMatches = g_state.scanStringMatches;
    s.scanRefCandidates = g_state.scanRefCandidates;
    return s;
}
