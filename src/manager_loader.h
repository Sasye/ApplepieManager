#pragma once
#include "hub_core.h"
#include "../sdk/applepie.h"
namespace loader {
enum State { Disabled, Dependency, Rejected, Duplicate, LoadFailed, Loaded,
             AttachFailed, Attached, WaitingCapabilities, Enabling, Active, Failed, NeedsRestart, Disabling, Queued, WaitingDependencies };
struct Plugin {
    wchar_t filename[MAX_PATH];
    APH_Manifest manifest;
    APH_PluginInfoData info;
    HMODULE module;
    APH_Owner owner;
    uint32_t state;
    APH_Result result;
    bool enabled, loaded, attached, capabilities_ready;
    bool inspected, has_info;
    uint32_t win32_error;
    char stage[32], detail[256];
};
unsigned count();
bool snapshot(unsigned index, Plugin*);
const wchar_t* directory();
const char* state_name(uint32_t state);
APH_Result request_enabled(unsigned index,bool enabled); // UI enqueues; never loads/waits here
void start_features(); // production Manager UI; headless test implementation does nothing
}
