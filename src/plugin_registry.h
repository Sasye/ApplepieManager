#pragma once
#include "manager_loader.h"
#include "config_parser.h"
// UI projection only. Loader and Core own runtime truth; no legacy entry calls.
static bool g_pluginEnabled[128]{};
static std::string ManagerConfigPath() {
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\applepie_manager_config.txt", loader::directory());
    char utf8[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
    return utf8;
}
static void InitializePluginView() {
    for (unsigned i = 0; i < loader::count(); ++i) {
        loader::Plugin plugin{};
        if (loader::snapshot(i, &plugin)) g_pluginEnabled[i] = plugin.enabled;
    }
}
static void SavePluginStates(ConfigFile& config) {
    for (unsigned i = 0; i < loader::count(); ++i) {
        loader::Plugin plugin{};
        if (!loader::snapshot(i, &plugin)) continue;
        char filename[MAX_PATH * 3];
        WideCharToMultiByte(CP_UTF8, 0, plugin.filename, -1, filename, sizeof(filename), nullptr, nullptr);
        ConfigSetValue(config, filename, g_pluginEnabled[i] ? "1" : "0", "plugins");
    }
    SaveConfigFile(config);
}
