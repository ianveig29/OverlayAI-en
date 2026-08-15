#include "BuildCompatibility.h"

#include <strsafe.h>

namespace BridgeCompatibility {
    ModuleIdentity ReadModuleIdentity(HMODULE module) noexcept {
        ModuleIdentity identity;
        if (!module) return identity;

        const auto* base = reinterpret_cast<const unsigned char*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return identity;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return identity;

        identity.timestamp = nt->FileHeader.TimeDateStamp;
        identity.imageSize = nt->OptionalHeader.SizeOfImage;
        return identity;
    }

    uint64_t HashBytes(const void* data, std::size_t size) noexcept {
        constexpr uint64_t offsetBasis = 14695981039346656037ull;
        constexpr uint64_t prime = 1099511628211ull;
        if (!data || size == 0) return offsetBasis;

        uint64_t hash = offsetBasis;
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index) {
            hash ^= bytes[index];
            hash *= prime;
        }
        return hash;
    }

    bool FormatStartupDescriptor(char* output, std::size_t capacity,
        HMODULE client, HMODULE engine, HMODULE panorama) noexcept {
        if (!output || capacity == 0) return false;
        const ModuleIdentity clientIdentity = ReadModuleIdentity(client);
        const ModuleIdentity engineIdentity = ReadModuleIdentity(engine);
        const ModuleIdentity panoramaIdentity = ReadModuleIdentity(panorama);

        return SUCCEEDED(StringCchPrintfA(output, capacity,
            "Compatibility: bridge=%u protocol=%u storage=%u catalog=%u "
            "client={ts:0x%08lX,size:0x%lX} "
            "engine={ts:0x%08lX,size:0x%lX} "
            "panorama={ts:0x%08lX,size:0x%lX}",
            kBridgeVersion, kProtocolVersion, kStorageVersion, kCatalogVersion,
            static_cast<unsigned long>(clientIdentity.timestamp),
            static_cast<unsigned long>(clientIdentity.imageSize),
            static_cast<unsigned long>(engineIdentity.timestamp),
            static_cast<unsigned long>(engineIdentity.imageSize),
            static_cast<unsigned long>(panoramaIdentity.timestamp),
            static_cast<unsigned long>(panoramaIdentity.imageSize)));
    }
}

