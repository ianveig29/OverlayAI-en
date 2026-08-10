// ============================================================
// InventoryPreview.cpp
// Shows a preview of inventory items in the menu.
// ============================================================

#include "InventoryPreview.h"

#include "Overlay.h"

#include <windows.h>
#include <d3d11.h>
#include <winhttp.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace {
    constexpr std::size_t kMaxDownloadBytes = 10 * 1024 * 1024;
    constexpr UINT kMaxImageDimension = 2048;
    constexpr std::size_t kMaxCachedTextures = 16;

    struct WorkerResult {
        int catalogIndex = -1;
        UINT width = 0;
        UINT height = 0;
        std::vector<unsigned char> pixels;
        std::string error;
    };

    struct CachedTexture {
        int catalogIndex = -1;
        UINT width = 0;
        UINT height = 0;
        unsigned long long lastUse = 0;
        ID3D11ShaderResourceView* view = nullptr;
    };

    std::thread g_worker;
    std::atomic<bool> g_workerDone{ false };
    std::mutex g_resultMutex;
    WorkerResult g_workerResult;
    std::vector<CachedTexture> g_cache;
    int g_requestedIndex = -1;
    int g_failedIndex = -1;
    std::string g_requestedUrl;
    std::string g_error;
    InventoryPreviewState g_state = InventoryPreviewState::Idle;
    unsigned long long g_useCounter = 0;

    std::wstring ToWide(const char* text) {
        if (!text || !*text) return {};
        const int required = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (required <= 1) return {};
        std::wstring wide(static_cast<std::size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), required);
        wide.pop_back();
        return wide;
    }

    bool DownloadUrl(const char* url, std::vector<unsigned char>& bytes, std::string& error) {
        const std::wstring wideUrl = ToWide(url);
        if (wideUrl.empty()) {
            error = "URL de imagen invalida.";
            return false;
        }

        URL_COMPONENTSW components{};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) {
            error = "No se pudo interpretar la URL.";
            return false;
        }

        const std::wstring host(components.lpszHostName, components.dwHostNameLength);
        std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
        if (components.dwExtraInfoLength > 0)
            path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
        if (path.empty()) path = L"/";

        HINTERNET session = WinHttpOpen(L"OverlayAI Inventory Preview/1.0",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            error = "No se pudo iniciar WinHTTP.";
            return false;
        }
        WinHttpSetTimeouts(session, 2500, 4000, 5000, 5000);

        HINTERNET connection = WinHttpConnect(
            session, host.c_str(), components.nPort, 0);
        const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS
            ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = connection
            ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)
            : nullptr;

        bool success = request &&
            WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr);

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (success) {
            success = WinHttpQueryHeaders(request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                WINHTTP_NO_HEADER_INDEX) && statusCode >= 200 && statusCode < 300;
        }

        while (success) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
                success = false;
                break;
            }
            if (available == 0) break;
            if (bytes.size() + available > kMaxDownloadBytes) {
                error = "La imagen supera el limite de 10 MB.";
                success = false;
                break;
            }
            const std::size_t previousSize = bytes.size();
            bytes.resize(previousSize + available);
            DWORD downloaded = 0;
            if (!WinHttpReadData(request, bytes.data() + previousSize,
                available, &downloaded)) {
                success = false;
                break;
            }
            bytes.resize(previousSize + downloaded);
        }

        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);

        if (!success || bytes.empty()) {
            if (error.empty())
                error = statusCode ? "El servidor rechazo la imagen." : "No se pudo descargar la imagen.";
            bytes.clear();
            return false;
        }
        return true;
    }

    template <typename T>
    void ReleaseCom(T*& object) {
        if (!object) return;
        object->Release();
        object = nullptr;
    }

    bool DecodeImage(const std::vector<unsigned char>& bytes, WorkerResult& result) {
        const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool uninitialize = SUCCEEDED(initializeResult);

        IWICImagingFactory* factory = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;

        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(
            const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()));
        if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromStream(
            stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
        if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
        if (SUCCEEDED(hr)) hr = frame->GetSize(&result.width, &result.height);
        if (SUCCEEDED(hr) && (result.width == 0 || result.height == 0 ||
            result.width > kMaxImageDimension || result.height > kMaxImageDimension)) {
            result.error = "Dimensiones de imagen no admitidas.";
            hr = E_INVALIDARG;
        }
        if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr)) hr = converter->Initialize(frame,
            GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(hr)) {
            const UINT stride = result.width * 4;
            result.pixels.resize(static_cast<std::size_t>(stride) * result.height);
            hr = converter->CopyPixels(nullptr, stride,
                static_cast<UINT>(result.pixels.size()), result.pixels.data());
        }

        ReleaseCom(converter);
        ReleaseCom(frame);
        ReleaseCom(decoder);
        ReleaseCom(stream);
        ReleaseCom(factory);
        if (uninitialize) CoUninitialize();

        if (FAILED(hr)) {
            result.pixels.clear();
            if (result.error.empty()) result.error = "Formato de imagen no compatible.";
            return false;
        }
        return true;
    }

    void DownloadWorker(int catalogIndex, std::string url) {
        WorkerResult result;
        result.catalogIndex = catalogIndex;
        std::vector<unsigned char> bytes;
        if (DownloadUrl(url.c_str(), bytes, result.error))
            (void)DecodeImage(bytes, result);
        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            g_workerResult = std::move(result);
        }
        g_workerDone.store(true, std::memory_order_release);
    }

    CachedTexture* FindCachedTexture(int catalogIndex) {
        for (CachedTexture& cached : g_cache) {
            if (cached.catalogIndex == catalogIndex) return &cached;
        }
        return nullptr;
    }

    bool UploadTexture(const WorkerResult& result) {
        if (!g_pd3dDevice || result.pixels.empty()) return false;

        D3D11_TEXTURE2D_DESC description{};
        description.Width = result.width;
        description.Height = result.height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initialData{};
        initialData.pSysMem = result.pixels.data();
        initialData.SysMemPitch = result.width * 4;

        ID3D11Texture2D* texture = nullptr;
        ID3D11ShaderResourceView* view = nullptr;
        HRESULT hr = g_pd3dDevice->CreateTexture2D(&description, &initialData, &texture);
        if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateShaderResourceView(texture, nullptr, &view);
        if (texture) texture->Release();
        if (FAILED(hr) || !view) return false;

        if (g_cache.size() >= kMaxCachedTextures) {
            auto oldest = std::min_element(g_cache.begin(), g_cache.end(),
                [](const CachedTexture& left, const CachedTexture& right) {
                    return left.lastUse < right.lastUse;
                });
            if (oldest != g_cache.end()) {
                if (oldest->view) oldest->view->Release();
                g_cache.erase(oldest);
            }
        }
        g_cache.push_back(CachedTexture{
            result.catalogIndex, result.width, result.height, ++g_useCounter, view
        });
        return true;
    }

    void PumpWorker() {
        if (!g_worker.joinable() ||
            !g_workerDone.load(std::memory_order_acquire))
            return;

        g_worker.join();
        g_workerDone.store(false, std::memory_order_release);
        WorkerResult result;
        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            result = std::move(g_workerResult);
            g_workerResult = {};
        }

        if (!result.pixels.empty() && UploadTexture(result)) {
            if (result.catalogIndex == g_requestedIndex) {
                g_state = InventoryPreviewState::Ready;
                g_error.clear();
            }
        } else if (result.catalogIndex == g_requestedIndex) {
            g_failedIndex = result.catalogIndex;
            g_state = InventoryPreviewState::Failed;
            g_error = result.error.empty() ? "No se pudo crear la textura." : result.error;
        }
    }

    void StartRequestedWorker() {
        if (g_worker.joinable() || g_requestedIndex < 0 ||
            g_requestedUrl.empty() || g_failedIndex == g_requestedIndex)
            return;
        g_state = InventoryPreviewState::Loading;
        g_workerDone.store(false, std::memory_order_release);
        g_worker = std::thread(DownloadWorker, g_requestedIndex, g_requestedUrl);
    }
}

void RequestInventoryPreview(int catalogIndex, const char* imageUrl) {
    PumpWorker();
    const std::string requestedUrl = imageUrl ? imageUrl : "";
    if (catalogIndex != g_requestedIndex || requestedUrl != g_requestedUrl) {
        g_requestedIndex = catalogIndex;
        g_requestedUrl = requestedUrl;
        g_failedIndex = -1;
        g_error.clear();
        g_state = g_requestedUrl.empty()
            ? InventoryPreviewState::Failed : InventoryPreviewState::Loading;
        if (g_requestedUrl.empty()) g_error = "Este articulo no tiene imagen.";
    }

    if (CachedTexture* cached = FindCachedTexture(g_requestedIndex)) {
        cached->lastUse = ++g_useCounter;
        g_state = InventoryPreviewState::Ready;
        return;
    }
    StartRequestedWorker();
}

ID3D11ShaderResourceView* GetInventoryPreviewTexture() {
    PumpWorker();
    CachedTexture* cached = FindCachedTexture(g_requestedIndex);
    return cached ? cached->view : nullptr;
}

InventoryPreviewInfo GetInventoryPreviewInfo() {
    PumpWorker();
    InventoryPreviewInfo info;
    info.state = g_state;
    info.catalogIndex = g_requestedIndex;
    if (CachedTexture* cached = FindCachedTexture(g_requestedIndex)) {
        info.width = static_cast<int>(cached->width);
        info.height = static_cast<int>(cached->height);
    }
    info.error = g_error.c_str();
    return info;
}

void RetryInventoryPreview() {
    g_failedIndex = -1;
    g_error.clear();
    StartRequestedWorker();
}

void ShutdownInventoryPreview() {
    if (g_worker.joinable()) g_worker.join();
    for (CachedTexture& cached : g_cache) {
        if (cached.view) cached.view->Release();
    }
    g_cache.clear();
    g_workerResult = {};
    g_requestedIndex = -1;
    g_requestedUrl.clear();
    g_error.clear();
    g_state = InventoryPreviewState::Idle;
}
