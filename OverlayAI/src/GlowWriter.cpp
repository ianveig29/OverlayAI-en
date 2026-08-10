// Glow writer implementation
#include "GlowInternal.h"
#include "Memory.h"
#include "Offsets.h"
#include "Stats.h"
#include <thread>
#include <unordered_map>

static std::mutex g_writeLock;
static bool g_writerRunning = false;
static std::thread g_writerThread;

// Coalesce queue by pawn: keep latest desired state per pawn
static void DrainAndWriteOnce() {
	std::vector<GlowUpdate> local;
	{
		std::lock_guard<std::mutex> lk(g_glowQueueMutex);
		if (g_glowQueue.empty()) return;
		local.swap(g_glowQueue);
	}

	// coalesce
	std::unordered_map<uintptr_t, GlowUpdate> latest;
	for (auto &u : local) latest[u.pawn] = u;
	Stats::glowQueueCoalescedCount.fetch_add((uint64_t) (local.size() - latest.size()));

	// perform writes serialized
	std::lock_guard<std::mutex> writeLk(g_writeLock);
	for (auto &kv : latest) {
		GlowUpdate u = kv.second;
		Stats::rpmWriteCount.fetch_add(1);
		uintptr_t pawn = u.pawn;
		if (!IsValidPtr(pawn)) continue;
		uintptr_t glowBase = pawn + Offsets::m_Glow;
		if (!u.enable) {
			mem.Write<bool>(glowBase + Offsets::glow_m_bGlowing, false);
			Stats::glowWritesTotal.fetch_add(1);
			continue;
		}
		// init if needed
		GlowState &st = g_prevGlow[pawn];
		if (!st.enabled) {
			mem.Write<int>(glowBase + Offsets::glow_m_iGlowType, 3);
			mem.Write<int>(glowBase + Offsets::glow_m_iGlowTeam, 0);
			mem.Write<int>(glowBase + Offsets::glow_m_nGlowRange, 3500);
			mem.Write<int>(glowBase + Offsets::glow_m_nGlowRangeMin, 0);
			GlowColorRGBA colorInit = { static_cast<uint8_t>(u.r), static_cast<uint8_t>(u.g), static_cast<uint8_t>(u.b), static_cast<uint8_t>(u.a) };
			mem.Write<GlowColorRGBA>(glowBase + Offsets::glow_m_glowColorOverride, colorInit);
			Vector3 glowVecInit = { u.r / 255.0f, u.g / 255.0f, u.b / 255.0f };
			mem.Write<Vector3>(glowBase + Offsets::glow_m_fGlowColor, glowVecInit);
			mem.Write<bool>(glowBase + Offsets::glow_m_bFlashing, false);
			mem.Write<bool>(glowBase + Offsets::glow_m_bEligibleForScreenHighlight, false);
			mem.Write<float>(pawn + Offsets::m_flGlowBackfaceMult, 1.0f);
			mem.Write<bool>(glowBase + Offsets::glow_m_bGlowing, true);
			st.enabled = true;
			st.r = (uint8_t)u.r; st.g = (uint8_t)u.g; st.b = (uint8_t)u.b; st.a = (uint8_t)u.a;
			st.glowing = true;
			Stats::glowWritesTotal.fetch_add(1);
			continue;
		}
		// already enabled: update color and re-assert glowing state
		GlowColorRGBA color = { static_cast<uint8_t>(u.r), static_cast<uint8_t>(u.g), static_cast<uint8_t>(u.b), static_cast<uint8_t>(u.a) };
		mem.Write<GlowColorRGBA>(glowBase + Offsets::glow_m_glowColorOverride, color);
		Vector3 glowVec = { u.r / 255.0f, u.g / 255.0f, u.b / 255.0f };
		mem.Write<Vector3>(glowBase + Offsets::glow_m_fGlowColor, glowVec);
		mem.Write<bool>(glowBase + Offsets::glow_m_bGlowing, true);
		st.r = (uint8_t)u.r; st.g = (uint8_t)u.g; st.b = (uint8_t)u.b; st.a = (uint8_t)u.a;
		st.glowing = true;
		Stats::glowWritesTotal.fetch_add(1);
	}
}

static void WriterLoop() {
	g_writerRunning = true;
	while (g_writerRunning) {
		DrainAndWriteOnce();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

void StartGlowWriter() {
	if (g_writerThread.joinable()) return;
	g_writerThread = std::thread(WriterLoop);
}

void StopGlowWriter() {
	g_writerRunning = false;
	if (g_writerThread.joinable()) g_writerThread.join();
}
