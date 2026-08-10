#pragma once

#include <cstdint>

namespace OverlayAIPanoramaContract {
    inline constexpr uint32_t kFrontendAbiVersion = 2;
    inline constexpr uint32_t kInventoryProtocolVersion = 1;

    inline constexpr const char* kRootPanelId = "OverlayAILocalInventoryRoot";
    inline constexpr const char* kFrontendNamespace = "OverlayAILocalInventory";
    inline constexpr const char* kHostSendCallback = "OverlayAIPanoramaBridgeSend";

    enum class BridgeState : uint8_t {
        Unavailable,
        InterfaceResolved,
        UiContextReady,
        PanelMounted,
        Connected
    };

    constexpr bool IsFrontendCompatible(uint32_t frontendAbiVersion) {
        return frontendAbiVersion == kFrontendAbiVersion;
    }
}
