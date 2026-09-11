#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct SyncNetStatus {
    bool connected = false;
    uint64_t receivedFrames = 0;
    uint64_t sentFrames = 0;
};

class SyncNetClient {
public:
    SyncNetClient();
    ~SyncNetClient();

    SyncNetClient(const SyncNetClient&) = delete;
    SyncNetClient& operator=(const SyncNetClient&) = delete;

    bool Connect(const char* host, uint16_t port);
    void Disconnect();
    bool IsConnected() const;
    bool TryReceive(std::string& frame);
    bool Send(const std::string& payload);
    SyncNetStatus GetStatus() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
