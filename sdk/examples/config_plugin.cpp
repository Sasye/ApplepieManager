// Optional basic settings, with no Hook or game dependency. Business code reads
// settings at its own safe boundary. Complex files/UI remain plugin-owned.
#include "../applepie_client.h"
#include <atomic>
#include <mutex>
static applepie::Client client;
static applepie::Config config;
static std::mutex settingsLock;
static std::atomic<int> refreshMs{100};
static APH_Result ApplySettings() {
    try {
        std::lock_guard<std::mutex> guard(settingsLock);
        APH_ConfigSnapshot state{}; std::vector<APH_ConfigValue> values;
        const auto result = config.Read(state, values);
        if (result == APH_OK) refreshMs = values[1].integer;
        return result;
    } catch (...) { return APH_E_FAULT; }
}
static void APH_CALL SettingsChanged(APH_Owner, void*) {
    ApplySettings(); // plugin-owned cache only; no game/GUI calls on the save worker
}
static APH_Result APH_CALL Restore(APH_Owner, const APH_Cleanup*, void*) { return APH_OK; }
extern "C" __declspec(dllexport) const APH_Manifest APH_PluginManifest = {
    sizeof(APH_Manifest), APH_ABI_MAJOR, APH_ABI_MINOR, APH_ARCH_X64,
    APH_PLUGIN_LIFECYCLE | APH_PLUGIN_LATE_LOAD | APH_PLUGIN_MODULE_OBSERVER,
    APH_CAP_CORE, 0, "example.config", "Basic settings example"
};
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_Attach(const APH_AttachArgs* args) {
    auto result = client.Attach(args);
    if (result != APH_OK) return result;
    const APH_LifecycleDesc life{sizeof(life), 10000, nullptr, Restore, nullptr};
    result = client.RegisterLifecycle(life);
    if (result != APH_OK) return result;
    APH_ConfigField fields[5]{};
    const char* keys[]{"show_status", "refresh_ms", "scale", "theme", "caption"};
    const char* labels[]{"Show status", "Refresh interval (ms)", "Scale", "Theme", "Caption"};
    for (unsigned i = 0; i < 5; ++i) {
        auto& f = fields[i]; f.size = sizeof(f); f.type = f.initial.type = i + 1;
        strcpy_s(f.key, keys[i]); strcpy_s(f.label, labels[i]);
    }
    fields[0].initial.integer = 1;
    fields[1].initial.integer = 100; fields[1].minimum = 16; fields[1].maximum = 1000;
    fields[2].initial.number = 1.0; fields[2].minimum = 0.5; fields[2].maximum = 2.0;
    const APH_ConfigOption themes[]{{"light"}, {"dark"}, {"system"}};
    fields[3].options = themes; fields[3].option_count = 3; strcpy_s(fields[3].initial.text, "system");
    fields[4].text_max_bytes = 120; strcpy_s(fields[4].initial.text, "Applepie");
    // All local strings/options are copied. Older hosts return UNAVAILABLE.
    result = config.Register(client, fields, 5);
    if (result != APH_OK) return result;
    result = config.Notify(SettingsChanged);
    return result == APH_OK ? ApplySettings() : result;
}
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_Enable(APH_Owner owner, uint64_t epoch) {
    return owner == client.owner() && epoch ? ApplySettings() : APH_E_ARGUMENT;
}
// Business reads its local cache. No periodic revision lookup or file access.
extern "C" __declspec(dllexport) int APH_ConfigExampleRefreshMs() {
    APH_OwnerStatus owner{};
    if (client.Status(owner) != APH_OK || owner.state != APH_OWNER_ACTIVE) return -1;
    return refreshMs.load();
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, void*) { return TRUE; }
