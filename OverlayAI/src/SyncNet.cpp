#include "SyncNet.h"
#include "SyncProtocol.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace {
    constexpr std::size_t kMaxQueuedFrames = 128;

    class WinsockInit {
    public:
        WinsockInit() {
            WSADATA data{};
            initialized_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }
        ~WinsockInit() { if (initialized_) WSACleanup(); }
        bool Ok() const { return initialized_; }
    private:
        bool initialized_ = false;
    };

    bool RecvExact(SOCKET sock, void* destination, int size) {
        auto* bytes = static_cast<unsigned char*>(destination);
        int completed = 0;
        while (completed < size) {
            const int received = recv(sock, reinterpret_cast<char*>(bytes + completed),
                size - completed, 0);
            if (received <= 0) return false;
            completed += received;
        }
        return true;
    }

    bool SendExact(SOCKET sock, const void* source, int size) {
        const auto* bytes = static_cast<const unsigned char*>(source);
        int completed = 0;
        while (completed < size) {
            const int sent = send(sock, reinterpret_cast<const char*>(bytes + completed),
                size - completed, 0);
            if (sent <= 0) return false;
            completed += sent;
        }
        return true;
    }
}

struct SyncNetClient::Impl {
    WinsockInit wsa;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> connected{false};
    std::atomic<uint64_t> receivedFrames{0};
    std::atomic<uint64_t> sentFrames{0};
    SOCKET sock = INVALID_SOCKET;
    std::thread recvThread;

    std::mutex inboundMutex;
    std::deque<std::string> inboundFrames;

    std::mutex sendMutex;

    void RecvLoop() {
        while (!stopRequested.load()) {
            uint32_t frameSize = 0;
            if (!RecvExact(sock, &frameSize, sizeof(frameSize)))
                break;
            if (frameSize == 0 || frameSize > kSyncProtocolMaxFrameBytes)
                break;

            std::string payload(frameSize, '\0');
            if (!RecvExact(sock, payload.data(), static_cast<int>(frameSize)))
                break;

            {
                std::lock_guard<std::mutex> lock(inboundMutex);
                if (inboundFrames.size() >= kMaxQueuedFrames)
                    inboundFrames.pop_front();
                inboundFrames.push_back(std::move(payload));
            }
            receivedFrames.fetch_add(1);
        }
        connected.store(false);
    }
};

SyncNetClient::SyncNetClient() : impl_(std::make_unique<Impl>()) {}

SyncNetClient::~SyncNetClient() {
    Disconnect();
}

bool SyncNetClient::Connect(const char* host, uint16_t port) {
    Disconnect();
    if (!impl_->wsa.Ok() || !host || !*host) return false;

    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portStr[16]{};
    snprintf(portStr, sizeof(portStr), "%u", port);

    struct addrinfo* result = nullptr;
    if (getaddrinfo(host, portStr, &hints, &result) != 0 || !result)
        return false;

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        return false;
    }

    // Set a 5-second connect timeout via non-blocking + select.
    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);

    const int connectResult = connect(sock, result->ai_addr,
        static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

    if (connectResult == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            closesocket(sock);
            return false;
        }
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(sock, &writeSet);
        timeval timeout{};
        timeout.tv_sec = 5;
        if (select(0, nullptr, &writeSet, nullptr, &timeout) <= 0) {
            closesocket(sock);
            return false;
        }
        // Check for actual connection errors.
        int sockError = 0;
        int optLen = sizeof(sockError);
        getsockopt(sock, SOL_SOCKET, SO_ERROR,
            reinterpret_cast<char*>(&sockError), &optLen);
        if (sockError != 0) {
            closesocket(sock);
            return false;
        }
    }

    // Switch back to blocking mode for recv/send.
    nonBlocking = 0;
    ioctlsocket(sock, FIONBIO, &nonBlocking);

    impl_->sock = sock;
    impl_->stopRequested.store(false);
    impl_->connected.store(true);
    impl_->recvThread = std::thread([this] { impl_->RecvLoop(); });
    return true;
}

void SyncNetClient::Disconnect() {
    impl_->stopRequested.store(true);
    if (impl_->sock != INVALID_SOCKET) {
        shutdown(impl_->sock, SD_BOTH);
        closesocket(impl_->sock);
        impl_->sock = INVALID_SOCKET;
    }
    if (impl_->recvThread.joinable())
        impl_->recvThread.join();
    impl_->connected.store(false);

    std::lock_guard<std::mutex> lock(impl_->inboundMutex);
    impl_->inboundFrames.clear();
}

bool SyncNetClient::IsConnected() const {
    return impl_->connected.load();
}

bool SyncNetClient::TryReceive(std::string& frame) {
    std::lock_guard<std::mutex> lock(impl_->inboundMutex);
    if (impl_->inboundFrames.empty()) return false;
    frame = std::move(impl_->inboundFrames.front());
    impl_->inboundFrames.pop_front();
    return true;
}

bool SyncNetClient::Send(const std::string& payload) {
    if (!impl_->connected.load() || payload.empty() ||
        payload.size() > kSyncProtocolMaxFrameBytes)
        return false;

    const uint32_t frameSize = static_cast<uint32_t>(payload.size());
    std::lock_guard<std::mutex> lock(impl_->sendMutex);
    if (!SendExact(impl_->sock, &frameSize, sizeof(frameSize)) ||
        !SendExact(impl_->sock, payload.data(), static_cast<int>(frameSize))) {
        impl_->connected.store(false);
        return false;
    }
    impl_->sentFrames.fetch_add(1);
    return true;
}

SyncNetStatus SyncNetClient::GetStatus() const {
    SyncNetStatus status;
    status.connected = impl_->connected.load();
    status.receivedFrames = impl_->receivedFrames.load();
    status.sentFrames = impl_->sentFrames.load();
    return status;
}
