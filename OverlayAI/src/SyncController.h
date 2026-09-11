#pragma once

#include "SyncNet.h"
#include "SyncProtocol.h"

#include <memory>
#include <string>
#include <vector>

struct SyncStatus {
    bool connected = false;
    std::string localStatus;
    std::vector<SyncPeerInfo> peers;
};

class SyncController {
public:
    SyncController();
    ~SyncController();

    SyncController(const SyncController&) = delete;
    SyncController& operator=(const SyncController&) = delete;

    bool Start(const char* host, uint16_t port, const char* playerName);
    void Stop();
    void Pump();
    SyncStatus GetStatus() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
