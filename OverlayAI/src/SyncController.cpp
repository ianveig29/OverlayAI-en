#include "SyncController.h"

#include "Memory.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {
    constexpr ULONGLONG kHeartbeatIntervalMs = 2000;
    constexpr int kMaxMessagesPerPump = 16;

    std::string DetectGameStatus() {
        return (mem.hProcess && mem.clientModule) ? "in-game" : "online";
    }
}

struct SyncController::Impl {
    SyncNetClient client;
    std::string playerName;
    std::string lastSentStatus;
    std::vector<SyncPeerInfo> peers;
    uint64_t nextRequestId = 1;
    ULONGLONG lastHeartbeatMs = 0;
    bool started = false;

    void SendHello() {
        const std::string status = DetectGameStatus();
        const std::string message = BuildSyncHelloMessage(
            nextRequestId++, playerName, status);
        if (client.Send(message))
            lastSentStatus = status;
    }

    void SendHeartbeat(const std::string& status) {
        const std::string message = BuildSyncHeartbeatMessage(
            nextRequestId++, status);
        if (client.Send(message))
            lastSentStatus = status;
    }

    void ProcessInbound() {
        std::string frame;
        for (int processed = 0;
            processed < kMaxMessagesPerPump && client.TryReceive(frame);
            ++processed) {
            SyncMessage msg;
            if (!ParseSyncMessage(frame, msg))
                continue;

            if (msg.type == "sync.peer_list") {
                peers = std::move(msg.peers);
            } else if (msg.type == "sync.error") {
                std::fprintf(stderr, "[SyncController] Error del relay: %s - %s\n",
                    msg.errorCode.c_str(), msg.errorMessage.c_str());
            }
        }
    }
};

SyncController::SyncController() : impl_(std::make_unique<Impl>()) {}

SyncController::~SyncController() {
    Stop();
}

bool SyncController::Start(const char* host, uint16_t port,
    const char* playerName) {
    Stop();
    if (!host || !*host || !playerName || !*playerName) return false;

    impl_->playerName = playerName;
    impl_->peers.clear();
    impl_->nextRequestId = 1;
    impl_->lastHeartbeatMs = 0;
    impl_->lastSentStatus.clear();

    if (!impl_->client.Connect(host, port))
        return false;

    impl_->started = true;
    impl_->SendHello();
    impl_->lastHeartbeatMs = GetTickCount64();
    return true;
}

void SyncController::Stop() {
    if (!impl_->started) return;
    impl_->client.Disconnect();
    impl_->peers.clear();
    impl_->started = false;
    impl_->lastSentStatus.clear();
}

void SyncController::Pump() {
    if (!impl_->started) return;

    if (!impl_->client.IsConnected()) {
        impl_->peers.clear();
        impl_->started = false;
        return;
    }

    impl_->ProcessInbound();

    const ULONGLONG now = GetTickCount64();
    if (now - impl_->lastHeartbeatMs >= kHeartbeatIntervalMs) {
        const std::string currentStatus = DetectGameStatus();
        impl_->SendHeartbeat(currentStatus);
        impl_->lastHeartbeatMs = now;
    }
}

SyncStatus SyncController::GetStatus() const {
    SyncStatus status;
    status.connected = impl_->started && impl_->client.IsConnected();
    status.localStatus = DetectGameStatus();
    status.peers = impl_->peers;
    return status;
}
