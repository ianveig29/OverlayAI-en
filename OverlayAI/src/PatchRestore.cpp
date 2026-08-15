#include "PatchRestore.h"
#include "Memory.h"
#include <vector>
#include <mutex>
#include <Windows.h>

static std::vector<std::pair<uintptr_t, uint8_t>> g_patches;
static std::mutex g_patchMutex;

void RegisterSmokePatchRestore(uintptr_t address, uint8_t originalByte) {
	std::lock_guard<std::mutex> lg(g_patchMutex);
	g_patches.emplace_back(address, originalByte);
}

void RestoreAllPatches() {
	std::lock_guard<std::mutex> lg(g_patchMutex);
	for (auto& p : g_patches) {
		uintptr_t addr = p.first;
		uint8_t orig = p.second;
		if (mem.hProcess) {
			SIZE_T out = 0;
			WriteProcessMemory(mem.hProcess, reinterpret_cast<LPVOID>(addr), &orig, sizeof(orig), &out);
		}
	}
	g_patches.clear();
}
