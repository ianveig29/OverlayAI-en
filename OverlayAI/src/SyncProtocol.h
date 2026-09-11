#pragma once

#include <cstdint>
#include <string>
#include <vector>

constexpr uint32_t kSyncProtocolVersion = 1;
constexpr uint32_t kSyncProtocolMaxFrameBytes = 64 * 1024;
constexpr uint16_t kSyncDefaultPort = 27099;

struct SyncPeerInfo {
    std::string name;
    std::string status;
};

struct SyncMessage {
    std::string type;
    uint64_t requestId = 0;
    // sync.hello / sync.heartbeat
    std::string playerName;
    std::string status;
    // sync.peer_list
    std::vector<SyncPeerInfo> peers;
    // sync.error
    std::string errorCode;
    std::string errorMessage;
};

std::string BuildSyncHelloMessage(
    uint64_t requestId, const std::string& playerName,
    const std::string& status);
std::string BuildSyncHeartbeatMessage(
    uint64_t requestId, const std::string& status);
std::string BuildSyncPeerListMessage(
    uint64_t requestId, const std::vector<SyncPeerInfo>& peers);
std::string BuildSyncErrorMessage(
    uint64_t requestId, const std::string& code,
    const std::string& message);

bool ParseSyncMessage(const std::string& payload, SyncMessage& out);
