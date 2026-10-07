#define NOMINMAX
#include "../../src/hub_internal.h"
#include "../../src/manager_loader.h"
#include "../../src/bounded_log.h"
#include "../../src/il2cpp_text_scanner.h"
#include <thread>
#include <string>
#include <fstream>
#include <cstdio>
#include <cstdarg>

static volatile LONG checks;
#define CHECK(x) do {InterlockedIncrement(&checks);if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);fflush(stdout);ExitProcess(10);}}while(0)
void Log(const char*,...){}
namespace loader {void start_features(){}}
template<class F> void Await(F f) {
    auto deadline=GetTickCount64()+5000;
    while(!f()){CHECK(GetTickCount64()<deadline);Sleep(1);}
}
static APH_Result APH_CALL Restore(APH_Owner,const APH_Cleanup*,void* context) {
    if(context)InterlockedIncrement((volatile LONG*)context);return APH_OK;
}
static void Add(applepie::Client& c,const char* id,void* restores=nullptr,bool active=true) {
    APH_Manifest m{sizeof(m),1,1,APH_ARCH_X64,APH_PLUGIN_LIFECYCLE,APH_CAP_CORE,0};
    strcpy_s(m.id,id);strcpy_s(m.display_name,id);
    auto owner=hub::add_owner(m,GetModuleHandleW(nullptr));CHECK(owner);
    APH_AttachArgs args{sizeof(args),0,owner,hub::api()};CHECK(c.Attach(&args)==APH_OK);
    APH_LifecycleDesc life{sizeof(life),2000,nullptr,Restore,restores};CHECK(c.RegisterLifecycle(life)==APH_OK);
    CHECK(hub::begin_enable(owner));if(active)CHECK(hub::finish_enable(owner,APH_OK)==APH_OK);
}
static void Disabled(applepie::Client& c){Await([&]{APH_OwnerStatus s{};c.Status(s);return s.state==APH_OWNER_DISABLED;});}
struct Barrier {HANDLE entered,release;volatile LONG calls;};
static APH_Result APH_CALL Service(void* context,uint32_t op,const void* input,uint32_t in_bytes,void* output,uint32_t out_bytes) {
    auto b=(Barrier*)context;InterlockedIncrement(&b->calls);
    if(op==1){SetEvent(b->entered);CHECK(WaitForSingleObject(b->release,5000)==WAIT_OBJECT_0);}
    if(op==2){volatile int* invalid=(int*)1;return *invalid;}
    if(in_bytes!=4||out_bytes!=4)return APH_E_ARGUMENT;
    *(int*)output=*(const int*)input+7;return APH_OK;
}
static void Services() {
    volatile LONG restores=0;applepie::Client provider,consumer;Add(provider,"test.provider",(void*)&restores,false);Add(consumer,"test.consumer");
    APH_PluginServiceApi api{};CHECK(provider.Service(api)==APH_OK);
    APH_DiagnosticsApi diagnostics{};CHECK(provider.Service(diagnostics)==APH_OK);
    Barrier b{CreateEventW(nullptr,TRUE,FALSE,nullptr),CreateEventW(nullptr,TRUE,FALSE,nullptr),0};
    APH_ServiceDesc desc{};desc.size=sizeof(desc);desc.activation_epoch=hub::owner_epoch(provider.owner());
    strcpy_s(desc.name,"math");desc.major=1;desc.minor=2;desc.invoke=Service;desc.context=&b;
    APH_PluginService handle=0;CHECK(api.publish(provider.owner(),&desc,&handle)==APH_OK&&handle);
    APH_ServiceQuery query{sizeof(query),1,"test.provider","math",2,0};APH_PluginService found=9;
    CHECK(api.find(consumer.owner(),&query,&found)==APH_E_UNAVAILABLE&&found==0);
    CHECK(hub::finish_enable(provider.owner(),APH_OK)==APH_OK);
    CHECK(api.find(consumer.owner(),&query,&found)==APH_OK&&found==handle);
    CHECK(api.publish(provider.owner(),&desc,&found)==APH_E_DUPLICATE&&found==0);
    query.min_minor=3;CHECK(api.find(consumer.owner(),&query,&found)==APH_E_VERSION);query.min_minor=2;
    query.major=2;CHECK(api.find(consumer.owner(),&query,&found)==APH_E_VERSION);query.major=1;
    int in=12,out=0;auto epoch=hub::owner_epoch(consumer.owner());
    CHECK(api.invoke(consumer.owner(),epoch,handle,0,&in,4,&out,4)==APH_OK&&out==19);
    CHECK(api.invoke(consumer.owner(),epoch,handle,0,&in,4097,&out,4)==APH_E_ARGUMENT);
    CHECK(api.invoke(consumer.owner(),epoch+1,handle,0,&in,4,&out,4)==APH_E_REVOKED);
    std::thread call([&]{int value=0;CHECK(api.invoke(consumer.owner(),epoch,handle,1,&in,4,&value,4)==APH_OK&&value==19);});
    CHECK(WaitForSingleObject(b.entered,5000)==WAIT_OBJECT_0);
    CHECK(provider.Disable()==APH_PENDING);CHECK(api.publish(provider.owner(),&desc,&found)==APH_E_REVOKED);
    CHECK(api.invoke(consumer.owner(),epoch,handle,0,&in,4,&out,4)==APH_E_STALE);
    CHECK(provider.Checkpoint()==APH_PENDING&&restores==0);
    SetEvent(b.release);call.join();Disabled(provider);CHECK(restores==1);
    CHECK(hub::begin_enable(provider.owner()));desc.activation_epoch=hub::owner_epoch(provider.owner());
    APH_PluginService renewed=0;CHECK(api.publish(provider.owner(),&desc,&renewed)==APH_OK&&renewed!=handle);
    CHECK(hub::finish_enable(provider.owner(),APH_OK)==APH_OK);
    CHECK(api.invoke(consumer.owner(),epoch,handle,0,&in,4,&out,4)==APH_E_STALE);
    CHECK(api.invoke(consumer.owner(),epoch,renewed,0,&in,4,&out,4)==APH_OK);
    ResetEvent(b.entered);ResetEvent(b.release);
    std::thread withdraw([&]{int value=0;CHECK(api.invoke(consumer.owner(),epoch,renewed,1,&in,4,&value,4)==APH_OK);});
    CHECK(WaitForSingleObject(b.entered,5000)==WAIT_OBJECT_0);
    CHECK(api.withdraw(provider.owner(),renewed)==APH_PENDING);
    CHECK(api.find(consumer.owner(),&query,&found)==APH_E_UNAVAILABLE);
    SetEvent(b.release);withdraw.join();CHECK(api.withdraw(provider.owner(),renewed)==APH_OK);
    for(unsigned i=0;i<260;++i){CHECK(api.publish(provider.owner(),&desc,&renewed)==APH_OK);CHECK(api.withdraw(provider.owner(),renewed)==APH_OK);}
    desc.thread_policy=APH_SERVICE_GAME_THREAD;CHECK(api.publish(provider.owner(),&desc,&renewed)==APH_OK);
    CHECK(api.invoke(consumer.owner(),epoch,renewed,0,&in,4,&out,4)==APH_E_UNAVAILABLE);
    APH_RuntimeApi runtime{};CHECK(provider.Service(runtime)==APH_OK);CHECK(runtime.claim_source(provider.owner())==APH_OK);
    APH_RuntimeUpdate live{sizeof(live),APH_RUNTIME_SET_LIVE,0,GetCurrentThreadId(),0};
    CHECK(runtime.update(provider.owner(),&live)==APH_OK);
    CHECK(api.invoke(consumer.owner(),epoch,renewed,0,&in,4,&out,4)==APH_OK);
    std::thread wrong([&]{int result=0;CHECK(api.invoke(consumer.owner(),epoch,renewed,0,&in,4,&result,4)==APH_E_THREAD);});wrong.join();
    CHECK(api.withdraw(provider.owner(),renewed)==APH_OK);
    desc.thread_policy=0;desc.recovery=APH_RECOVERY_READ_ONLY;CHECK(api.publish(provider.owner(),&desc,&renewed)==APH_OK);
    out=555;CHECK(api.invoke(consumer.owner(),epoch,renewed,2,&in,4,&out,4)==APH_E_FAULT&&out==555);
    APH_ErrorDetail error{};CHECK(diagnostics.last_error(provider.owner(),sizeof(error),&error)==APH_OK);
    CHECK(error.result==APH_E_FAULT&&!strcmp(error.resource,"math"));
    APH_OwnerStatus status{};provider.Status(status);CHECK(APH_OwnerHasFault(status.state));
    CHECK(api.invoke(consumer.owner(),epoch,renewed,0,&in,4,&out,4)==APH_E_STALE);
    CloseHandle(b.entered);CloseHandle(b.release);
    puts("PASS services: versions, bounded POD, deterministic disable/withdraw drainage, epochs, reuse, thread policy and SEH");
}
static std::string Read(const wchar_t* path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void Logs() {
    wchar_t path[MAX_PATH];swprintf_s(path,L"foundation-log-%lu.txt",GetCurrentProcessId());
    {BoundedLog log;CHECK(log.open(path,256));log.write("previous process evidence");}
    {BoundedLog log;CHECK(log.open(path,256));wchar_t old[MAX_PATH];swprintf_s(old,L"%s.1",path);CHECK(Read(old).find("previous process evidence")!=std::string::npos);
        for(unsigned i=0;i<80;++i){char message[128];sprintf_s(message,"record %u complete",i);log.write(message);}}
    CHECK(Read(path).find("record 79 complete")!=std::string::npos);
    for(unsigned i=0;i<4;++i){wchar_t file[MAX_PATH];if(i)swprintf_s(file,L"%s.%u",path,i);else wcscpy_s(file,path);
        auto data=Read(file);CHECK(!data.empty()&&data.size()<=256&&data.back()=='\n'&&data.find('\0')==std::string::npos);}
    const auto before=Read(path);
    HANDLE hold=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);CHECK(hold!=INVALID_HANDLE_VALUE);
    {BoundedLog log;CHECK(!log.open(path,256));log.write("must not truncate a locked previous file");}
    CHECK(Read(path)==before);CloseHandle(hold);
    for(unsigned i=0;i<4;++i){wchar_t file[MAX_PATH];if(i)swprintf_s(file,L"%s.%u",path,i);else wcscpy_s(file,path);CHECK(DeleteFileW(file)||GetLastError()==ERROR_FILE_NOT_FOUND);}
    puts("PASS logs: previous startup retained, bounded rotation and locked-file failure preserves evidence");
}
static bool corlibReady,threadsReady,alreadyAttached;
static int domainCalls,attachCalls,detachCalls,unityCalls,originalCalls;
static DWORD expectedThread;
static void* Corlib(){return corlibReady?(void*)1:nullptr;}
static void** Threads(size_t* count){*count=threadsReady?1:0;return (void**)1;}
static void* Domain(){++domainCalls;return (void*)2;}
static void* Attach(void*){++attachCalls;return (void*)3;}
static void Detach(void*){++detachCalls;}
static void* Current(){return alreadyAttached?(void*)3:nullptr;}
static void* String(const char*){CHECK(GetCurrentThreadId()==expectedThread);return (void*)4;}
static void* Invoke(void* method,void*,void**,void** exception){CHECK(GetCurrentThreadId()==expectedThread);++unityCalls;*exception=nullptr;return method==(void*)1?(void*)5:nullptr;}
static uint32_t NewHandle(void*,int){CHECK(GetCurrentThreadId()==expectedThread);return 1;}
static void* Target(uint32_t){CHECK(GetCurrentThreadId()==expectedThread);return (void*)5;}
__declspec(noinline) static void TextTarget(void*,void*,void*){InterlockedIncrement((volatile LONG*)&originalCalls);}
static void Uid() {
    applepie::Il2CppBootstrap b{Corlib,Threads,Domain,Attach,Detach,Current};void* own=nullptr;
    CHECK(!b.Ready()&&!b.AttachWorker(&own)&&!domainCalls&&!attachCalls);
    corlibReady=true;CHECK(!b.Ready()&&!b.AttachWorker(&own)&&!domainCalls);
    threadsReady=true;CHECK(b.Ready()&&b.AttachWorker(&own)==(void*)2&&own==(void*)3);b.DetachWorker(own);
    CHECK(attachCalls==1&&detachCalls==1);alreadyAttached=true;
    CHECK(b.AttachWorker(&own)==(void*)2&&!own);b.DetachWorker(own);CHECK(attachCalls==1&&detachCalls==1);
    CHECK(InitializeUidOwner());expectedThread=GetCurrentThreadId();g_uidGameThread=expectedThread;
    p_il2cpp_string_new=String;p_il2cpp_runtime_invoke=Invoke;p_il2cpp_gchandle_new=NewHandle;p_il2cpp_gchandle_get_target=Target;
    g_methodGameObjectFind=(void*)1;g_methodGameObjectSetActive=(void*)2;
    APH_NativeProc original=nullptr;
    CHECK(PrepareUidHook((void*)TextTarget,(APH_NativeProc)hkTMPSetText,1,"win64:void(void*,Il2CppString*,MethodInfo*)",&g_setTextClaim,&original));
    g_origSetText=(tTMPSetText)original;
    CHECK(g_uidHooks.activate_hook(g_uidOwner,g_setTextClaim,original)>=0&&WaitUidHook(g_setTextClaim,APH_HOOK_ACTIVE));
    CHECK(hub::finish_enable(g_uidOwner,APH_OK)==APH_OK);
    auto volatile call=(tTMPSetText)TextTarget;
    std::thread overlay([&]{SetUidVisible(false);SetPingVisible(true);call(nullptr,nullptr,nullptr);});overlay.join();
    CHECK(!unityCalls&&originalCalls==1);
    call(nullptr,nullptr,nullptr);CHECK(unityCalls==4&&g_uidLocated&&originalCalls==2);
    const int before=domainCalls;
    for(unsigned i=0;i<1000;++i)call(nullptr,nullptr,nullptr);
    CHECK(unityCalls==4&&domainCalls==before&&originalCalls==1002);
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr),stopped=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread worker([&]{AcquireSRWLockShared(&g_uidVmLock);g_uidVmHeld=true;SetEvent(entered);CHECK(WaitForSingleObject(release,5000)==WAIT_OBJECT_0);g_uidVmHeld=false;ReleaseSRWLockShared(&g_uidVmLock);});
    CHECK(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    std::thread stopping([&]{StopUidRuntime();SetEvent(stopped);});Await([]{return g_hookDisabled.load();});
    CHECK(WaitForSingleObject(stopped,0)==WAIT_TIMEOUT);SetEvent(release);worker.join();stopping.join();
    SetUidVisible(true);call(nullptr,nullptr,nullptr);CHECK(unityCalls==4&&originalCalls==1003);
    CloseHandle(entered);CloseHandle(release);CloseHandle(stopped);
    puts("PASS UID: cold VM probes, owned attach/detach, actual text entry runs Unity work only on verified thread, stop drains worker");
}
static volatile LONG stopOriginals;
__declspec(noinline) static void ShutdownTarget(){CHECK(g_hookDisabled);InterlockedIncrement(&stopOriginals);}
__declspec(noinline) static void QuitTarget(void* method){CHECK(g_hookDisabled&&method==(void*)123);InterlockedIncrement(&stopOriginals);}
static void UidStop() {
    CHECK(InitializeUidOwner());
    CHECK(UidShutdown::Install((APH_NativeProc)ShutdownTarget,"void() C-export","il2cpp.runtime.shutdown"));
    CHECK(UidApplicationQuit::Install((APH_NativeProc)QuitTarget,"void(MethodInfo*) static","unity.Application.Internal_ApplicationQuit"));
    // No Active transition or VM access is required for module stop observation.
    auto volatile quit=(void(*)(void*))QuitTarget;quit((void*)123);
    CHECK(g_hookDisabled&&!UidRuntimeOpen()&&stopOriginals==1);
    auto volatile shutdown=(void(*)())ShutdownTarget;shutdown();CHECK(stopOriginals==2);
    puts("PASS UID stop: shared quit/shutdown close guard before original while initialization is pending; original exactly once");
}
static unsigned Plugin(const char* id){for(unsigned i=0;i<loader::count();++i){loader::Plugin p{};loader::snapshot(i,&p);if(!strcmp(p.manifest.id,id))return i;}CHECK(false);return 0;}
static loader::Plugin State(const char* id){loader::Plugin p{};CHECK(loader::snapshot(Plugin(id),&p));return p;}
static void Loader(bool delayed) {
    wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);*wcsrchr(path,L'\\')=0;
    wchar_t config[MAX_PATH];swprintf_s(config,L"%s\\plugin\\applepie_manager_config.txt",path);
    FILE* f=nullptr;CHECK(!_wfopen_s(&f,config,L"wb"));fprintf(f,"[plugins]\nfixture8.dll=%d\nfixture11.dll=0\n",delayed?0:1);fclose(f);
    CHECK(APH_Bootstrap(1)>=0);
    Await([]{APH_BootStatus b{};return APH_GetBootStatus(sizeof(b),&b)==APH_OK&&b.state==APH_SCANNED;});
    Await([]{return State("fixture.9").state==loader::Active;});
    Await([]{return State("example.service.consumer").state==loader::Active;});
    auto compute=(APH_Result(APH_CALL*)(int32_t,int32_t*))GetProcAddress(State("example.service.consumer").module,"APH_ExampleCompute");
    int32_t computed=0;CHECK(compute&&compute(21,&computed)==APH_OK&&computed==42);
    CHECK(State("fixture.2").state==loader::Active);
    CHECK(State("fixture.3").state==loader::WaitingDependencies&&!State("fixture.3").loaded);
    CHECK(State("fixture.4").result==APH_E_VERSION&&!State("fixture.4").loaded);
    CHECK(State("fixture.5").result==APH_E_CYCLE&&!State("fixture.5").loaded);
    CHECK(State("fixture.6").result==APH_E_CYCLE&&!State("fixture.6").loaded);
    CHECK(State("fixture.7").state==loader::Rejected&&!State("fixture.7").loaded);
    if(delayed) {
        CHECK(State("fixture.1").state==loader::WaitingDependencies&&!State("fixture.1").loaded);
        CHECK(State("fixture.8").state==loader::Disabled&&!State("fixture.8").loaded);
        Await([&]{return loader::request_enabled(Plugin("fixture.8"),true)==APH_PENDING;});
    }
    Await([]{return State("fixture.1").state==loader::Active;});
    Await([]{return State("fixture.10").state==loader::Enabling;});
    CHECK(State("fixture.8").state==loader::Active);
    CHECK(loader::request_enabled(Plugin("fixture.8"),false)==APH_E_BUSY);
    CHECK(strstr(State("fixture.8").detail,"fixture.1"));
    CHECK(loader::request_enabled(Plugin("fixture.1"),false)==APH_PENDING);
    Await([]{return State("fixture.1").state==loader::Disabled;});
    CHECK(loader::request_enabled(Plugin("fixture.10"),false)==APH_PENDING);
    Await([]{return State("fixture.10").state==loader::Disabled;});
    CHECK(loader::request_enabled(Plugin("fixture.8"),false)==APH_PENDING);
    Await([]{return State("fixture.8").state==loader::Disabled;});
    CHECK(loader::request_enabled(Plugin("fixture.8"),true)==APH_PENDING);
    Await([]{return State("fixture.8").state==loader::Active;});
    CHECK(loader::request_enabled(Plugin("fixture.1"),true)==APH_PENDING);
    Await([]{return State("fixture.1").state==loader::Active;});
    CHECK(loader::request_enabled(Plugin("fixture.10"),true)==APH_PENDING);
    Await([]{return State("fixture.10").state==loader::Enabling;});
    CHECK(hub::services::lifecycle_api()->disable(State("fixture.8").owner)==APH_PENDING);
    Await([]{return State("fixture.1").state==loader::Disabled;});
    Await([]{return State("fixture.10").state==loader::Disabled;});
    CHECK(hub::services::lifecycle_api()->disable(State("example.service.provider").owner)==APH_PENDING);
    CHECK(compute(21,&computed)<0);
    Await([]{return State("example.service.consumer").state==loader::Disabled;});
    puts("PASS loader: legacy manifest, required/optional/version/cycle checks, no premature DLL execution, dependency stop order and provider loss");
}
static void AttachLoss() {
    wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);*wcsrchr(path,L'\\')=0;
    wchar_t config[MAX_PATH];swprintf_s(config,L"%s\\plugin\\applepie_manager_config.txt",path);
    FILE* f=nullptr;CHECK(!_wfopen_s(&f,config,L"wb"));fprintf(f,"[plugins]\n");
    for(unsigned i=1;i<=11;++i)fprintf(f,"fixture%u.dll=%u\n",i,i==8||i==11?1:0);fclose(f);
    CHECK(APH_Bootstrap(1)>=0);
    Await([]{APH_BootStatus b{};return APH_GetBootStatus(sizeof(b),&b)==APH_OK&&b.state==APH_SCANNED;});
    Await([]{return State("fixture.8").state==loader::Disabled;});
    auto waiting=State("fixture.11");APH_OwnerStatus status{};
    CHECK(waiting.loaded&&waiting.attached&&waiting.state==loader::WaitingDependencies);
    CHECK(hub::api()->owner_status(waiting.owner,sizeof(status),&status)==APH_OK&&status.state==APH_OWNER_WAITING&&!status.activation_epoch);
    Await([]{return loader::request_enabled(Plugin("fixture.8"),true)==APH_PENDING;});
    Await([]{return State("fixture.11").state==loader::Active;});
    puts("PASS loader attach race: provider lost during Attach, unstarted dependent waits and resumes when provider recovers");
}
int main(int argc,char** argv) {
    if(argc>1&&!strcmp(argv[1],"loader-attach-loss"))AttachLoss();
    else if(argc>1&&!strncmp(argv[1],"loader",6))Loader(!strcmp(argv[1],"loader-delayed"));
    else {CHECK(hub::initialize()==APH_OK);if(argc>1&&!strcmp(argv[1],"uid"))Uid();else if(argc>1&&!strcmp(argv[1],"uid-stop"))UidStop();else if(argc>1&&!strcmp(argv[1],"logs"))Logs();else Services();}
    printf("PASS foundation checks: %ld assertions\n",checks);return 0;
}
