#pragma once

// ============================================================
// PanoramaMount.h
// Panorama mount function declarations.
// ============================================================

#include <cstdint>

enum class PanoramaInventoryCommandType {
    None = 0,
    Equip = 1,
    CloseReveal = 2,
    DismissCollection = 3,
    NativeEquipItem = 4
};

struct PanoramaInventoryCommand {
    PanoramaInventoryCommandType type = PanoramaInventoryCommandType::None;
    std::uint64_t localId = 0;
};

bool InitializePanoramaMount(void* panoramaInterface) noexcept;
bool IsPanoramaProbeEnabled() noexcept;
void PublishPanoramaInventorySnapshot(const char* json) noexcept;
bool ConsumePanoramaInventoryCommand(
    PanoramaInventoryCommand& command) noexcept;
void RequeuePanoramaInventoryCommand(
    const PanoramaInventoryCommand& command) noexcept;
void RequestPanoramaRevealAcknowledged(std::uint64_t localId) noexcept;
void RequestPanoramaRevealRefresh() noexcept;
void RequestPanoramaCollection(std::uint64_t localId) noexcept;
void RequestPanoramaProbeCreate() noexcept;
void RequestPanoramaProbeDestroy() noexcept;
void RunPanoramaMountFrame() noexcept;
bool IsPanoramaProbeMounted() noexcept;
void ShutdownPanoramaMount() noexcept;
