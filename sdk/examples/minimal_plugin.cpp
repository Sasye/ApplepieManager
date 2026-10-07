// This plugin hooks ONLY its own sample function. It needs no game addresses,
// IL2CPP, current character or write leases. DllMain remains passive.
#include "../applepie_client.h"

static applepie::Client g_client;
using SampleHook = applepie::NativeHook<1, int(*)(int)>;
static volatile LONG workers = 0, original_calls = 0;

extern "C" __declspec(dllexport) __declspec(noinline) int APH_ExampleTarget(int value) {
    InterlockedIncrement(&original_calls);
    return value + 1;
}
extern "C" __declspec(dllexport) LONG APH_ExampleOriginalCalls() {
    return InterlockedCompareExchange(&original_calls, 0, 0);
}
static int Business(int value) { return SampleHook::Original(value) + 10; }

static APH_Result APH_CALL Restore(APH_Owner, const APH_Cleanup*, void*) {
    // The Core closes business admission. Keep the native claim for re-enable.
    // A pending initialization worker observes the closed epoch and exits.
    return InterlockedCompareExchange(&workers, 0, 0) ? APH_PENDING : APH_OK;
}
static DWORD WINAPI Initialize(void* parameter) {
    const uint64_t epoch = (uint64_t)(uintptr_t)parameter;
    APH_Result result = APH_PENDING;
    const ULONGLONG deadline = GetTickCount64() + 5000;
    __try {
        while (result == APH_PENDING || result == APH_E_BUSY) {
            APH_OwnerStatus owner{};
            if (g_client.Status(owner) != APH_OK || owner.state != APH_OWNER_ENABLING ||
                owner.activation_epoch != epoch) return 0;
            result = SampleHook::Advance();
            if (GetTickCount64() >= deadline && result != APH_OK) { result = APH_E_UNAVAILABLE; break; }
            if (result == APH_PENDING || result == APH_E_BUSY) Sleep(1);
        }
        // Core rejects a completion from an old/disabled epoch.
        g_client.CompleteEnable(epoch, result);
        OutputDebugStringA(result == APH_OK ? "[APH example] enabled\n" : "[APH example] initialization failed\n");
    } __finally { InterlockedDecrement(&workers); }
    return 0;
}

extern "C" __declspec(dllexport) const APH_Manifest APH_PluginManifest = {
    sizeof(APH_Manifest), APH_ABI_MAJOR, APH_ABI_MINOR, APH_ARCH_X64,
    APH_PLUGIN_LIFECYCLE | APH_PLUGIN_LATE_LOAD,
    APH_CAP_CORE | APH_CAP_HOOKS, 0, "example.minimal", "Applepie SDK example"
};
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_Attach(const APH_AttachArgs* args) {
    APH_Result r = g_client.Attach(args);
    if (r != APH_OK) return r;
    const APH_LifecycleDesc life{sizeof(life), 10000, nullptr, Restore, nullptr};
    return g_client.RegisterLifecycle(life);
}
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_Enable(APH_Owner owner, uint64_t epoch) {
    if (owner != g_client.owner() || !epoch) return APH_E_ARGUMENT;
    APH_Result r = SampleHook::Request(g_client, 1, APH_ExampleTarget, Business,
        "i32(i32)", "example.local-counter");
    if (r < 0) return r;
    r = SampleHook::Advance();
    if (r != APH_PENDING && r != APH_E_BUSY) return r;
    // Explicit initialization worker belonging to THIS example. The SDK never
    // creates a thread. Existing plugins can advance requests on their workers.
    InterlockedIncrement(&workers);
    HANDLE thread = CreateThread(nullptr, 0, Initialize, (void*)(uintptr_t)epoch, 0, nullptr);
    if (!thread) { InterlockedDecrement(&workers); return APH_E_UNAVAILABLE; }
    CloseHandle(thread);
    return APH_PENDING;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, void*) { return TRUE; }
