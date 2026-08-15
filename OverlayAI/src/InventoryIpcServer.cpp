#include "InventoryIpcServer.h"
#include "Localization.h"

#include "InventoryLog.h"
#include "InventoryProtocol.h"

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace {
    constexpr std::size_t kMaxQueuedFrames = 128;
    constexpr DWORD kPipeBufferBytes = kInventoryProtocolMaxFrameBytes + sizeof(uint32_t);

    class PipeSecurityContext {
    public:
        PipeSecurityContext() = default;
        ~PipeSecurityContext() {
            if (descriptor_) LocalFree(descriptor_);
        }

        PipeSecurityContext(const PipeSecurityContext&) = delete;
        PipeSecurityContext& operator=(const PipeSecurityContext&) = delete;

        bool Initialize() {
            HANDLE token = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
                return false;

            DWORD tokenBytes = 0;
            (void)GetTokenInformation(token, TokenUser, nullptr, 0, &tokenBytes);
            if (tokenBytes == 0) {
                CloseHandle(token);
                return false;
            }

            std::vector<unsigned char> tokenStorage(tokenBytes);
            const bool readToken = GetTokenInformation(
                token, TokenUser, tokenStorage.data(), tokenBytes, &tokenBytes) != FALSE;
            CloseHandle(token);
            if (!readToken) return false;

            const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(tokenStorage.data());
            LPWSTR userSidText = nullptr;
            if (!ConvertSidToStringSidW(tokenUser->User.Sid, &userSidText))
                return false;

            // Only this Windows user, administrators and SYSTEM may access the pipe.
            // The medium integrity label lets a normal CS2 process reach an elevated overlay.
            const std::wstring sddl =
                L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;" +
                std::wstring(userSidText) + L")S:(ML;;NW;;;ME)";
            LocalFree(userSidText);

            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                sddl.c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
                return false;

            attributes_.nLength = sizeof(attributes_);
            attributes_.lpSecurityDescriptor = descriptor_;
            attributes_.bInheritHandle = FALSE;
            return true;
        }

        SECURITY_ATTRIBUTES* Attributes() { return &attributes_; }

    private:
        PSECURITY_DESCRIPTOR descriptor_ = nullptr;
        SECURITY_ATTRIBUTES attributes_{};
    };

    bool ReadExact(HANDLE pipe, void* destination, DWORD size) {
        auto* bytes = static_cast<unsigned char*>(destination);
        DWORD completed = 0;
        while (completed < size) {
            DWORD read = 0;
            if (!ReadFile(pipe, bytes + completed, size - completed, &read, nullptr) || read == 0)
                return false;
            completed += read;
        }
        return true;
    }

    bool WriteExact(HANDLE pipe, const void* source, DWORD size) {
        const auto* bytes = static_cast<const unsigned char*>(source);
        DWORD completed = 0;
        while (completed < size) {
            DWORD written = 0;
            if (!WriteFile(pipe, bytes + completed, size - completed, &written, nullptr) ||
                written == 0)
                return false;
            completed += written;
        }
        return true;
    }
}

struct InventoryIpcServer::Impl {
    struct OutboundFrame {
        uint64_t connectionId = 0;
        std::string payload;
    };

    std::atomic<bool> stopRequested{ false };
    std::atomic<bool> running{ false };
    std::atomic<bool> startupComplete{ false };
    std::atomic<DWORD> startupError{ ERROR_SUCCESS };
    std::atomic<bool> clientConnected{ false };
    std::atomic<uint64_t> connectionId{ 0 };
    std::atomic<uint64_t> receivedFrames{ 0 };
    std::atomic<uint64_t> sentFrames{ 0 };
    std::atomic<uint64_t> rejectedFrames{ 0 };
    std::thread worker;
    std::mutex inboundMutex;
    std::deque<InventoryIpcInboundFrame> inboundFrames;
    std::mutex outboundMutex;
    std::condition_variable outboundReady;
    std::deque<OutboundFrame> outboundFrames;

    void Run() {
        PipeSecurityContext security;
        if (!security.Initialize()) {
            startupError.store(GetLastError());
            startupComplete.store(true);
            WriteInventoryLog(InventoryLogCategory::Transport, InventoryLogLevel::Error,
                Localized("No se pudo crear la seguridad local del pipe. Win32 error %lu.",
                    "Could not create local pipe security. Win32 error %lu."),
                startupError.load());
            return;
        }
        uint64_t nextConnectionId = 1;
        bool startupSignaled = false;
        while (!stopRequested.load()) {
            HANDLE pipe = CreateNamedPipeW(
                kInventoryIpcPipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1, kPipeBufferBytes, kPipeBufferBytes, 0, security.Attributes());
            if (pipe == INVALID_HANDLE_VALUE) {
                const DWORD error = GetLastError();
                if (!startupSignaled) {
                    startupError.store(error);
                    startupComplete.store(true);
                }
                WriteInventoryLog(InventoryLogCategory::Transport, InventoryLogLevel::Error,
                    Localized("CreateNamedPipeW fallo. Win32 error %lu.",
                        "CreateNamedPipeW failed. Win32 error %lu."), error);
                break;
            }
            if (!startupSignaled) {
                running.store(true);
                startupComplete.store(true);
                startupSignaled = true;
            }

            const BOOL connected = ConnectNamedPipe(pipe, nullptr)
                ? TRUE : GetLastError() == ERROR_PIPE_CONNECTED;
            if (!connected || stopRequested.load()) {
                CloseHandle(pipe);
                continue;
            }

            const uint64_t activeConnection = nextConnectionId++;
            connectionId.store(activeConnection);
            clientConnected.store(true);
            bool keepConnection = true;
            while (keepConnection && !stopRequested.load()) {
                uint32_t frameSize = 0;
                if (!ReadExact(pipe, &frameSize, sizeof(frameSize))) break;
                if (frameSize == 0 || frameSize > kInventoryProtocolMaxFrameBytes) {
                    rejectedFrames.fetch_add(1);
                    WriteInventoryLog(InventoryLogCategory::Transport, InventoryLogLevel::Warning,
                        Localized("Frame rechazado por tamano: %u bytes.",
                            "Frame rejected due to size: %u bytes."), frameSize);
                    break;
                }

                std::string payload(frameSize, '\0');
                if (!ReadExact(pipe, payload.data(), frameSize)) break;
                {
                    std::lock_guard<std::mutex> lock(inboundMutex);
                    if (inboundFrames.size() >= kMaxQueuedFrames) {
                        rejectedFrames.fetch_add(1);
                        break;
                    }
                    inboundFrames.push_back({ activeConnection, std::move(payload) });
                }
                receivedFrames.fetch_add(1);

                OutboundFrame response;
                {
                    std::unique_lock<std::mutex> lock(outboundMutex);
                    const bool available = outboundReady.wait_for(
                        lock, std::chrono::seconds(5), [&] {
                            return stopRequested.load() || std::any_of(
                                outboundFrames.begin(), outboundFrames.end(),
                                [activeConnection](const OutboundFrame& frame) {
                                    return frame.connectionId == activeConnection;
                                });
                        });
                    if (!available || stopRequested.load()) break;
                    const auto iterator = std::find_if(
                        outboundFrames.begin(), outboundFrames.end(),
                        [activeConnection](const OutboundFrame& frame) {
                            return frame.connectionId == activeConnection;
                        });
                    if (iterator == outboundFrames.end()) break;
                    response = std::move(*iterator);
                    outboundFrames.erase(iterator);
                }

                const uint32_t responseSize = static_cast<uint32_t>(response.payload.size());
                if (!WriteExact(pipe, &responseSize, sizeof(responseSize)) ||
                    !WriteExact(pipe, response.payload.data(), responseSize))
                    keepConnection = false;
                else
                    sentFrames.fetch_add(1);
            }

            clientConnected.store(false);
            connectionId.store(0);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            {
                std::lock_guard<std::mutex> lock(outboundMutex);
                outboundFrames.erase(std::remove_if(
                    outboundFrames.begin(), outboundFrames.end(),
                    [activeConnection](const OutboundFrame& frame) {
                        return frame.connectionId == activeConnection;
                    }), outboundFrames.end());
            }
        }
        clientConnected.store(false);
        connectionId.store(0);
        running.store(false);
    }
};

InventoryIpcServer::InventoryIpcServer() : impl_(std::make_unique<Impl>()) {}

InventoryIpcServer::~InventoryIpcServer() {
    Stop();
}

bool InventoryIpcServer::Start() {
    if (impl_->worker.joinable()) return impl_->running.load();
    impl_->stopRequested.store(false);
    impl_->startupComplete.store(false);
    impl_->startupError.store(ERROR_SUCCESS);
    impl_->worker = std::thread([this] { impl_->Run(); });
    for (int attempt = 0; attempt < 1000 && !impl_->startupComplete.load(); ++attempt)
        Sleep(1);
    const bool started = impl_->running.load();
    WriteInventoryLog(InventoryLogCategory::Transport,
        started ? InventoryLogLevel::Info : InventoryLogLevel::Error,
        started ? Localized("Servidor iniciado en OverlayAI.Inventory.v1.",
                            "Server started on OverlayAI.Inventory.v1.")
                : Localized("No se pudo iniciar el servidor IPC. Win32 error %lu.",
                            "Could not start the IPC server. Win32 error %lu."),
        started ? ERROR_SUCCESS : impl_->startupError.load());
    return started;
}

void InventoryIpcServer::Stop() {
    if (!impl_->worker.joinable()) return;
    impl_->stopRequested.store(true);
    impl_->outboundReady.notify_all();
    CancelSynchronousIo(impl_->worker.native_handle());

    HANDLE wakePipe = CreateFileW(
        kInventoryIpcPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (wakePipe != INVALID_HANDLE_VALUE) CloseHandle(wakePipe);
    impl_->worker.join();
    WriteInventoryLog(InventoryLogCategory::Transport, InventoryLogLevel::Info,
        Localized("Servidor detenido y recursos liberados.",
            "Server stopped and resources released."));

    std::lock_guard<std::mutex> inboundLock(impl_->inboundMutex);
    impl_->inboundFrames.clear();
    std::lock_guard<std::mutex> outboundLock(impl_->outboundMutex);
    impl_->outboundFrames.clear();
}

bool InventoryIpcServer::TryReceive(InventoryIpcInboundFrame& frame) {
    std::lock_guard<std::mutex> lock(impl_->inboundMutex);
    if (impl_->inboundFrames.empty()) return false;
    frame = std::move(impl_->inboundFrames.front());
    impl_->inboundFrames.pop_front();
    return true;
}

bool InventoryIpcServer::Send(uint64_t connectionId, std::string payload) {
    if (connectionId == 0 || payload.empty() ||
        payload.size() > kInventoryProtocolMaxFrameBytes)
        return false;
    {
        std::lock_guard<std::mutex> lock(impl_->outboundMutex);
        if (impl_->outboundFrames.size() >= kMaxQueuedFrames) return false;
        impl_->outboundFrames.push_back({ connectionId, std::move(payload) });
    }
    impl_->outboundReady.notify_one();
    return true;
}

InventoryIpcServerStatus InventoryIpcServer::GetStatus() const {
    InventoryIpcServerStatus status;
    status.running = impl_->running.load();
    status.clientConnected = impl_->clientConnected.load();
    status.connectionId = impl_->connectionId.load();
    status.receivedFrames = impl_->receivedFrames.load();
    status.sentFrames = impl_->sentFrames.load();
    status.rejectedFrames = impl_->rejectedFrames.load();
    return status;
}
