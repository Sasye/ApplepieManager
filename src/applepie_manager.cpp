#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>

extern "C" __declspec(dllexport) void DummyExport() {}

// ============================================================================
// Logging
// ============================================================================
static HANDLE g_logHandle = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logLock;

void Log(const char *fmt, ...) {
    if (g_logHandle == INVALID_HANDLE_VALUE) return;
    EnterCriticalSection(&g_logLock);
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf) - 2, fmt, args);
    va_end(args);
    if (len < 0) len = 0;
    if (len > (int)sizeof(buf) - 3) len = (int)sizeof(buf) - 3;
    buf[len++] = '\r';
    buf[len++] = '\n';
    DWORD written;
    WriteFile(g_logHandle, buf, (DWORD)len, &written, NULL);
    LeaveCriticalSection(&g_logLock);
}

// ============================================================================
// Modules
// ============================================================================
#include "plugin_registry.h"
#include "il2cpp_text_scanner.h"
#include "imgui_panel.h"

// ============================================================================
// Manager Config (global for imgui_panel.h's Save button)
// ============================================================================
ConfigFile g_managerConfig;
static int g_configLang = 0; // 0=zh, 1=en — applied after imgui_panel.h defines g_lang

// ============================================================================
// Hotkey Polling Thread
// ============================================================================
static volatile bool g_hotkeyRunning = true;

static DWORD WINAPI HotkeyThread(LPVOID) {
    Log("[AM] Hotkey thread started (toggle=HOME)");

    bool homeWasDown = false;

    while (g_hotkeyRunning) {
        // Wait for game window
        if (!g_gameHwnd) {
            g_gameHwnd = FindGameWindow();
            Sleep(200);
            continue;
        }

        // Check if game is still alive
        if (!IsWindow(g_gameHwnd)) {
            Log("[AM] Game window closed, shutting down");
            g_hookDisabled = true;   // Hook becomes passthrough BEFORE game cleanup
            g_hotkeyRunning = false;
            g_overlayRunning = false;
            break;
        }

        // HOME key toggle
        bool homeDown = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
        if (homeDown && !homeWasDown) {
            TogglePanel();
            Log("[AM] Panel toggled: %s", g_panelVisible ? "visible" : "hidden");
        }
        homeWasDown = homeDown;

        Sleep(30);
    }

    return 0;
}

// ============================================================================
// Main Initialization Thread
// ============================================================================
static DWORD WINAPI InitThread(LPVOID) {
    InitializeCriticalSection(&g_logLock);
    g_logHandle = CreateFileA("plugin\\applepie_manager_log.txt",
                              GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    Log("=== Applepie Manager v0.1.0 ===");

    // Load manager config
    g_managerConfig = LoadConfigFile("plugin\\applepie_manager_config.txt");
    if (!g_managerConfig.loaded) {
        Log("[AM] Config not found, generating defaults");
        g_managerConfig.filePath = "plugin\\applepie_manager_config.txt";
        g_managerConfig.loaded = true;

        // [general] section
        ConfigSetValue(g_managerConfig, "language", "zh", "general");

        // [uid] section with defaults
        ConfigSetValue(g_managerConfig, "hide_uid",       "1", "uid");
        ConfigSetValue(g_managerConfig, "hide_ping",      "0", "uid");
        ConfigSetValue(g_managerConfig, "hide_menu_uid",  "1", "uid");
        ConfigSetValue(g_managerConfig, "hide_menu_name", "1", "uid");
        ConfigSetValue(g_managerConfig, "hide_card_uid",  "1", "uid");
        ConfigSetValue(g_managerConfig, "hide_card_name", "1", "uid");

        SaveConfigFile(g_managerConfig);
        Log("[AM] Default config generated");
    } else {
        Log("[AM] Config loaded");

        // Read language setting
        const char* langVal = ConfigGetValue(g_managerConfig, "language", "general");
        if (langVal && strcmp(langVal, "en") == 0) g_configLang = 1;

        // Apply UID hide settings from config
        g_uidHidden      = ConfigGetBool(g_managerConfig, "hide_uid",       true,  "uid");
        g_pingHidden     = ConfigGetBool(g_managerConfig, "hide_ping",      false, "uid");
        g_menuUidHidden  = ConfigGetBool(g_managerConfig, "hide_menu_uid",  true,  "uid");
        g_menuNameHidden = ConfigGetBool(g_managerConfig, "hide_menu_name", true,  "uid");
        g_cardUidHidden  = ConfigGetBool(g_managerConfig, "hide_card_uid",  true,  "uid");
        g_cardNameHidden = ConfigGetBool(g_managerConfig, "hide_card_name", true,  "uid");
    }

    // Scan plugin directory
    ScanPluginDirectory("plugin");
    Log("[AM] Found %d plugins in plugin\\ directory", g_pluginCount);

    // Apply enable/disable from config
    ApplyPluginConfig(g_managerConfig);

    // Load enabled plugins
    LoadEnabledPlugins(Log);

    // Log summary
    int loadedCount = 0, interfaceCount = 0;
    for (int i = 0; i < g_pluginCount; i++) {
        if (g_plugins[i].loaded) loadedCount++;
        if (g_plugins[i].hasInterface) interfaceCount++;
    }
    Log("[AM] Summary: %d/%d loaded, %d with standard interface",
        loadedCount, g_pluginCount, interfaceCount);

    // Apply saved language setting
    g_lang = (g_configLang == 1) ? LANG_EN : LANG_ZH;

    // Sync initial language to all plugins
    BroadcastLanguage(g_lang == LANG_ZH ? "zh" : "en");

    // Start hotkey thread
    CreateThread(NULL, 0, HotkeyThread, NULL, 0, NULL);

    // Start overlay thread (waits for game window internally)
    CreateThread(NULL, 0, OverlayThread, NULL, 0, NULL);

    // Initialize IL2CPP scanner (non-blocking, retries internally)
    InitIL2CppScanner();

    // Install set_text hook immediately (before game creates any UI)
    if (g_il2cppResolved) {
        bool hooked = InstallSetTextHook();
        Log("[AM] set_text hook: %s", hooked ? "OK" : "FAILED");
    }

    // One-shot: find and hide existing UID text (hook handles future updates)
    CreateThread(NULL, 0, [](LPVOID) -> DWORD {
        // Wait for game window to actually exist
        HWND wnd = nullptr;
        for (int w = 0; w < 120 && !g_hookDisabled; w++) {
            wnd = FindWindowW(L"UnityWndClass", nullptr);
            if (wnd) break;
            Sleep(500);
        }
        if (!wnd || g_hookDisabled) return 0;

        // Retry until UID GO is found or game closes
        int attempt = 0;
        while (IsWindow(wnd) && !g_hookDisabled) {
            // Sleep in short intervals so we can exit quickly
            for (int s = 0; s < 20 && !g_hookDisabled; s++) Sleep(100);
            if (!IsWindow(wnd) || g_hookDisabled) break;
            __try {
                if (g_uidHidden) {
                    SetUidVisible(false);
                    if (g_uidTextGO) {
                        Log("[AM] One-shot UID hide: OK (attempt %d)", attempt + 1);
                        return 0;
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            attempt++;
        }
        Log("[AM] One-shot UID hide: game closed");
        return 0;
    }, NULL, 0, NULL);

    Log("[AM] Init complete. Press HOME to toggle manager panel.");
    return 0;
}

// ============================================================================
// DLL Entry Point
// ============================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Instance guard
        HANDLE hMutex = CreateMutexA(NULL, TRUE,
                                     "Local\\ApplepieManager_InstanceGuard");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if (hMutex) CloseHandle(hMutex);
            return TRUE;
        }

        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        g_hookDisabled = true;  // Make hook passthrough immediately
        g_hotkeyRunning = false;
        g_overlayRunning = false;
        MemoryBarrier();        // Ensure all threads see the flags NOW
    }
    return TRUE;
}
