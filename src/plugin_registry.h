#pragma once
// ============================================================================
// plugin_registry.h — Plugin discovery, loading, and interface binding
// ============================================================================

#include "applepie_mgr.h"
#include "config_parser.h"
#include <algorithm>

// ============================================================================
// Managed Plugin State
// ============================================================================

struct ManagedPlugin {
    // File info
    char dllName[64];
    char dllPath[MAX_PATH];
    char versionBuf[32];    // Buffer for version string read from DLL resource

    // Runtime state
    HMODULE hModule;
    bool enabled;           // Config-based: should this plugin be loaded?
    bool loaded;            // Was LoadLibrary successful?
    bool active;            // Is the plugin currently active (not paused)?
    bool hasInterface;      // Does it export AP_GetPluginInfo?

    // Standard interface function pointers (nullptr if not implemented)
    pfn_AP_GetPluginInfo      pfnGetInfo;
    pfn_AP_PluginEnable       pfnEnable;
    pfn_AP_PluginDisable      pfnDisable;
    pfn_AP_ReloadConfig       pfnReloadConfig;
    pfn_AP_GetHotkeys         pfnGetHotkeys;
    pfn_AP_SetLanguage        pfnSetLanguage;

    // Cached metadata from AP_GetPluginInfo
    const char* displayName;
    const char* version;
    const char* description;
    const char* configFile;
    bool supportsHotDisable;

    // Cached hotkeys
    AP_HotkeyInfo hotkeys[16];
    int hotkeyCount;
};

// ============================================================================
// Managed Plugin Storage
// ============================================================================

#define MAX_MANAGED_PLUGINS 32

static ManagedPlugin g_plugins[MAX_MANAGED_PLUGINS];
static int g_pluginCount = 0;

// ============================================================================
// Internal: Read version string from DLL VERSIONINFO resource
// ============================================================================
#pragma comment(lib, "version.lib")

static bool ReadDllVersion(const char* dllPath, char* outBuf, int bufSize) {
    DWORD dummy;
    DWORD verSize = GetFileVersionInfoSizeA(dllPath, &dummy);
    if (verSize == 0) return false;

    std::vector<BYTE> verData(verSize);
    if (!GetFileVersionInfoA(dllPath, 0, verSize, verData.data())) return false;

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT fileInfoLen = 0;
    if (!VerQueryValueA(verData.data(), "\\", (void**)&fileInfo, &fileInfoLen)) return false;
    if (fileInfoLen == 0 || !fileInfo) return false;

    snprintf(outBuf, bufSize, "%d.%d.%d",
             HIWORD(fileInfo->dwFileVersionMS),
             LOWORD(fileInfo->dwFileVersionMS),
             HIWORD(fileInfo->dwFileVersionLS));
    return true;
}

// ============================================================================
// Internal: Resolve standard interface from a loaded DLL
// ============================================================================

static void ResolvePluginInterface(ManagedPlugin& p) {
    if (!p.hModule) return;

    p.pfnGetInfo      = (pfn_AP_GetPluginInfo)     GetProcAddress(p.hModule, "AP_GetPluginInfo");
    p.pfnEnable       = (pfn_AP_PluginEnable)      GetProcAddress(p.hModule, "AP_PluginEnable");
    p.pfnDisable      = (pfn_AP_PluginDisable)     GetProcAddress(p.hModule, "AP_PluginDisable");
    p.pfnReloadConfig = (pfn_AP_ReloadConfig)       GetProcAddress(p.hModule, "AP_ReloadConfig");
    p.pfnGetHotkeys   = (pfn_AP_GetHotkeys)        GetProcAddress(p.hModule, "AP_GetHotkeys");
    p.pfnSetLanguage  = (pfn_AP_SetLanguage)        GetProcAddress(p.hModule, "AP_SetLanguage");

    p.hasInterface = (p.pfnGetInfo != nullptr);

    if (p.pfnGetInfo) {
        AP_PluginInfo* info = p.pfnGetInfo();
        if (info && info->apiVersion == APPLEPIE_PLUGIN_API_VERSION) {
            p.displayName       = info->displayName;
            p.description       = info->description;
            p.configFile        = info->configFile;
            p.supportsHotDisable = info->supportsHotDisable;
        }
    }

    // Read version from DLL VERSIONINFO resource
    ReadDllVersion(p.dllPath, p.versionBuf, sizeof(p.versionBuf));
    p.version = p.versionBuf;

    // Query hotkeys
    p.hotkeyCount = 0;
    if (p.pfnGetHotkeys) {
        p.hotkeyCount = p.pfnGetHotkeys(p.hotkeys, 16);
    }
}

// ============================================================================
// Scan plugin directory and build registry
// ============================================================================

static void ScanPluginDirectory(const char* pluginDir) {
    g_pluginCount = 0;

    char searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s\\*.dll", pluginDir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        // Skip ourselves
        if (_stricmp(fd.cFileName, "applepie_manager.dll") == 0) continue;
        if (g_pluginCount >= MAX_MANAGED_PLUGINS) break;

        ManagedPlugin& p = g_plugins[g_pluginCount];
        memset(&p, 0, sizeof(ManagedPlugin));

        strncpy(p.dllName, fd.cFileName, sizeof(p.dllName) - 1);
        snprintf(p.dllPath, MAX_PATH, "%s\\%s", pluginDir, fd.cFileName);

        // Default display name = DLL filename without extension
        p.displayName = p.dllName;
        p.enabled = true;   // default: enabled (overridden by config)
        p.active = true;

        g_pluginCount++;
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

// ============================================================================
// Broadcast language to all loaded plugins
// ============================================================================

static void BroadcastLanguage(const char* langCode) {
    for (int i = 0; i < g_pluginCount; i++) {
        ManagedPlugin& p = g_plugins[i];
        if (p.loaded && p.pfnSetLanguage) {
            p.pfnSetLanguage(langCode);
        }
    }
}

// ============================================================================
// Apply enable/disable state from manager config
// ============================================================================

static void ApplyPluginConfig(const ConfigFile& cfg) {
    for (int i = 0; i < g_pluginCount; i++) {
        ManagedPlugin& p = g_plugins[i];
        const char* val = ConfigGetValue(cfg, p.dllName, "plugins");
        if (val) {
            p.enabled = (strcmp(val, "1") == 0 || _stricmp(val, "true") == 0);
        }
        // If not listed, default to enabled (already set)
    }
}

// ============================================================================
// Discover plugins already loaded by proxy (via GetModuleHandle, not LoadLibrary)
// ============================================================================

static void LoadEnabledPlugins(void (*logFn)(const char*, ...) = nullptr) {
    for (int i = 0; i < g_pluginCount; i++) {
        ManagedPlugin& p = g_plugins[i];

        // Check if already loaded by proxy
        p.hModule = GetModuleHandleA(p.dllPath);
        if (!p.hModule) {
            // Try just the filename (proxy may have loaded with relative path)
            p.hModule = GetModuleHandleA(p.dllName);
        }

        if (p.hModule) {
            p.loaded = true;
            p.active = p.enabled;
            ResolvePluginInterface(p);
            if (logFn) {
                if (p.hasInterface) {
                    logFn("[AM] Found: %s [%s v%s]%s",
                          p.dllName, p.displayName ? p.displayName : "?",
                          p.version ? p.version : "?",
                          p.enabled ? "" : " (disabled in config)");
                } else {
                    logFn("[AM] Found: %s (no standard interface)%s",
                          p.dllName,
                          p.enabled ? "" : " (disabled in config)");
                }
            }
        } else {
            p.loaded = false;
            p.active = false;
            if (logFn) logFn("[AM] Not loaded: %s", p.dllName);
        }
    }
}

// ============================================================================
// Runtime control helpers
// ============================================================================

static bool TogglePluginActive(int index) {
    if (index < 0 || index >= g_pluginCount) return false;
    ManagedPlugin& p = g_plugins[index];
    if (!p.loaded || !p.supportsHotDisable) return false;

    if (p.active) {
        // Disable
        if (p.pfnDisable) {
            bool ok = p.pfnDisable();
            if (ok) p.active = false;
            return ok;
        }
    } else {
        // Enable
        if (p.pfnEnable) {
            bool ok = p.pfnEnable();
            if (ok) p.active = true;
            return ok;
        }
    }
    return false;
}

static bool ReloadPluginConfig(int index) {
    if (index < 0 || index >= g_pluginCount) return false;
    ManagedPlugin& p = g_plugins[index];
    if (!p.loaded || !p.pfnReloadConfig) return false;
    bool ok = p.pfnReloadConfig();
    // Refresh hotkey cache after reload
    if (p.pfnGetHotkeys) {
        p.hotkeyCount = p.pfnGetHotkeys(p.hotkeys, 16);
    }
    return ok;
}

// ============================================================================
// Save plugin enabled states back to manager config
// ============================================================================

static void SavePluginStates(ConfigFile& cfg) {
    for (int i = 0; i < g_pluginCount; i++) {
        ConfigSetValue(cfg, g_plugins[i].dllName,
                       g_plugins[i].enabled ? "1" : "0", "plugins");
    }
    // Note: UID hide settings are saved by the caller (imgui_panel.h)
    // since the UID state variables are defined in il2cpp_text_scanner.h
    SaveConfigFile(cfg);
}

// ============================================================================
// Hotkey conflict detection
// ============================================================================

struct HotkeyConflict {
    int vk;
    int pluginIdx1;
    int hotkeyIdx1;
    int pluginIdx2;
    int hotkeyIdx2;
};

static std::vector<HotkeyConflict> DetectHotkeyConflicts() {
    std::vector<HotkeyConflict> conflicts;
    struct Entry { int vk; int pluginIdx; int hotkeyIdx; };
    std::vector<Entry> all;

    for (int i = 0; i < g_pluginCount; i++) {
        if (!g_plugins[i].loaded || !g_plugins[i].active) continue;
        for (int j = 0; j < g_plugins[i].hotkeyCount; j++) {
            if (g_plugins[i].hotkeys[j].currentVK != 0) {
                all.push_back({g_plugins[i].hotkeys[j].currentVK, i, j});
            }
        }
    }

    for (size_t a = 0; a < all.size(); a++) {
        for (size_t b = a + 1; b < all.size(); b++) {
            if (all[a].vk == all[b].vk) {
                conflicts.push_back({all[a].vk,
                    all[a].pluginIdx, all[a].hotkeyIdx,
                    all[b].pluginIdx, all[b].hotkeyIdx});
            }
        }
    }

    return conflicts;
}
