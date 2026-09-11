#include "SyncProtocol.h"

#include "third_party/nlohmann/json.hpp"

using Json = nlohmann::json;

namespace {
    Json MakeEnvelope(const char* type, uint64_t requestId, Json payload) {
        return Json{
            {"protocol_version", kSyncProtocolVersion},
            {"message_type", type ? type : "sync.error"},
            {"request_id", requestId},
            {"payload", std::move(payload)}
        };
    }
}

std::string BuildSyncHelloMessage(
    uint64_t requestId, const std::string& playerName,
    const std::string& status) {
    return MakeEnvelope("sync.hello", requestId, {
        {"player_name", playerName},
        {"status", status}
    }).dump();
}

std::string BuildSyncHeartbeatMessage(
    uint64_t requestId, const std::string& status) {
    return MakeEnvelope("sync.heartbeat", requestId, {
        {"status", status}
    }).dump();
}

std::string BuildSyncPeerListMessage(
    uint64_t requestId, const std::vector<SyncPeerInfo>& peers) {
    Json peerArray = Json::array();
    for (const SyncPeerInfo& peer : peers) {
        peerArray.push_back({
            {"name", peer.name},
            {"status", peer.status}
        });
    }
    return MakeEnvelope("sync.peer_list", requestId, {
        {"peers", std::move(peerArray)}
    }).dump();
}

std::string BuildSyncErrorMessage(
    uint64_t requestId, const std::string& code,
    const std::string& message) {
    return MakeEnvelope("sync.error", requestId, {
        {"code", code},
        {"message", message}
    }).dump();
}

bool ParseSyncMessage(const std::string& payload, SyncMessage& out) {
    out = {};
    if (payload.empty() || payload.size() > kSyncProtocolMaxFrameBytes)
        return false;

    const Json root = Json::parse(payload, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return false;
    if (!root.contains("protocol_version") || !root.contains("message_type") ||
        !root.contains("request_id") || !root.contains("payload"))
        return false;

    const uint32_t version = root.value("protocol_version", 0u);
    if (version != kSyncProtocolVersion)
        return false;

    out.requestId = root.value("request_id", 0ULL);
    out.type = root.value("message_type", "");
    if (out.type.empty() || !root.at("payload").is_object())
        return false;

    const Json& data = root.at("payload");

    if (out.type == "sync.hello") {
        out.playerName = data.value("player_name", "");
        out.status = data.value("status", "online");
        return !out.playerName.empty();
    }
    if (out.type == "sync.heartbeat") {
        out.status = data.value("status", "online");
        return true;
    }
    if (out.type == "sync.peer_list") {
        if (!data.contains("peers") || !data.at("peers").is_array())
            return false;
        for (const Json& peer : data.at("peers")) {
            SyncPeerInfo info;
            info.name = peer.value("name", "");
            info.status = peer.value("status", "offline");
            if (!info.name.empty())
                out.peers.push_back(std::move(info));
        }
        return true;
    }
    if (out.type == "sync.error") {
        out.errorCode = data.value("code", "");
        out.errorMessage = data.value("message", "");
        return true;
    }

    return false;
}
