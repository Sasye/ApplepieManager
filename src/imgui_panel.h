#pragma once
// ============================================================================
// imgui_panel.h — ImGui overlay panel for Applepie Manager
// ============================================================================
// Follows EIEM's proven GUI architecture:
// - Independent DX11 device + WS_EX_TOOLWINDOW + WS_POPUP
// - ImGui fills the OS window (NoTitleBar/NoMove/NoResize)
// - Custom title bar with drag via SetWindowPos
// - Focus-aware z-order (TOPMOST only when game is foreground)
// ============================================================================

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <dcomp.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dcomp.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// Overlay State
// ============================================================================

static bool g_panelVisible = false;
static HWND g_overlayHwnd = nullptr;
static HWND g_gameHwnd = nullptr;

// DX11 state
static ID3D11Device*           g_pd3dDevice = nullptr;
static ID3D11DeviceContext*    g_pd3dContext = nullptr;
static IDXGISwapChain1*        g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_pRTV = nullptr;
static IDCompositionDevice*    g_pDCompDevice = nullptr;
static IDCompositionTarget*    g_pDCompTarget = nullptr;
static IDCompositionVisual*    g_pDCompVisual = nullptr;

// Forward declarations
static void RenderPluginPanel();
static bool CreateOverlayDeviceD3D(HWND hWnd, int w, int h);
static void CleanupOverlayDeviceD3D();

// ============================================================================
// i18n — Internationalization
// ============================================================================

enum Lang { LANG_ZH = 0, LANG_EN = 1 };
static Lang g_lang = LANG_ZH;

enum StrKey {
    S_TITLE,            // always English
    S_STATUS_DISABLED,
    S_STATUS_ACTIVE,
    S_STATUS_PAUSED,
    S_LOAD,
    S_RESTART_NEEDED,
    S_PAUSE,
    S_RESUME,
    S_SETTINGS,
    S_NO_INTERFACE,
    S_HOTKEYS,
    S_HOTKEY_OVERVIEW,
    S_PLUGIN_MANAGER,
    S_NO_CONFLICT,
    S_CONFLICT_FMT,
    S_SAVE_CONFIG,
    S_LOAD_RESTART,
    S_REBIND,
    S_REBIND_PROMPT,
    S_REBIND_CANCEL,
    S_IL2CPP_NOT_READY,
    S_UID_CONTROL,
    S_UID_LOCATE,
    S_UID_HIDE,
    S_PING_HIDE,
    S_MENU_UID_HIDE,
    S_MENU_NAME_HIDE,
    S_UID_INGAME,
    S_UID_MENU,
    S_UID_CARD,
    S_CARD_UID_HIDE,
    S_CARD_NAME_HIDE,
    S_COUNT
};

static const char* g_strings[2][S_COUNT] = {
    // LANG_ZH
    {
        "Applepie Manager",
        u8"已禁用",
        u8"运行中",
        u8"已暂停",
        u8"加载",
        u8"(重启生效)",
        u8"暂停",
        u8"恢复",
        u8"设置",
        u8"  (无接口)",
        u8"热键",
        u8"热键总览",
        u8"插件管理器",
        u8"  无冲突",
        u8"  冲突: %s 被 %s 和 %s 同时使用",
        u8"保存配置",
        u8"重新加载/卸载插件需重启游戏",
        u8"变更",
        u8"请按下新热键...",
        u8"(ESC取消)",
        u8"IL2CPP 未就绪",
        u8"UID 控制",
        u8"定位 UID",
        u8"隐藏 UID",
        u8"隐藏延迟",
        u8"隐藏 UID",
        u8"隐藏名字",
        u8"水印",
        u8"菜单",
        u8"名片",
        u8"隐藏 UID",
        u8"隐藏名字",
    },
    // LANG_EN
    {
        "Applepie Manager",
        "Disabled",
        "Active",
        "Paused",
        "Load",
        "(restart)",
        "Pause",
        "Resume",
        "Settings",
        "  (no interface)",
        "Hotkeys",
        "Hotkey Overview",
        "Applepie Manager",
        "  No conflicts",
        "  CONFLICT: %s used by %s and %s",
        "Save Config",
        "Reload/unload plugins require restart",
        "Rebind",
        "Press new key...",
        "(ESC cancel)",
        "IL2CPP not ready",
        "UID Control",
        "Locate UID",
        "Hide UID",
        "Hide Ping",
        "Hide UID",
        "Hide Name",
        "Watermark",
        "Menu",
        "Card",
        "Hide UID",
        "Hide Name",
    },
};

static const char* T(StrKey key) { return g_strings[g_lang][key]; }

// ============================================================================
// Find game window — only from the current process
// ============================================================================

struct EnumWindowCtx { DWORD pid; HWND result; };

static BOOL CALLBACK EnumWindowProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumWindowCtx*>(lParam);
    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid != ctx->pid) return TRUE; // wrong process, continue

    char cls[64] = {};
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strcmp(cls, "UnityWndClass") == 0 && IsWindowVisible(hwnd)) {
        ctx->result = hwnd;
        return FALSE; // found, stop
    }
    return TRUE;
}

static HWND FindGameWindow() {
    EnumWindowCtx ctx = {};
    ctx.pid = GetCurrentProcessId();
    ctx.result = nullptr;
    EnumWindows(EnumWindowProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

// ============================================================================
// Render Target helpers
// ============================================================================

static void CreateRenderTarget() {
    ID3D11Texture2D* pBack = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBack));
    if (pBack) {
        g_pd3dDevice->CreateRenderTargetView(pBack, nullptr, &g_pRTV);
        pBack->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_pRTV) { g_pRTV->Release(); g_pRTV = nullptr; }
}

// ============================================================================
// Overlay WndProc
// ============================================================================

static LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 0;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
                                        DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================================
// DX11 Device Creation (same as EIEM)
// ============================================================================

static bool CreateOverlayDeviceD3D(HWND hWnd, int w, int h) {
    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[] = {D3D_FEATURE_LEVEL_11_0};

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 1, D3D11_SDK_VERSION,
        &g_pd3dDevice, &featureLevel, &g_pd3dContext);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 1, D3D11_SDK_VERSION,
            &g_pd3dDevice, &featureLevel, &g_pd3dContext);
    }
    if (FAILED(hr)) {
        Log("[AM] D3D11CreateDevice failed: 0x%08X", hr);
        return false;
    }

    IDXGIDevice* pDxgiDevice = nullptr;
    g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pDxgiDevice));
    IDXGIAdapter* adapter = nullptr;
    pDxgiDevice->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = w;
    sd.Height = h;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = factory->CreateSwapChainForComposition(g_pd3dDevice, &sd, nullptr, &g_pSwapChain);
    factory->Release();
    adapter->Release();

    if (FAILED(hr)) {
        Log("[AM] CreateSwapChainForComposition failed: 0x%08X", hr);
        pDxgiDevice->Release();
        return false;
    }

    hr = DCompositionCreateDevice(pDxgiDevice, IID_PPV_ARGS(&g_pDCompDevice));
    pDxgiDevice->Release();
    if (FAILED(hr)) {
        Log("[AM] DCompositionCreateDevice failed: 0x%08X", hr);
        return false;
    }
    g_pDCompDevice->CreateTargetForHwnd(hWnd, TRUE, &g_pDCompTarget);
    g_pDCompDevice->CreateVisual(&g_pDCompVisual);

    // 3DMigoto/SSMT bypass (same as EIEM)
    if (g_pSwapChain) {
        __try {
            void* vtable = *(void**)g_pSwapChain;
            HMODULE hModule = NULL;
            if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    (LPCSTR)vtable, &hModule)) {
                char moduleName[MAX_PATH] = {};
                GetModuleFileNameA(hModule, moduleName, MAX_PATH);
                for (int i = 0; moduleName[i]; i++)
                    moduleName[i] = (char)tolower((unsigned char)moduleName[i]);
                if (strstr(moduleName, "d3d11.dll")) {
                    Log("[AM] DETECTED 3DMigoto/SSMT wrapper on SwapChain!");
                    IDXGISwapChain1* candidate =
                        *(IDXGISwapChain1**)((char*)g_pSwapChain + 8);
                    IDXGISwapChain1* real = nullptr;
                    if (candidate &&
                        SUCCEEDED(candidate->QueryInterface(IID_PPV_ARGS(&real)))) {
                        Log("[AM] Unwrapped real SwapChain");
                        g_pSwapChain = real;
                    }
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    g_pDCompVisual->SetContent(g_pSwapChain);
    g_pDCompTarget->SetRoot(g_pDCompVisual);
    g_pDCompDevice->Commit();

    CreateRenderTarget();
    Log("[AM] DX11 pipeline initialized");
    return true;
}

static void CleanupOverlayDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pDCompVisual) { g_pDCompVisual->Release(); g_pDCompVisual = nullptr; }
    if (g_pDCompTarget) { g_pDCompTarget->Release(); g_pDCompTarget = nullptr; }
    if (g_pDCompDevice) { g_pDCompDevice->Release(); g_pDCompDevice = nullptr; }
    if (g_pd3dContext) { g_pd3dContext->Release(); g_pd3dContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

// ============================================================================
// Toggle Panel (same as EIEM ToggleGui)
// ============================================================================

// ============================================================================
// Auto-save config helper
// ============================================================================
static void SaveAllConfig() {
    extern ConfigFile g_managerConfig;
    ConfigSetValue(g_managerConfig, "language",
                   g_lang == LANG_ZH ? "zh" : "en", "general");
    ConfigSetValue(g_managerConfig, "hide_uid",       g_uidHidden       ? "1" : "0", "uid");
    ConfigSetValue(g_managerConfig, "hide_ping",      g_pingHidden      ? "1" : "0", "uid");
    ConfigSetValue(g_managerConfig, "hide_menu_uid",  g_menuUidHidden   ? "1" : "0", "uid");
    ConfigSetValue(g_managerConfig, "hide_menu_name", g_menuNameHidden  ? "1" : "0", "uid");
    ConfigSetValue(g_managerConfig, "hide_card_uid",  g_cardUidHidden   ? "1" : "0", "uid");
    ConfigSetValue(g_managerConfig, "hide_card_name", g_cardNameHidden  ? "1" : "0", "uid");
    SavePluginStates(g_managerConfig);
}

static void TogglePanel() {
    if (!g_overlayHwnd) return;
    g_panelVisible = !g_panelVisible;
    if (g_panelVisible) {
        // Reposition to game window on first show
        static bool s_firstShow = true;
        if (s_firstShow && g_gameHwnd) {
            RECT gr;
            GetWindowRect(g_gameHwnd, &gr);
            RECT wr;
            GetWindowRect(g_overlayHwnd, &wr);
            int pw = wr.right - wr.left;
            int ph = wr.bottom - wr.top;
            // Position at game window's top-right with safe clamping
            int posX = gr.right - pw - 20;
            int posY = gr.top + 40;
            // Ensure within screen bounds
            if (posX < 0) posX = 0;
            if (posY < 0) posY = 0;
            // Ensure doesn't go below screen
            int screenH = GetSystemMetrics(SM_CYSCREEN);
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            if (posX + pw > screenW) posX = screenW - pw;
            if (posY + ph > screenH) posY = screenH - ph;
            SetWindowPos(g_overlayHwnd, HWND_TOPMOST, posX, posY, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
            s_firstShow = false;
        } else {
            SetWindowPos(g_overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        ShowWindow(g_overlayHwnd, SW_SHOW);
        SetForegroundWindow(g_overlayHwnd);
    } else {
        SetWindowPos(g_overlayHwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(g_overlayHwnd, SW_HIDE);
        if (g_gameHwnd) SetForegroundWindow(g_gameHwnd);
    }
}

// ============================================================================
// ImGui Panel Rendering
// ============================================================================

static void RenderPluginPanel() {
    // Fill the entire OS window (same as EIEM)
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("PluginMgr", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    // ==================== Custom title bar ====================
    const float titleH = 36.0f;
    const float btnSize = 22.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();

    // Title bar background
    dl->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + titleH),
                      IM_COL32(24, 24, 24, 180));
    // Left accent strip (teal)
    dl->AddRectFilled(winPos, ImVec2(winPos.x + 4, winPos.y + titleH),
                      IM_COL32(60, 180, 220, 255));
    // Bottom separator
    dl->AddLine(ImVec2(winPos.x, winPos.y + titleH),
                ImVec2(winPos.x + winSize.x, winPos.y + titleH),
                IM_COL32(100, 100, 105, 80), 1.0f);

    // Title text (always English)
    dl->AddText(ImVec2(winPos.x + 14, winPos.y + 9), IM_COL32(60, 180, 220, 255),
                "Applepie Manager");
    dl->AddText(ImVec2(winPos.x + 195, winPos.y + 9),
                IM_COL32(100, 100, 105, 200), "v0.1.0");

    // ---- Drag zone (title bar, excluding lang+close buttons) ----
    ImGui::SetCursorPos(ImVec2(0, 0));
    float dragW = winSize.x - (btnSize + 8) * 2 - 8;
    if (dragW < 1.0f) dragW = 1.0f;
    ImGui::InvisibleButton("##titlebar_drag", ImVec2(dragW, titleH));
    // Move the borderless OS window via screen-space cursor tracking
    static bool s_dragging = false;
    static POINT s_dragAnchor = {0, 0};
    static RECT s_winAtDrag = {0, 0, 0, 0};
    if (ImGui::IsItemActivated()) {
        s_dragging = true;
        GetCursorPos(&s_dragAnchor);
        GetWindowRect(g_overlayHwnd, &s_winAtDrag);
    }
    if (s_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        POINT cur;
        GetCursorPos(&cur);
        int nx = s_winAtDrag.left + (cur.x - s_dragAnchor.x);
        int ny = s_winAtDrag.top  + (cur.y - s_dragAnchor.y);
        SetWindowPos(g_overlayHwnd, nullptr, nx, ny, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        s_dragging = false;
    }

    // ---- Language toggle button (left of close) ----
    float langBtnX = winSize.x - (btnSize + 8) * 2;
    ImVec2 langPos = ImVec2(langBtnX, (titleH - btnSize) * 0.5f);
    ImGui::SetCursorPos(langPos);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btnSize * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(55, 58, 70, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 85, 100, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(100, 105, 120, 255));
    if (ImGui::Button("##lang", ImVec2(btnSize, btnSize))) {
        g_lang = (g_lang == LANG_ZH) ? LANG_EN : LANG_ZH;
        BroadcastLanguage(g_lang == LANG_ZH ? "zh" : "en");
        SaveAllConfig();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    // Draw language label on the button (show OPPOSITE language)
    {
        const char* langLabel = (g_lang == LANG_ZH) ? "EN" : u8"中";
        ImVec2 labelSize = ImGui::CalcTextSize(langLabel);
        dl->AddText(ImVec2(winPos.x + langPos.x + (btnSize - labelSize.x) * 0.5f,
                           winPos.y + langPos.y + (btnSize - labelSize.y) * 0.5f),
                    IM_COL32(180, 200, 220, 255), langLabel);
    }

    // ---- Close button (top-right) ----
    ImVec2 closePos = ImVec2(winSize.x - btnSize - 8, (titleH - btnSize) * 0.5f);
    ImGui::SetCursorPos(closePos);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btnSize * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(55, 58, 70, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(200, 60, 60, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(220, 40, 40, 255));
    if (ImGui::Button("##close", ImVec2(btnSize, btnSize))) {
        g_panelVisible = false;
        SetWindowPos(g_overlayHwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(g_overlayHwnd, SW_HIDE);
        if (g_gameHwnd) SetForegroundWindow(g_gameHwnd);
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    // Draw X on the button
    {
        ImVec2 ctr = ImVec2(winPos.x + closePos.x + btnSize * 0.5f,
                            winPos.y + closePos.y + btnSize * 0.5f);
        float r = 5.0f;
        dl->AddLine(ImVec2(ctr.x - r, ctr.y - r), ImVec2(ctr.x + r, ctr.y + r),
                    IM_COL32(200, 200, 200, 255), 1.5f);
        dl->AddLine(ImVec2(ctr.x + r, ctr.y - r), ImVec2(ctr.x - r, ctr.y + r),
                    IM_COL32(200, 200, 200, 255), 1.5f);
    }

    // ==================== Content area ====================
    float contentY = titleH + 8.0f;
    float contentH = winSize.y - titleH - 16.0f;
    if (contentH < 1.0f) contentH = 1.0f;
    ImGui::SetCursorPos(ImVec2(12, contentY));
    ImGui::BeginChild("##content", ImVec2(winSize.x - 24, contentH), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // Plugin list
    for (int i = 0; i < g_pluginCount; i++) {
        ManagedPlugin& p = g_plugins[i];
        ImGui::PushID(i);

        // Status indicator
        ImVec4 statusColor;
        const char* statusText;
        if (!p.loaded) {
            statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
            statusText = T(S_STATUS_DISABLED);
        } else if (p.active) {
            statusColor = ImVec4(0.2f, 0.9f, 0.3f, 1.0f);
            statusText = T(S_STATUS_ACTIVE);
        } else {
            statusColor = ImVec4(0.95f, 0.75f, 0.1f, 1.0f);
            statusText = T(S_STATUS_PAUSED);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
        ImGui::BulletText("%s", statusText);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        const char* name = p.hasInterface && p.displayName ? p.displayName : p.dllName;
        if (p.version && p.version[0]) {
            ImGui::Text("%s v%s", name, p.version);
        } else {
            ImGui::Text("%s", name);
        }

        if (p.hasInterface && p.description && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", p.description);
        }

        // Boot enable/disable
        {
            bool wasEnabled = p.enabled;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110);
            ImGui::Checkbox(T(S_LOAD), &p.enabled);
            if (p.enabled != wasEnabled) {
                SaveAllConfig();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), T(S_RESTART_NEEDED));
            }
        }

        // Runtime pause/resume
        if (p.loaded && p.supportsHotDisable) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30);
            if (p.active) {
                if (ImGui::SmallButton(T(S_PAUSE))) TogglePluginActive(i);
            } else {
                if (ImGui::SmallButton(T(S_RESUME))) TogglePluginActive(i);
            }
        }

        // Config editor
        if (p.hasInterface && p.configFile) {
            char configPath[MAX_PATH];
            snprintf(configPath, MAX_PATH, "plugin\\%s", p.configFile);
            if (ImGui::TreeNode(T(S_SETTINGS))) {
                auto entries = ReadPluginConfig(configPath);
                bool configChanged = false;
                for (auto& e : entries) {
                    if (e.key == "enabled") continue;
                    // Skip hotkey config keys — already shown in Hotkeys section
                    bool isHotkeyKey = false;
                    for (int hk = 0; hk < p.hotkeyCount; hk++) {
                        if (p.hotkeys[hk].configKey && e.key == p.hotkeys[hk].configKey) {
                            isHotkeyKey = true;
                            break;
                        }
                    }
                    if (isHotkeyKey) continue;
                    char valBuf[256];
                    strncpy(valBuf, e.value.c_str(), sizeof(valBuf) - 1);
                    valBuf[sizeof(valBuf) - 1] = '\0';
                    ImGui::SetNextItemWidth(120);
                    if (ImGui::InputText(e.key.c_str(), valBuf, sizeof(valBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue)) {
                        UpdatePluginConfigValue(configPath, e.key.c_str(), valBuf);
                        configChanged = true;
                    }
                }
                if (configChanged && p.pfnReloadConfig) ReloadPluginConfig(i);
                ImGui::TreePop();
            }
        } else if (!p.hasInterface) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               T(S_NO_INTERFACE));
        }

        // Hotkeys with rebind
        if (p.hotkeyCount > 0 && ImGui::TreeNode(T(S_HOTKEYS))) {
            for (int h = 0; h < p.hotkeyCount; h++) {
                ImGui::PushID(h);
                ImGui::Text("  %s", p.hotkeys[h].name);
                ImGui::SameLine(180);
                ImGui::TextColored(ImVec4(0.24f, 0.71f, 0.86f, 1.0f),
                                   "[%s]", VKToName(p.hotkeys[h].currentVK));

                // Rebind button (only if configKey is set)
                if (p.hotkeys[h].configKey) {
                    ImGui::SameLine();

                    // State for this specific hotkey rebind
                    static int s_rebindPlugin = -1;
                    static int s_rebindHotkey = -1;
                    bool isRebinding = (s_rebindPlugin == i && s_rebindHotkey == h);

                    if (isRebinding) {
                        // Show prompt
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f),
                                           "%s %s", T(S_REBIND_PROMPT), T(S_REBIND_CANCEL));

                        // Scan for key press
                        bool captured = false;
                        for (int vk = 1; vk < 256; vk++) {
                            // Skip mouse buttons and modifier-only keys
                            if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
                            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;
                            if (vk == VK_LSHIFT || vk == VK_RSHIFT) continue;
                            if (vk == VK_LCONTROL || vk == VK_RCONTROL) continue;
                            if (vk == VK_LMENU || vk == VK_RMENU) continue;

                            if (GetAsyncKeyState(vk) & 1) {
                                if (vk == VK_ESCAPE) {
                                    // Cancel
                                    s_rebindPlugin = -1;
                                    s_rebindHotkey = -1;
                                    captured = true;
                                    break;
                                }
                                // Apply new key — write as decimal VK code
                                // Plugins parse via ParseVK() which supports
                                // decimal (112), hex (0x70), and single char (Q)
                                char vkStr[16];
                                snprintf(vkStr, sizeof(vkStr), "%d", vk);
                                char configPath[MAX_PATH];
                                snprintf(configPath, MAX_PATH, "plugin\\%s", p.configFile);
                                UpdatePluginConfigValue(configPath, p.hotkeys[h].configKey, vkStr);
                                if (p.pfnReloadConfig) ReloadPluginConfig(i);
                                Log("[AM] Rebound %s.%s -> %s (0x%02X)",
                                    p.dllName, p.hotkeys[h].configKey, VKToName(vk), vk);
                                s_rebindPlugin = -1;
                                s_rebindHotkey = -1;
                                captured = true;
                                break;
                            }
                        }
                    } else {
                        if (ImGui::SmallButton(T(S_REBIND))) {
                            s_rebindPlugin = i;
                            s_rebindHotkey = h;
                        }
                    }
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::Separator();

    // Hotkey overview
    if (ImGui::CollapsingHeader(T(S_HOTKEY_OVERVIEW), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("  %-12s  %s", "HOME", T(S_PLUGIN_MANAGER));
        for (int i = 0; i < g_pluginCount; i++) {
            if (!g_plugins[i].loaded || !g_plugins[i].active) continue;
            const char* name = g_plugins[i].hasInterface && g_plugins[i].displayName
                ? g_plugins[i].displayName : g_plugins[i].dllName;
            for (int h = 0; h < g_plugins[i].hotkeyCount; h++) {
                ImGui::Text("  %-12s  %s - %s",
                            VKToName(g_plugins[i].hotkeys[h].currentVK),
                            name, g_plugins[i].hotkeys[h].name);
            }
        }
        auto conflicts = DetectHotkeyConflicts();
        if (conflicts.empty()) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), T(S_NO_CONFLICT));
        } else {
            for (const auto& c : conflicts) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                    T(S_CONFLICT_FMT),
                    VKToName(c.vk),
                    g_plugins[c.pluginIdx1].displayName ? g_plugins[c.pluginIdx1].displayName : g_plugins[c.pluginIdx1].dllName,
                    g_plugins[c.pluginIdx2].displayName ? g_plugins[c.pluginIdx2].displayName : g_plugins[c.pluginIdx2].dllName);
            }
        }
    }

    ImGui::Separator();

    // ==================== UID Control ====================
    if (ImGui::CollapsingHeader(T(S_UID_CONTROL), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!g_il2cppResolved) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                               T(S_IL2CPP_NOT_READY));
        } else {
            // Auto-locate UID on first render
            if (!g_uidPanelGO) {
                static bool s_locateStarted = false;
                if (!s_locateStarted) {
                    s_locateStarted = true;
                    CreateThread(NULL, 0, [](LPVOID) -> DWORD {
                        for (int attempt = 0; attempt < 10; attempt++) {
                            if (FindUidComponent()) break;
                            if (g_gameHwnd && !IsWindow(g_gameHwnd)) break;
                            Sleep(500);
                        }
                        return 0;
                    }, NULL, 0, NULL);
                }
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f),
                                   u8"\xe2\x8f\xb3 %s...", T(S_UID_LOCATE));
            } else {
                // In-game section
                ImGui::PushID("ingame");
                ImGui::Text("%s:", T(S_UID_INGAME));
                ImGui::SameLine();
                bool hideUid = g_uidHidden;
                if (ImGui::Checkbox(T(S_UID_HIDE), &hideUid)) {
                    SetUidVisible(!hideUid);
                    SaveAllConfig();
                }
                ImGui::SameLine();
                bool hidePing = g_pingHidden;
                if (ImGui::Checkbox(T(S_PING_HIDE), &hidePing)) {
                    SetPingVisible(!hidePing);
                    SaveAllConfig();
                }
                ImGui::PopID();

                // Menu section
                ImGui::PushID("menu");
                ImGui::Text("%s:", T(S_UID_MENU));
                ImGui::SameLine();
                bool hideMenuUid = g_menuUidHidden;
                if (ImGui::Checkbox(T(S_MENU_UID_HIDE), &hideMenuUid)) {
                    SetMenuUidVisible(!hideMenuUid);
                    SaveAllConfig();
                }
                ImGui::SameLine();
                bool hideMenuName = g_menuNameHidden;
                if (ImGui::Checkbox(T(S_MENU_NAME_HIDE), &hideMenuName)) {
                    SetMenuNameVisible(!hideMenuName);
                    SaveAllConfig();
                }
                ImGui::PopID();

                // Business card section
                ImGui::PushID("card");
                ImGui::Text("%s:", T(S_UID_CARD));
                ImGui::SameLine();
                bool hideCardUid = g_cardUidHidden;
                if (ImGui::Checkbox(T(S_CARD_UID_HIDE), &hideCardUid)) {
                    SetCardUidVisible(!hideCardUid);
                    SaveAllConfig();
                }
                ImGui::SameLine();
                bool hideCardName = g_cardNameHidden;
                if (ImGui::Checkbox(T(S_CARD_NAME_HIDE), &hideCardName)) {
                    SetCardNameVisible(!hideCardName);
                    SaveAllConfig();
                }
                ImGui::PopID();
            }
        }
    }

    ImGui::Separator();

    // Hint
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                       T(S_LOAD_RESTART));

    ImGui::EndChild();
    ImGui::End();
}

// ============================================================================
// Overlay Thread
// ============================================================================

static volatile bool g_overlayRunning = true;
static WNDPROC g_origGameWndProc = nullptr;
extern volatile bool g_hotkeyRunning;

static DWORD WINAPI OverlayThread(LPVOID) {
    Log("[AM] Overlay thread started");

    // Wait for game window (current process only)
    while (!g_gameHwnd) {
        g_gameHwnd = FindGameWindow();
        if (!g_gameHwnd) Sleep(200);
    }
    Log("[AM] Game window found: 0x%p (pid=%lu)", g_gameHwnd, GetCurrentProcessId());

    // Subclass game window to detect Alt+F4 (WM_CLOSE) before Unity cleanup
    // This sets g_hookDisabled BEFORE Unity calls set_text during teardown
    g_origGameWndProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC,
        (LONG_PTR)+[](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (msg == WM_CLOSE || msg == WM_DESTROY) {
                g_hookDisabled = true;   // Kill hook BEFORE Unity cleanup starts
                g_overlayRunning = false;
                g_hotkeyRunning = false;
            }
            return CallWindowProcW(g_origGameWndProc, hwnd, msg, wParam, lParam);
        });
    if (g_origGameWndProc)
        Log("[AM] Game window subclassed for shutdown detection");
    else
        Log("[AM] WARNING: Failed to subclass game window");

    // Position: top-right of game window
    RECT gr;
    GetWindowRect(g_gameHwnd, &gr);
    int panelW = 480;
    int panelH = 560;
    int posX = gr.right - panelW - 20;
    int posY = gr.top + 40;

    // Clamp to screen bounds
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    if (posX < 0) posX = 0;
    if (posY < 0) posY = 0;
    if (posX + panelW > screenW) posX = screenW - panelW;
    if (posY + panelH > screenH) posY = screenH - panelH;

    Log("[AM] Panel position: %d,%d (%dx%d) game=[%ld,%ld,%ld,%ld]",
        posX, posY, panelW, panelH, gr.left, gr.top, gr.right, gr.bottom);

    // Register window class (CS_CLASSDC, same as EIEM)
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"EndfieldPluginMgrOverlay";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    // NOT topmost at creation — we set it on show (same as EIEM)
    g_overlayHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, wc.lpszClassName, L"Applepie Manager",
        WS_POPUP, posX, posY, panelW, panelH,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!g_overlayHwnd) {
        Log("[AM] ERROR: CreateWindowExW failed! err=%lu", GetLastError());
        return 1;
    }
    Log("[AM] Window created OK");

    // Windows 11 rounded corners
    {
        const DWORD attr = 33;
        const DWORD pref = 2;
        DwmSetWindowAttribute(g_overlayHwnd, attr, &pref, sizeof(pref));
    }

    // Acrylic blur-behind (same as EIEM)
    {
        struct ACCENT_POLICY { DWORD AccentState; DWORD AccentFlags; DWORD GradientColor; DWORD AnimationId; };
        struct WINCOMPATTRDATA { DWORD Attrib; PVOID pvData; SIZE_T cbData; };
        typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);

        HMODULE hUser = GetModuleHandleW(L"user32.dll");
        auto SetWCA = (pSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
        if (SetWCA) {
            ACCENT_POLICY accent = {};
            accent.AccentState = 4; // ACCENT_ENABLE_ACRYLICBLURBEHIND
            accent.GradientColor = 0x00FFFFFF;
            WINCOMPATTRDATA data = {};
            data.Attrib = 19;
            data.pvData = &accent;
            data.cbData = sizeof(accent);
            SetWCA(g_overlayHwnd, &data);
            Log("[AM] Acrylic blur-behind enabled");
        }
    }

    if (!CreateOverlayDeviceD3D(g_overlayHwnd, panelW, panelH)) {
        Log("[AM] ERROR: DX11 device creation failed!");
        DestroyWindow(g_overlayHwnd);
        return 1;
    }

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.MouseDrawCursor = false;

    // Style
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12, 12);
    style.ItemSpacing = ImVec2(8, 6);

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg]        = ImVec4(0.10f, 0.10f, 0.12f, 0.70f);
    c[ImGuiCol_ChildBg]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_Text]            = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    c[ImGuiCol_Header]          = ImVec4(0.20f, 0.20f, 0.22f, 0.80f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.30f, 0.30f, 0.32f, 0.80f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    c[ImGuiCol_Button]          = ImVec4(0.22f, 0.22f, 0.25f, 0.90f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.30f, 0.30f, 0.35f, 1.00f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.15f, 0.15f, 0.18f, 0.90f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.20f, 0.20f, 0.23f, 0.90f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.22f, 0.22f, 0.26f, 0.90f);
    c[ImGuiCol_CheckMark]       = ImVec4(0.24f, 0.71f, 0.86f, 1.00f);
    c[ImGuiCol_SliderGrab]      = ImVec4(0.24f, 0.71f, 0.86f, 0.80f);
    c[ImGuiCol_Separator]       = ImVec4(0.30f, 0.30f, 0.35f, 0.60f);
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.10f, 0.10f, 0.12f, 0.50f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.30f, 0.30f, 0.35f, 0.60f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.45f, 0.80f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);

    // Load CJK font
    {
        ImFontConfig fontCfg;
        fontCfg.OversampleH = 2;
        fontCfg.OversampleV = 1;
        fontCfg.PixelSnapH = true;
        const char* fontPath = "C:\\Windows\\Fonts\\msyh.ttc";
        bool loaded = false;
        if (GetFileAttributesA(fontPath) != INVALID_FILE_ATTRIBUTES) {
            ImFont* f = io.Fonts->AddFontFromFileTTF(
                fontPath, 18.0f, &fontCfg,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            loaded = (f != nullptr);
        }
        if (!loaded) {
            io.Fonts->AddFontDefault();
            Log("[AM] WARN: msyh.ttc not found");
        }
    }

    ImGui_ImplWin32_Init(g_overlayHwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

    // Start hidden
    g_panelVisible = false;
    ShowWindow(g_overlayHwnd, SW_HIDE);

    Log("[AM] ImGui initialized, overlay ready");

    // Render loop
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (g_overlayRunning) {
        // Always check game window first — even when hidden
        if (!IsWindow(g_gameHwnd)) {
            Log("[AM] Game window gone, stopping");
            g_hookDisabled = true;  // Kill hook IMMEDIATELY — don't wait for DLL_PROCESS_DETACH
            g_overlayRunning = false;
            g_hotkeyRunning = false;
            break;
        }

        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                g_hookDisabled = true;
                g_overlayRunning = false;
                break;
            }
        }
        if (!g_overlayRunning) break;

        // Only render when visible
        if (!g_panelVisible) {
            // Short sleep so we can detect game exit quickly
            Sleep(50);
            continue;
        }

        // Focus-aware z-order (same as EIEM):
        // TOPMOST only when game or panel is foreground.
        // Hide + NOTOPMOST when user alt-tabs to another app.
        static bool s_panelShown = false;
        HWND fg = GetForegroundWindow();
        bool gameInFocus = false;
        if (fg) {
            if (fg == g_gameHwnd || fg == g_overlayHwnd) {
                gameInFocus = true;
            } else {
                DWORD fgPid = 0, gamePid = 0;
                GetWindowThreadProcessId(fg, &fgPid);
                if (g_gameHwnd) GetWindowThreadProcessId(g_gameHwnd, &gamePid);
                if (fgPid != 0 && fgPid == gamePid) gameInFocus = true;
            }
        }

        if (gameInFocus && !s_panelShown) {
            SetWindowPos(g_overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ShowWindow(g_overlayHwnd, SW_SHOWNOACTIVATE);
            s_panelShown = true;
        } else if (!gameInFocus && s_panelShown) {
            SetWindowPos(g_overlayHwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ShowWindow(g_overlayHwnd, SW_HIDE);
            s_panelShown = false;
        }

        if (!s_panelShown) {
            Sleep(50);
            continue;
        }

        // Render
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderPluginPanel();

        ImGui::Render();
        const float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        g_pd3dContext->OMSetRenderTargets(1, &g_pRTV, nullptr);
        g_pd3dContext->ClearRenderTargetView(g_pRTV, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present without vsync — fail fast if device lost during shutdown
        HRESULT hr = g_pSwapChain->Present(0, 0);
        if (FAILED(hr)) {
            Log("[AM] Present failed (0x%08X), stopping", hr);
            g_overlayRunning = false;
            break;
        }
    }

    // Skip ALL cleanup during shutdown.
    // COM Release() on DXGI/DComp objects can trigger DCOM/RPC cleanup
    // with a ~10 second timeout when the RPC server is already torn down.
    // The OS reclaims all resources on process exit anyway.
    Log("[AM] Overlay thread exited (cleanup skipped — process shutting down)");
    return 0;
}
