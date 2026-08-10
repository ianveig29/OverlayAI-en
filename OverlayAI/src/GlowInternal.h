#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "Types.h"

struct GlowColorRGBA { uint8_t r,g,b,a; };

struct GlowState {
	bool enabled = false;
	uint8_t r = 0, g = 0, b = 0, a = 0;
	bool glowing = false;
	uint64_t lastUpdatedMs = 0;
};

struct GlowUpdate { uintptr_t pawn; bool enable; int r,g,b,a; uint64_t ts; };

// shared globals
extern std::unordered_map<uintptr_t, GlowState> g_prevGlow;
extern std::mutex g_glowQueueMutex;
extern std::vector<GlowUpdate> g_glowQueue;

uint64_t NowMs();
