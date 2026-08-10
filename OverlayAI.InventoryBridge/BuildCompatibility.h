#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace BridgeCompatibility {
    inline constexpr uint32_t kBridgeVersion = 3;
    inline constexpr uint32_t kProtocolVersion = 1;
    inline constexpr uint32_t kStorageVersion = 5;
    inline constexpr uint32_t kCatalogVersion = 1;

    struct ModuleIdentity {
        uint32_t timestamp = 0;
        uint32_t imageSize = 0;
    };

    ModuleIdentity ReadModuleIdentity(HMODULE module) noexcept;
    uint64_t HashBytes(const void* data, std::size_t size) noexcept;
    bool FormatStartupDescriptor(char* output, std::size_t capacity,
        HMODULE client, HMODULE engine, HMODULE panorama) noexcept;
}
