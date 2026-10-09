#pragma once
#include "manager_loader.h"
// UI projection only. Loader and Core own runtime truth; no legacy entry calls.
static bool g_pluginEnabled[128]{};
static void InitializePluginView() {
    for (unsigned i = 0; i < loader::count(); ++i) {
        loader::Plugin plugin{};
        if (loader::snapshot(i, &plugin)) g_pluginEnabled[i] = plugin.enabled;
    }
}
