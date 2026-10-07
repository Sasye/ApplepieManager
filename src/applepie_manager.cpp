#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>

// ============================================================================
// Logging
// ============================================================================
#include "bounded_log.h"
static BoundedLog g_managerLog;
void Log(const char* fmt, ...) {
    char buf[4096]; va_list args; va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args); va_end(args);
    g_managerLog.write(buf);
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
    // ── Phase 1: Initialize logging ──
    wchar_t logPath[MAX_PATH];
    swprintf_s(logPath, L"%s\\applepie_manager_log.txt", loader::directory());
    g_managerLog.open(logPath);
    Log("=== Applepie Manager / CoreReady ===");
    Log("[AM] runtime/window services: %s",InitializeManagerUiOwner()?"ready":"unavailable");
    InitializePluginView();
    g_managerConfig = LoadConfigFile(ManagerConfigPath().c_str());

    // ── Phase 4: Apply manager settings from config ──
    if (!g_managerConfig.loaded) {
        Log("[AM] Config not found, generating defaults");
        g_managerConfig.filePath = ManagerConfigPath();
        g_managerConfig.loaded = true;

        ConfigSetValue(g_managerConfig, "language", "zh", "general");
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

        const char* langVal = ConfigGetValue(g_managerConfig, "language", "general");
        if (langVal && strcmp(langVal, "en") == 0) g_configLang = 1;

        g_uidHidden      = ConfigGetBool(g_managerConfig, "hide_uid",       true,  "uid");
        g_pingHidden     = ConfigGetBool(g_managerConfig, "hide_ping",      false, "uid");
        g_menuUidHidden  = ConfigGetBool(g_managerConfig, "hide_menu_uid",  true,  "uid");
        g_menuNameHidden = ConfigGetBool(g_managerConfig, "hide_menu_name", true,  "uid");
        g_cardUidHidden  = ConfigGetBool(g_managerConfig, "hide_card_uid",  true,  "uid");
        g_cardNameHidden = ConfigGetBool(g_managerConfig, "hide_card_name", true,  "uid");
    }

    // Apply saved language setting
    g_lang = (g_configLang == 1) ? LANG_EN : LANG_ZH;

    // Start hotkey thread
    CreateThread(NULL, 0, HotkeyThread, NULL, 0, NULL);

    // Start overlay thread (waits for game window internally)
    CreateThread(NULL, 0, OverlayThread, NULL, 0, NULL);

    // Metadata startup is independent of the panel and its hotkeys.
    HANDLE uidWorker=CreateThread(nullptr,0,[](LPVOID)->DWORD {
        bool ok=false;
        __try {ok=InitIL2CppScanner()&&InstallSetTextHook();}
        __except(APH_EntryFilter(&g_uidHooks,g_uidOwner,0,GetExceptionInformation(),APH_ORIGINAL_NOT_ENTERED,APH_RECOVERY_NONE)){}
        if(!ok){g_hookDisabled=true;if(g_uidOwner)hub::fail_owner(g_uidOwner,APH_E_UNAVAILABLE);}
        Log("[AM] UID runtime: %s",ok?"ready":"unavailable (see Core error details)");return 0;
    },nullptr,0,nullptr);
    if(uidWorker)CloseHandle(uidWorker);

    Log("[AM] Init complete. Press HOME to toggle manager panel.");
    return 0;
}

// ============================================================================
// DLL Entry Point
// ============================================================================
namespace loader {
void start_features() {
    HANDLE worker = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    if (worker) CloseHandle(worker);
}
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    // No startup, wait or cleanup under Loader Lock. Modules are process resident.
    return TRUE;
}
