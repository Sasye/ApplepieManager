#pragma once
#include "hub_core.h"
#include "../sdk/applepie_seh.h"
#include "../sdk/applepie.h"
#include <type_traits>
#include <atomic>
#include "../sdk/applepie_client.h"
#include "../sdk/applepie_il2cpp.h"
static std::atomic<bool> g_hookDisabled{false};
static std::atomic<DWORD> g_uidGameThread{0};
static SRWLOCK g_uidVmLock=SRWLOCK_INIT;
static __declspec(thread) bool g_uidVmHeld;
static applepie::Client g_uidClient;
static applepie::Il2CppBootstrap g_uidBootstrap;
static void StopUidRuntime() {
    g_hookDisabled.store(true,std::memory_order_release);
    // Wait for a metadata worker/business operation before the VM's original
    // shutdown runs. A same-thread nested stop already owns this boundary.
    if(!g_uidVmHeld){AcquireSRWLockExclusive(&g_uidVmLock);ReleaseSRWLockExclusive(&g_uidVmLock);}
}
static bool UidOnGameThread() {
    DWORD thread=g_uidGameThread.load(std::memory_order_acquire);
    if(!thread) {
        HWND own=nullptr;
        EnumThreadWindows(GetCurrentThreadId(),[](HWND wnd,LPARAM context)->BOOL {
            wchar_t name[64]{};DWORD process=0;GetWindowThreadProcessId(wnd,&process);
            if(process==GetCurrentProcessId()&&GetClassNameW(wnd,name,64)&&!wcscmp(name,L"UnityWndClass")) {
                *(HWND*)context=wnd;return FALSE;
            }
            return TRUE;
        },(LPARAM)&own);
        if(!own)return false;
        thread=GetCurrentThreadId();
        g_uidGameThread.store(thread,std::memory_order_release);
    }
    return thread==GetCurrentThreadId();
}
// The Manager's existing UID feature uses the same backend control path. This
// does not advertise a validated game/GUI capability to business plugins.
static APH_Owner g_uidOwner;
static APH_HookApi g_uidHooks{};
static APH_Hook g_setTextClaim, g_generateClaim;
static APH_RuntimeApi g_uidRuntime{};
static bool UidRuntimeOpen() {
    return !g_hookDisabled.load(std::memory_order_acquire)&&g_uidRuntime.probe_open&&
        g_uidRuntime.probe_open(g_uidOwner)==APH_OK;
}
template<class> struct UidRuntimeFunction;
template<class R,class... Args> struct UidRuntimeFunction<R(*)(Args...)> {
    using Pointer=R(*)(Args...);Pointer function=nullptr;
    UidRuntimeFunction& operator=(Pointer p){function=p;return *this;}
    explicit operator bool()const{return function!=nullptr;}
    bool operator==(std::nullptr_t)const{return !function;}
    bool operator!=(std::nullptr_t)const{return function!=nullptr;}
    R operator()(Args... args)const {
        if(!function||!UidRuntimeOpen()){if constexpr(std::is_void_v<R>)return;else return R{};}
        return function(args...);
    }
};
static bool InitializeUidOwner() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCWSTR)&InitializeUidOwner, &self)) return false;
    APH_Manifest manifest{sizeof(manifest), 1, 0, APH_ARCH_X64, APH_PLUGIN_LIFECYCLE|APH_PLUGIN_MODULE_OBSERVER, APH_CAP_CORE | APH_CAP_HOOKS,
                          0, "applepie.manager.uid", "Manager UID"};
    g_uidOwner = hub::add_owner(manifest, self);
    if (!g_uidOwner || hub::api()->copy_service(g_uidOwner, APH_SERVICE_HOOKS, 1, sizeof(g_uidHooks), &g_uidHooks) != APH_OK) return false;
    if(hub::api()->copy_service(g_uidOwner,APH_SERVICE_RUNTIME,1,sizeof(g_uidRuntime),&g_uidRuntime)!=APH_OK)return false;
    APH_AttachArgs args{sizeof(args),0,g_uidOwner,hub::api()};
    return g_uidClient.Attach(&args)==APH_OK&&hub::begin_enable(g_uidOwner)!=0;
}
static bool WaitUidHook(APH_Hook claim, uint32_t state, APH_NativeProc* original = nullptr) {
    // Initialization thread only; native entries must never wait for themselves.
    for (unsigned i = 0; i < 500; ++i) {
        if(!UidRuntimeOpen())return false;
        APH_HookStatus status{};
        if (g_uidHooks.query_hook(g_uidOwner, claim, sizeof(status), &status) != APH_OK || status.result < 0) return false;
        if (status.state == state && status.result == APH_OK) {
            if (original) *original = status.original;
            return true;
        }
        Sleep(10);
    }
    return false;
}
static bool PrepareUidHook(void* target, APH_NativeProc detour, uint64_t key,
                           const char* signature, APH_Hook* claim, APH_NativeProc* original) {
    APH_HookRequest request{};
    request.size = sizeof(request); request.mode = APH_EXCLUSIVE; request.request_key = key;
    request.target = (APH_NativeProc)target; request.native_entry = detour;
    request.recovery = APH_RECOVERY_NONE; // cached GC handles / field writes are not read-only
    strcpy_s(request.native_signature, signature); strcpy_s(request.diagnostic_name, "Manager UID");
    if (!APH_ModuleBuild(request.target, &request.module_timestamp, &request.module_image_size)) return false;
    if (g_uidHooks.request_hook(g_uidOwner, &request, claim) < 0) return false;
    return WaitUidHook(*claim, APH_HOOK_PREPARED, original);
}


// Matches the existing zero-payload shutdown contracts used by EIEM/other plugins.
template<unsigned Tag,class... Args> struct UidStopHook {
    inline static applepie::SharedHook hook;
    inline static applepie::Subscription observer;
    inline static APH_NativeProc original;
    static APH_Result APH_CALL Observe(APH_Owner,const APH_SharedEvent*,void*) {StopUidRuntime();return APH_OK;}
    static int Fault(EXCEPTION_POINTERS* e,uint32_t phase) {
        hook.api()->native_fault(hook.owner(),hook.handle(),e->ExceptionRecord->ExceptionCode,
            (uint64_t)e->ExceptionRecord->ExceptionAddress,phase);return EXCEPTION_CONTINUE_SEARCH;
    }
    static void Entry(Args... args) {
        APH_SharedCall call{};const auto entered=hook.api()->enter(hook.owner(),hook.handle(),&call);
        uint32_t phase=APH_ORIGINAL_NOT_ENTERED;
        __try {
            __try {
                StopUidRuntime();
                if(entered==APH_OK&&hook.api()->publish(&call,APH_BEFORE,nullptr,0)<0)
                    RaiseException(0xe0411001u,EXCEPTION_NONCONTINUABLE,0,nullptr);
                phase=APH_ORIGINAL_RUNNING;((void(*)(Args...))original)(args...);phase=APH_ORIGINAL_RETURNED;
                if(entered==APH_OK&&hook.api()->publish(&call,APH_AFTER,nullptr,0)<0)
                    RaiseException(0xe0411001u,EXCEPTION_NONCONTINUABLE,0,nullptr);
            } __except(Fault(GetExceptionInformation(),phase)){}
        } __finally {if(entered==APH_OK)hook.api()->leave(&call);}
    }
    static bool Install(APH_NativeProc target,const char* signature,const char* contract) {
        APH_SharedRequest request{};
        if(applepie::MakeSharedRequest(request,Tag,target,(APH_NativeProc)Entry,signature,contract,1,0)!=APH_OK||
            hook.Request(g_uidClient,request)<0)return false;
        APH_SharedListen listen{sizeof(listen),APH_BEFORE,APH_MODULE_OBSERVER,APH_RECOVERY_NONE,Observe,nullptr};
        if(observer.Listen(hook,listen)!=APH_OK)return false;
        for(unsigned i=0;i<500&&UidRuntimeOpen();++i) {
            const auto result=hook.Advance(&original);if(result==APH_OK)return true;
            if(result<0&&result!=APH_E_BUSY)return false;Sleep(10);
        }
        return false;
    }
};
using UidShutdown=UidStopHook<100>;
using UidApplicationQuit=UidStopHook<101,void*>;
