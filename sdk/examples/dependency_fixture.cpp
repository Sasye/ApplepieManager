// Offline loader fixture. Built with FIXTURE=1..11 into an isolated test folder.
#include "../applepie_client.h"
#define STR_(x) #x
#define STR(x) STR_(x)
static applepie::Client client;
static APH_Result APH_CALL Restore(APH_Owner,const APH_Cleanup*,void*){return APH_OK;}
extern "C" __declspec(dllexport) const APH_Manifest APH_PluginManifest{
    sizeof(APH_Manifest),1,FIXTURE==9?0:1,APH_ARCH_X64,
    APH_PLUGIN_LIFECYCLE|APH_PLUGIN_LATE_LOAD,APH_CAP_CORE,0,"fixture." STR(FIXTURE),"Dependency fixture " STR(FIXTURE)};
#if FIXTURE!=9
constexpr APH_PluginInfoData Info() {
    APH_PluginInfoData info{sizeof(info),1,APH_VERSION(2,1,0)};
#if FIXTURE==1 || FIXTURE==10 || FIXTURE==11
    info.dependency_count=1;info.dependencies[0]={"fixture.8",APH_VERSION(2,0,0),APH_VERSION(3,0,0)};
#elif FIXTURE==2
    info.dependency_count=2;info.dependencies[0]={"fixture.absent",0,0,APH_DEP_OPTIONAL};
    info.dependencies[1]={"fixture.8",APH_VERSION(3,0,0),0,APH_DEP_OPTIONAL};
#elif FIXTURE==3
    info.dependency_count=1;info.dependencies[0]={"fixture.absent"};
#elif FIXTURE==4
    info.dependency_count=1;info.dependencies[0]={"fixture.8",APH_VERSION(3,0,0)};
#elif FIXTURE==5
    info.dependency_count=1;info.dependencies[0]={"fixture.6"};
#elif FIXTURE==6
    info.dependency_count=1;info.dependencies[0]={"fixture.5"};
#elif FIXTURE==7
    info.dependency_count=99;
#endif
    return info;
}
extern "C" __declspec(dllexport) const APH_PluginInfoData APH_PluginInfo=Info();
#endif
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_Attach(const APH_AttachArgs* args) {
    auto r=client.Attach(args);if(r!=APH_OK)return r;
    const APH_LifecycleDesc life{sizeof(life),5000,nullptr,Restore,nullptr};r=client.RegisterLifecycle(life);
#if FIXTURE==11
    // Deterministically lose the required provider after Loader's preflight
    // check but before this plugin enters Enable. This export is test-only.
    auto owner=(APH_Owner(APH_CALL*)())GetProcAddress(GetModuleHandleW(L"fixture8.dll"),"FixtureOwner");
    APH_LifecycleApi api{};
    if(r!=APH_OK||!owner||client.Service(api)!=APH_OK)return APH_E_UNAVAILABLE;
    r=api.disable(owner());return r>=0?APH_OK:r;
#else
    return r;
#endif
}
extern "C" __declspec(dllexport) APH_Owner APH_CALL FixtureOwner(){return client.owner();}
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_Enable(APH_Owner,uint64_t) {return FIXTURE==10?APH_PENDING:APH_OK;}
BOOL WINAPI DllMain(HMODULE module,DWORD why,void*) {if(why==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
