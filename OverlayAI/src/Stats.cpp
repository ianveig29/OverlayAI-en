// ============================================================
// Stats.cpp
// Displays statistics on screen (FPS, player count, diagnostic data).
// ============================================================

#include "Stats.h"

namespace Stats {
	std::atomic<uint64_t> rpmReadCount{0};
	std::atomic<uint64_t> rpmWriteCount{0};
	std::atomic<uint64_t> glowQueueEnqueueCount{0};
	std::atomic<uint64_t> glowQueueCoalescedCount{0};
	std::atomic<uint64_t> glowQueueSkipCount{0};
	std::atomic<uint64_t> glowWritesTotal{0};
	std::atomic<double> lastSnapshotBuildMs{0.0};
	std::atomic<double> lastRenderMs{0.0};
	std::atomic<int> weaponResolvedCount{0};
	std::atomic<int> weaponFailedCount{0};
	std::atomic<int> weaponLastDefinition{0};
	std::atomic<int> weaponLastClip{-1};
}
