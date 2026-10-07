#pragma once
#include "../sdk/applepie.h"
#include <windows.h>
#include <cwchar>
// DllMain schedules and returns; this entry cannot execute until loader-lock
// notifications finish. No waiting, scanning, config or LoadLibrary in DllMain.
static DWORD WINAPI BootstrapManager(void*) {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                      (LPCWSTR)&BootstrapManager, &self);
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, _countof(path));
    if (!n || n >= _countof(path)) return 1;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash || size_t(slash - path) + 29 >= _countof(path)) return 1;
    wcscpy_s(slash, _countof(path) - (slash - path), L"\\plugin\\applepie_manager.dll");
    HMODULE manager = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!manager) { OutputDebugStringA("[APH proxy] Manager load failed\n"); return 1; }
    auto bootstrap = (APH_BootstrapFn)GetProcAddress(manager, "APH_Bootstrap");
    if (!bootstrap || bootstrap(APH_ABI_MAJOR) < 0) {
        OutputDebugStringA("[APH proxy] Bootstrap failed\n"); return 1;
    }
    // Host reference retained. Manager owns process-lifetime idempotence and scan.
    return 0;
}
static void ScheduleManagerBootstrap() {
    HANDLE worker = CreateThread(nullptr, 0, BootstrapManager, nullptr, 0, nullptr);
    if (worker) CloseHandle(worker);
}
