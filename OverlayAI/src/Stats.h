#pragma once
#include <atomic>

namespace Stats {
	extern std::atomic<uint64_t> rpmReadCount; // ReadProcessMemory calls counted in snapshots
	extern std::atomic<uint64_t> rpmWriteCount; // WriteProcessMemory calls from glow writer
	extern std::atomic<uint64_t> glowQueueEnqueueCount; // enqueues attempted
	extern std::atomic<uint64_t> glowQueueCoalescedCount; // coalesced/merged
	extern std::atomic<uint64_t> glowQueueSkipCount; // skipped because no-change
	extern std::atomic<uint64_t> glowWritesTotal; // total writes performed
	extern std::atomic<double> lastSnapshotBuildMs;
	extern std::atomic<double> lastRenderMs;
	extern std::atomic<int> weaponResolvedCount;
	extern std::atomic<int> weaponFailedCount;
	extern std::atomic<int> weaponLastDefinition;
	extern std::atomic<int> weaponLastClip;
}
