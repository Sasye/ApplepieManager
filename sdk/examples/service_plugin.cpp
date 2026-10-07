// Build twice: /DSERVICE_PROVIDER=1 and /DSERVICE_PROVIDER=0. Plain POD messages;
// no game/runtime dependency, threads, raw provider pointers or private hooks.
#include "../applepie_client.h"
static applepie::Client client;
static APH_PluginServiceApi services{};
static APH_PluginService endpoint;
static APH_Result APH_CALL Restore(APH_Owner,const APH_Cleanup*,void*){return APH_OK;}
static APH_Result APH_CALL Compute(void*,uint32_t operation,const void* input,uint32_t in_bytes,void* output,uint32_t out_bytes) {
    if(operation!=1||in_bytes!=sizeof(int32_t)||out_bytes!=sizeof(int32_t))return APH_E_ARGUMENT;
    const int32_t value=*(const int32_t*)input;
    if(value>INT32_MAX/2||value<INT32_MIN/2)return APH_E_ARGUMENT;
    *(int32_t*)output=value*2;return APH_OK;
}
#if SERVICE_PROVIDER
#define SAMPLE_ID "example.service.provider"
#else
#define SAMPLE_ID "example.service.consumer"
#endif
extern "C" __declspec(dllexport) const APH_Manifest APH_PluginManifest{
    sizeof(APH_Manifest),1,1,APH_ARCH_X64,APH_PLUGIN_LIFECYCLE|APH_PLUGIN_LATE_LOAD,
    APH_CAP_CORE,0,SAMPLE_ID,SAMPLE_ID};
extern "C" __declspec(dllexport) const APH_PluginInfoData APH_PluginInfo{
    sizeof(APH_PluginInfoData),1,APH_VERSION(1,0,0),
#if SERVICE_PROVIDER
    0,0,{}
#else
    1,0,{{"example.service.provider",APH_VERSION(1,0,0),APH_VERSION(2,0,0),0,0}}
#endif
};
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_Attach(const APH_AttachArgs* args) {
    auto result=client.Attach(args);if(result!=APH_OK)return result;
    result=client.Service(services);if(result!=APH_OK)return result;
    const APH_LifecycleDesc life{sizeof(life),2000,nullptr,Restore,nullptr};return client.RegisterLifecycle(life);
}
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_Enable(APH_Owner owner,uint64_t epoch) {
#if SERVICE_PROVIDER
    APH_ServiceDesc desc{};desc.size=sizeof(desc);desc.activation_epoch=epoch;
    strcpy_s(desc.name,"double-i32");desc.major=1;desc.invoke=Compute;
    return services.publish(owner,&desc,&endpoint); // Not callable until Active.
#else
    (void)owner;(void)epoch;
    const APH_ServiceQuery query{sizeof(query),1,"example.service.provider","double-i32",0,0};
    return services.find(client.owner(),&query,&endpoint);
#endif
}
#if !SERVICE_PROVIDER
// Illustrative business entry. An activation-epoch token from the owner is
// required on every call; the service itself also checks the provider's epoch.
extern "C" __declspec(dllexport) APH_Result APH_CALL APH_ExampleCompute(int32_t value,int32_t* output) {
    APH_OwnerStatus status{};auto result=client.Status(status);if(result!=APH_OK)return result;
    return services.invoke(client.owner(),status.activation_epoch,endpoint,1,&value,sizeof(value),output,sizeof(*output));
}
#endif
BOOL WINAPI DllMain(HMODULE module,DWORD why,void*){if(why==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
