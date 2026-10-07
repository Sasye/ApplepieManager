#pragma once
#include <windows.h>
#include <cstddef>

namespace applepie {
// Optional IL2CPP worker bootstrap. The host must validate these non-allocating
// probes and startup order against its game build. CoreReady is not GC readiness.
// Caller provides shutdown exclusion; this helper neither starts nor stops a VM.
struct Il2CppBootstrap {
    void* (*corlib)() = nullptr;
    void** (*threads)(size_t*) = nullptr;
    void* (*domain)() = nullptr;
    void* (*attach)(void*) = nullptr;
    void (*detach)(void*) = nullptr;
    void* (*current)() = nullptr;
    bool Resolve(HMODULE module) {
        corlib=(decltype(corlib))GetProcAddress(module,"il2cpp_get_corlib");
        threads=(decltype(threads))GetProcAddress(module,"il2cpp_thread_get_all_attached_threads");
        domain=(decltype(domain))GetProcAddress(module,"il2cpp_domain_get");
        attach=(decltype(attach))GetProcAddress(module,"il2cpp_thread_attach");
        detach=(decltype(detach))GetProcAddress(module,"il2cpp_thread_detach");
        current=(decltype(current))GetProcAddress(module,"il2cpp_thread_current");
        return corlib&&threads&&domain&&attach&&detach&&current;
    }
    bool Ready() const {
        if(!corlib||!threads||!corlib())return false;
        size_t count=0;return threads(&count)&&count!=0; // Never traverse this borrowed array.
    }
    void* AttachWorker(void** owned) const {
        *owned=nullptr;
        if(!Ready())return nullptr;
        void* d=domain();if(!d)return nullptr;
        if(!current()) { *owned=attach(d);if(!*owned)return nullptr; }
        return d;
    }
    void DetachWorker(void* owned) const { if(owned)detach(owned); }
};
}
