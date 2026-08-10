#pragma once

// ============================================================
// PatchRestore.h
// Patch/restore function declarations.
// ============================================================

#include <cstdint>

// Register a single-byte patch to be restored on shutdown
void RegisterSmokePatchRestore(uintptr_t address, uint8_t originalByte);
// Run restore of all registered patch bytes (called on shutdown)
void RestoreAllPatches();
