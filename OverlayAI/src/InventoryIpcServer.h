#pragma once

// ============================================================
// InventoryIpcServer.h
// IPC server function declarations.
// ============================================================

#include <cstdint>
#include <memory>
#include <string>

constexpr const wchar_t* kInventoryIpcPipeName = L"\\\\.\\pipe\\OverlayAI.Inventory.v1";

struct InventoryIpcInboundFrame {
    uint64_t connectionId = 0;
    std::string payload;
};

struct InventoryIpcServerStatus {
    bool running = false;
    bool clientConnected = false;
    uint64_t connectionId = 0;
    uint64_t receivedFrames = 0;
    uint64_t sentFrames = 0;
    uint64_t rejectedFrames = 0;
};

class InventoryIpcServer {
public:
    InventoryIpcServer();
    ~InventoryIpcServer();

    InventoryIpcServer(const InventoryIpcServer&) = delete;
    InventoryIpcServer& operator=(const InventoryIpcServer&) = delete;

    bool Start();
    void Stop();
    bool TryReceive(InventoryIpcInboundFrame& frame);
    bool Send(uint64_t connectionId, std::string payload);
    InventoryIpcServerStatus GetStatus() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
