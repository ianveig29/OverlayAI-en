#include "AntiFlashPatch.h"
#include "Memory.h"
#include "Entity.h"
#include "Offsets.h"

// Non-intrusive antiflash: when called, write a safe value to the pawn flash alpha
// field to neutralize flash for the local player if possible. This uses existing
// mem.Write and the offsets already present in the project. We intentionally do
// not duplicate external reference logic: we resolve the local pawn via
// ResolveActiveLocal and attempt a single write to m_flFlashOverlayAlpha and
// fall back to m_flFlashMaxAlpha.

void RunAntiFlash() {
	if (!mem.hProcess || !mem.clientModule) return;

	uintptr_t entityList = GetEntityListBase();
	if (!IsValidPtr(entityList)) return;

	uintptr_t localController = 0;
	uintptr_t localPawn = 0;
	int localTeam = 0;
	int localPlayerIndex = -1;
	ResolveActiveLocal(entityList, g_entityStride, localController, localPawn, localTeam, localPlayerIndex);
	// If ResolveActiveLocal failed, try other common ways to obtain the local pawn
	if (!IsValidPtr(localPawn)) {
		// Try direct read of dwLocalPlayerPawn (some builds expose direct pointer)
		uintptr_t p = mem.Read<uintptr_t>(mem.clientModule + Offsets::dwLocalPlayerPawn);
		if (IsValidPtr(p)) localPawn = p;
	}
	if (!IsValidPtr(localPawn)) {
		// Reference sample used a hardcoded client-relative pointer; try that as a last resort.
		constexpr uintptr_t kRefLocalPlayerPtr = 0x02076A50; // from Antiflashreference.cpp
		uintptr_t p = mem.Read<uintptr_t>(mem.clientModule + kRefLocalPlayerPtr);
		if (IsValidPtr(p)) localPawn = p;
	}
	if (!IsValidPtr(localPawn)) return;

	// Try to replicate reference behaviour: write several flash-related fields.
	// Use a mix of zeros and high value (reference used 20) depending on field semantics.
	float zero = 0.0f;
	float high = 20.0f;

	// Attempt multiple writes; ignore individual failures, we try all plausible fields.
	(void)mem.Write<float>(localPawn + Offsets::m_flFlashOverlayAlpha, zero);
	(void)mem.Write<float>(localPawn + Offsets::m_flFlashMaxAlpha, high);
	(void)mem.Write<float>(localPawn + Offsets::m_flFlashDuration, zero);
	(void)mem.Write<float>(localPawn + Offsets::m_flLastSmokeOverlayAlpha, 0.0f);
	// Some builds track flashed amount in a different field
	(void)mem.Write<float>(localPawn + Offsets::m_flFlashedAmount, 0.0f);

	// Also attempt to clear a boolean 'flashing' flag if present (byte)
	// m_bFlashing may alias other bytes; write cautiously using a single byte value 0
	uint8_t notFlashing = 0;
	(void)mem.Write<uint8_t>(localPawn + Offsets::m_bFlashing, notFlashing);

	// End: no persistent state kept here; RunAntiFlash can be called on demand.
}
