#include <windows.h>
#include <iostream>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <dwmapi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

#include "Config.h"
#include "ConsoleUi.h"
#include "DumperValidator.h"
#include "Esp.h"
#include "Memory.h"
#include "Menu.h"
#include "WeaponIcons.h"
#include "Offsets.h"
#include "Overlay.h"
#include "Triggerbot.h"
#include "Aimlock.h"
#include "Bhop.h"
#include "ThirdPerson.h"
#include "MoneyReveal.h"
#include "Glow.h"
#include "Entity.h"
#include "AntiFlash.h"
#include "PunchView.h"
#include "RecoilControl.h"
#include "AntiSmoke.h"
#include "SmokeColor.h"
#include "OtherGlow.h"
#include "RadarHack.h"
#include "Crosshair.h"
#include "GrenadeTrajectory.h"
#include "SpectatorList.h"
#include "InventoryChanger.h"
#include "InventoryIpcController.h"
#include "BridgeRuntimeLogMonitor.h"
#include "InventoryPreview.h"
#include "Localization.h"
#include "ModelDiagnostics.h"
#include "TextFonts.h"
#include "UiAppearance.h"
#include "resource.h"
#include <cstdio>
#include <cstring>
#include <dxgi.h>

namespace {
    struct GameWindowSearch {
        DWORD pid = 0;
        HWND window = nullptr;
    };

    BOOL CALLBACK FindGameWindow(HWND window, LPARAM parameter) {
        GameWindowSearch* search = reinterpret_cast<GameWindowSearch*>(parameter);
        DWORD windowPid = 0;
        GetWindowThreadProcessId(window, &windowPid);
        if (windowPid != search->pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
            return TRUE;

        search->window = window;
        return FALSE;
    }

    HWND GetGameWindow() {
        static HWND gameWindow = nullptr;
        DWORD windowPid = 0;
        if (gameWindow)
            GetWindowThreadProcessId(gameWindow, &windowPid);
        if (!gameWindow || !IsWindow(gameWindow) || windowPid != mem.pid) {
            GameWindowSearch search{ mem.pid, nullptr };
            EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&search));
            gameWindow = search.window;
        }
        return gameWindow;
    }

    bool IsOverlayPresentationActive() {
        const HWND gameWindow = GetGameWindow();
        if (!gameWindow || IsIconic(gameWindow) || !IsWindowVisible(gameWindow))
            return false;

        const HWND foregroundWindow = GetForegroundWindow();
        if (!foregroundWindow || IsIconic(foregroundWindow) || !IsWindowVisible(foregroundWindow))
            return false;

        DWORD foregroundPid = 0;
        GetWindowThreadProcessId(foregroundWindow, &foregroundPid);
        if (foregroundPid == mem.pid)
            return true;

        return g_MenuOpen && foregroundWindow == g_OverlayHwnd;
    }
}



int main(int argc, char** argv) {
    InitializeLocalization();
    InitializeUiAppearance();
    ConsoleUi::Initialize();
    (void)EnsureDumperAvailableInteractive();
    ConsoleUi::ReportWaitingForGame();
    while (!mem.Attach(L"cs2.exe")) {
        Sleep(1000);
    }
    ConsoleUi::ReportGameAttached(mem.pid, mem.clientModule);

    InitializeOffsetSystem();
    LoadEspConfig("esp_config.ini");
    EnsureConfigStorage();
    const std::string preload = LoadPreloadConfig();
    if (!preload.empty()) LoadConfigPreset(preload);
    ConsoleUi::ReportOffsetsLoaded();

    InventoryIpcController inventoryIpc;
    const bool inventoryIpcStarted = inventoryIpc.Start();
    ConsoleUi::ReportIpcStarted(inventoryIpcStarted);

    const bool modelDiagnosticMode = argc > 1 &&
        strcmp(argv[1], "--model-diagnostics") == 0;
    if (modelDiagnosticMode) {
        SetModelDiagnosticsEnabled(true);
        for (int sample = 0; sample < 5; ++sample) {
            RefreshGlobalSnapshot();
            UpdateModelDiagnostics();
            Sleep(800);
        }
        const ModelDiagnostics& diagnostics = GetModelDiagnostics();
        std::cout << "MODEL_DIAGNOSTICS\n";
        std::cout << "samples=" << diagnostics.samples
            << " read_failures=" << diagnostics.readFailures
            << " hitbox_set=" << diagnostics.hitboxSet << "\n";
        std::cout << "pawn=0x" << std::hex << diagnostics.pawn
            << " scene=0x" << diagnostics.sceneNode
            << " handle=0x" << diagnostics.modelHandle
            << " data=0x" << diagnostics.modelData << std::dec << "\n";
        std::cout << "candidates=" << diagnostics.validCandidates
            << " quaternion_valid=" << (diagnostics.boneRotationValid ? 1 : 0)
            << " quaternion_norm=" << diagnostics.boneRotationNorm << "\n";
        std::cout << "model_bones=" << diagnostics.modelBoneCount
            << " external_parts=" << diagnostics.externalPartCount << "\n";
        std::cout << "hitbox_sets=" << diagnostics.modelHitboxSetCount
            << " hitboxes=" << diagnostics.modelHitboxCount
            << " hitbox_offset=0x" << std::hex << diagnostics.hitboxListOffset
            << " hitbox_root=0x" << diagnostics.hitboxListRoot << std::dec << "\n";
        std::cout << "first_hitbox_bone=" << diagnostics.firstHitboxBone << "\n";
        std::cout << "model=" << diagnostics.modelName << "\n";
        std::cout << "resource=" << diagnostics.resourceName << "\n";
        return diagnostics.samples > 0 ? 0 : 2;
    }

    const bool inventoryIpcDiagnosticMode = argc > 1 &&
        strcmp(argv[1], "--inventory-ipc-diagnostics") == 0;
    if (inventoryIpcDiagnosticMode) {
        const ULONGLONG deadline = GetTickCount64() + 15000;
        while (GetTickCount64() < deadline) {
            inventoryIpc.Pump();
            PumpBridgeRuntimeLog();
            Sleep(2);
        }
        return 0;
    }

    ConsoleUi::BeginInteractiveMode();

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const HICON appIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_OVERLAYAI_APP));
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = appIcon;
    wc.hIconSm = appIcon;
    wc.lpszClassName = L"OverlayClass";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"OverlayClass", L"OverlayAI Canvas",
        WS_POPUP,
        0, 0, screenWidth, screenHeight,
        nullptr, nullptr, wc.hInstance, nullptr
    );
    g_OverlayHwnd = hwnd;

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = screenWidth;
    sd.BufferDesc.Height = screenHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    IDXGIDevice1* dxgiDevice = nullptr;
    if (SUCCEEDED(g_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgiDevice)))) {
        dxgiDevice->SetMaximumFrameLatency(1);
        dxgiDevice->Release();
    }
    CreateRenderTarget();

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ApplyOverlayUiStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    InitializeTextFonts();
    InitializeOtherGlow(g_pd3dDevice, g_pd3dDeviceContext);
    InitializeWeaponIconFont();
    ImGui_ImplWin32_EnableAlphaCompositing(hwnd);

    bool running = true;
    bool insertWasDown = false;
    bool tpWasDown = false;
    MSG msg;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        ConsoleUi::PollInput();
        if (ConsoleUi::IsShutdownRequested()) {
            running = false;
            break;
        }

        PollMenuKeyBind();
        PollPanicKeyBind();
        if (ConsumePanicShutdownRequest()) {
            running = false;
            break;
        }

        // Detect if CS2 has exited and close OverlayAI automatically.
        // GetExitCodeProcess returns STILL_ACTIVE while the process exists.
        // If the process has terminated, OverlayAI shuts down cleanly
        // (all restore functions run in the cleanup section below).
        if (g_App.autoCloseOnGameExit && mem.hProcess) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(mem.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                running = false;
                break;
            }
        }
        PollTriggerbotKeyBind();
        PollAimKeyBind();
        PollBhopKeyBind();
        PollThirdPersonKeyBind();
        bool menuKeyDown = (GetAsyncKeyState(g_App.menuToggleVk) & 0x8000) != 0;
        if (menuKeyDown && !insertWasDown)
            g_MenuOpen = !g_MenuOpen;

        // Third-person toggle key (edge detection, like the menu).
        // The key ONLY works while the checkbox (master enable) is ON,
        // and it does NOT touch the checkbox: it toggles the camera directly.
        // While a new key is being captured, the toggle is ignored.
        bool tpKeyDown = (GetAsyncKeyState(g_Esp.thirdPersonKeyVk) & 0x8000) != 0;
        if (tpKeyDown && !tpWasDown && g_Esp.enableThirdperson && !g_Esp.waitingForThirdPersonKey) {
            if (IsThirdPersonActive())
                RestoreThirdPerson();
            else
                (void)RunThirdPerson();
        }
        tpWasDown = tpKeyDown;
        insertWasDown = menuKeyDown;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        static bool menuInputActive = false;
        if (g_MenuOpen)
            RenderEspMenu();
        if (ConsumePanicShutdownRequest()) {
            ImGui::EndFrame();
            running = false;
            break;
        }
        // Always render persistent windows such as bomb and spectator info
        RenderPersistentWindows();
        if (g_Esp.enableGlow && g_Esp.enableOtherGlow) {
            g_Esp.enableGlow = false;
            ShutdownGlow();
        }
        if (g_MenuOpen != menuInputActive) {
            SetMenuInputMode(g_MenuOpen);
            menuInputActive = g_MenuOpen;
        }

        const ULONGLONG frameNowMs = GetTickCount64();
        static ULONGLONG lastSnapshotUpdateMs = 0;
        static ULONGLONG lastGlowUpdateMs = 0;
        static bool frameReady = false;
        const ULONGLONG snapshotIntervalMs = (g_Aim.enabled || g_Triggerbot.enabled)
            ? 4
            : (g_MenuOpen ? 12 : 8);
        if (!frameReady || frameNowMs - lastSnapshotUpdateMs >= snapshotIntervalMs) {
            if (RefreshGlobalSnapshot()) {
                frameReady = true;
                lastSnapshotUpdateMs = frameNowMs;
            }
        }
        if (frameReady && frameNowMs - lastGlowUpdateMs >= 16) {
            RunGlow();
            lastGlowUpdateMs = frameNowMs;
        }
        UpdateRadarHack();
        inventoryIpc.Pump();
        PumpBridgeRuntimeLog();
        UpdateInventoryChanger();
        UpdateFlashState();
        UpdateModelDiagnostics();
        if (g_Esp.enableAntiSmoke)
            (void)RunAntiSmoke();
        else
            RestoreAntiSmoke();
        UpdateSmokeColors(g_Esp.enableSmokeColor, Vector3{
            static_cast<float>(g_Esp.smokeColorR),
            static_cast<float>(g_Esp.smokeColorG),
            static_cast<float>(g_Esp.smokeColorB)
        });
        RunAimlock(screenWidth, screenHeight);
        RunTriggerbot();
        // Apply Anti Flash only while its explicit setting is enabled.
        if (g_Esp.enableAntiFlashbang) {
            RunAntiFlash(static_cast<float>(g_Esp.antiFlashOpacityPercent));
        }

        // If anti-flash was disabled, ensure any overrides are restored
        if (!g_Esp.enableAntiFlashbang) {
            RestoreAntiFlashOverrides();
        }
        RunBhop();

        // RCS: compensates recoil into the view angles. Runs FIRST to
        // read the REAL punch before Quit Aim Punch zeroes it out.
        RunRCS();

        // Quit Punchview: neutralizes the damage camera kick while
        // enabled. Called every frame (same as Anti Flash).
        RunQuitPunchview();
        RunQuitAimPunch();

        // Third person: the checkbox is the master enable.
        // Checking it activates the camera right away; unchecking
        // restores it. The key toggles the camera without touching the checkbox.
        static bool tpMasterPrev = false;
        if (g_Esp.enableThirdperson != tpMasterPrev) {
            tpMasterPrev = g_Esp.enableThirdperson;
            if (g_Esp.enableThirdperson)
                (void)RunThirdPerson();
            else
                RestoreThirdPerson();
        }

        // Money reveal: patches is_hltv ONLY while the scoreboard is open
        // (Tab held). The patch makes the game believe it is an HLTV
        // spectator, which also hides the pause menu (ESC). That is why we
        // only keep it active while the user is looking at the scoreboard.
        if (g_Esp.showMoney && (GetAsyncKeyState(VK_TAB) & 0x8000))
            (void)RunMoneyReveal();
        else
            RestoreMoneyReveal();

        // Draw ESP as late as possible so rapid camera movement uses the matrix
        // closest to the frame that will actually be submitted to the compositor.
        const bool presentationActive = IsOverlayPresentationActive();
        BeginOtherGlowFrame(screenWidth, screenHeight);
        if (frameReady && presentationActive) RenderESP(screenWidth, screenHeight);
        if (frameReady && presentationActive) {
            RenderGrenadeTrajectory(screenWidth, screenHeight);
            RenderCrosshair(screenWidth, screenHeight);
        }

        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        static bool otherGlowTargetsActive = false;
        const bool renderOtherGlow = g_Esp.enableOtherGlow && presentationActive;
        if (renderOtherGlow) {
            RenderOtherGlow(g_mainRenderTargetView, g_Esp.otherGlowSoftness, g_Esp.otherGlowLayers);
            otherGlowTargetsActive = true;
        } else if (otherGlowTargetsActive) {
            ReleaseOtherGlowRenderTargets();
            otherGlowTargetsActive = false;
        }
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(0, 0);
        // Present(0) avoids the extra queued frame, while DwmFlush prevents the
        // external overlay from spinning far above the desktop refresh rate.
        DwmFlush();
    }

    SetMenuInputMode(false);
    ShutdownBhop();
    RestoreThirdPerson();
    RestoreMoneyReveal();
    RestoreAntiFlashOverrides();
    RestoreSmokeColors();
    RestoreAntiSmoke();
    ShutdownRadarHack();
    inventoryIpc.Stop();
    ShutdownInventoryChanger();
    ShutdownInventoryPreview();
    ResetSpectatorList();
    ShutdownGlow();
    ShutdownOtherGlow();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupRenderTarget();
    g_pSwapChain->Release();
    g_pd3dDeviceContext->Release();
    g_pd3dDevice->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}
