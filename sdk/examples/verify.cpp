// Offline integration checks using the production Core + MinHook backend.
// No APH_TESTING seams, live game, deployment, GUI or injected process.
#define NOMINMAX
#pragma pack(push, 1)
#include "../applepie_endfield_adapters.h"
struct CallerPacking { char first; void* second; };
static_assert(sizeof(CallerPacking) == 9, "SDK must restore caller packing");
static_assert(alignof(applepie::Hook) == 8 && alignof(APH_NativeBinding) == 8, "SDK context alignment");
static_assert(sizeof(APH_EfInputBinding) == 48 && alignof(APH_EfInputBinding) == 8, "input payload ABI");
#pragma pack(pop)
#include "../../src/hub_core.h"
#include "MinHook.h"
#include <cstdio>
#include <thread>

static volatile LONG checks = 0;
#define CHECK(x) do { InterlockedIncrement(&checks); if (!(x)) { \
    printf("FAIL line %d: %s\n", __LINE__, #x); fflush(stdout); ExitProcess(10); } } while (0)
template<class F> static void Await(F work, const char* label) {
    const ULONGLONG deadline = GetTickCount64() + 5000;
    for (;;) {
        const APH_Result r = work();
        if (r == APH_OK) return;
        if ((r != APH_PENDING && r != APH_E_BUSY) || GetTickCount64() >= deadline) {
            printf("FAIL waiting for %s: %d\n", label, r); fflush(stdout); ExitProcess(11);
        }
        Sleep(1);
    }
}
static APH_Result APH_CALL Restore(APH_Owner, const APH_Cleanup*, void*) { return APH_OK; }
static void Add(applepie::Client& instance, const char* id, bool active = true,
    const APH_LifecycleDesc* customLife = nullptr) {
    APH_Manifest m{sizeof(m), APH_ABI_MAJOR, 0, APH_ARCH_X64,
        APH_PLUGIN_LIFECYCLE | APH_PLUGIN_LATE_LOAD, APH_CAP_CORE | APH_CAP_HOOKS, 0};
    strcpy_s(m.id, id); strcpy_s(m.display_name, id);
    const APH_Owner owner = hub::add_owner(m, GetModuleHandleW(nullptr));
    CHECK(owner != 0);
    const APH_AttachArgs args{sizeof(args), 0, owner, hub::api()};
    CHECK(instance.Attach(&args) == APH_OK);
    CHECK(instance.Attach(&args) == APH_OK);
    const APH_LifecycleDesc life{sizeof(life), 10000, nullptr, Restore, nullptr};
    CHECK(instance.RegisterLifecycle(customLife ? *customLife : life) == APH_OK);
    if (active) { CHECK(hub::begin_enable(owner) != 0); hub::finish_enable(owner, APH_OK); }
}
static void State(const applepie::Client& instance, uint32_t expected) {
    Await([&] {
        APH_OwnerStatus s{};
        APH_Result r = instance.Status(s);
        if (r != APH_OK) return r;
        if (s.state == expected) return (APH_Result)APH_OK;
        return APH_OwnerHasFault(s.state) ? s.result : (APH_Result)APH_PENDING;
    }, "owner state");
}
static void Enable(const applepie::Client& instance) {
    CHECK(hub::begin_enable(instance.owner()) != 0);
    hub::finish_enable(instance.owner(), APH_OK);
}

struct CancelProbe {
    HANDLE entered, resume, notified;
    volatile LONG enableCalls, notifications, restores, allowRestore;
    APH_Result returnValue;
};
static CancelProbe cancelProbe{}, timeoutProbe{};
static APH_Result APH_CALL CancelRestore(APH_Owner, const APH_Cleanup* cleanup, void* context) {
    auto* p = (CancelProbe*)context;
    CHECK(cleanup->reason == APH_CLEANUP_DISABLE && !cleanup->domains);
    InterlockedIncrement(&p->restores);
    return InterlockedCompareExchange(&p->allowRestore, 0, 0) ? APH_OK : APH_PENDING;
}
static void APH_CALL CancelNotice(APH_Owner, uint64_t, void* context) {
    auto* p = (CancelProbe*)context;
    InterlockedIncrement(&p->notifications); SetEvent(p->notified);
}
static APH_Result APH_CALL BlockingEnable(APH_Owner, uint64_t) {
    InterlockedIncrement(&cancelProbe.enableCalls); SetEvent(cancelProbe.entered);
    CHECK(WaitForSingleObject(cancelProbe.resume, 5000) == WAIT_OBJECT_0);
    return cancelProbe.returnValue;
}
static APH_Result APH_CALL EarlyComplete(APH_Owner owner, uint64_t epoch) {
    APH_LifecycleApi life{};
    CHECK(hub::api()->copy_service(owner, APH_SERVICE_LIFECYCLE, 1, sizeof(life), &life) == APH_OK);
    CHECK(life.complete_enable(owner, epoch, APH_OK) == APH_OK);
    return APH_PENDING;
}
static void ResetProbe(CancelProbe& p) {
    if (!p.entered) {
        p.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        p.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        p.notified = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        CHECK(p.entered && p.resume && p.notified);
    }
    ResetEvent(p.entered); ResetEvent(p.resume); ResetEvent(p.notified);
    p.enableCalls = p.notifications = p.restores = 0; p.allowRestore = 1;
}
using CancelHook = applepie::NativeHook<990, int(*)(int)>;
static __declspec(noinline) int CancelTarget(int n) { return n + 37; }
// The target is patched at runtime; do not let LTCG constant-fold direct calls.
static int (*volatile callCancelTarget)(int) = CancelTarget;
static int CancelBusiness(int n) { return CancelHook::Original(n) + 100; }
static void CancellationChecks() {
    ResetProbe(cancelProbe);
    applepie::Client client;
    const APH_LifecycleDesc life{sizeof(life), 10000, CancelNotice, CancelRestore, &cancelProbe};
    Add(client, "sdk.cancel", false, &life);
    const APH_Owner owner = client.owner();
    APH_OwnerStatus status{};

    // Cancellation wins before callback admission. A partially prepared native
    // claim remains resident and can be reused on the next epoch.
    uint64_t oldEpoch = hub::begin_enable(owner); CHECK(oldEpoch);
    CHECK(CancelHook::Request(client, 1, CancelTarget, CancelBusiness,
        "i32(i32)", "cancel.target") == APH_PENDING);
    Await([] { return CancelHook::Advance(); }, "cancel hook ready");
    CHECK(callCancelTarget(1) == 38);
    CHECK(client.Status(status) == APH_OK);
    const uint32_t resources = status.resource_count;
    CHECK(resources != 0 && client.Disable() == APH_PENDING);
    CHECK(hub::enable(owner, BlockingEnable, oldEpoch) == APH_E_STALE);
    State(client, APH_OWNER_DISABLED);
    CHECK(cancelProbe.enableCalls == 0 && cancelProbe.notifications == 1);
    CHECK(client.Disable() == APH_OK && !hub::admit_owner(owner, oldEpoch));
    CHECK(client.CompleteEnable(oldEpoch, APH_OK) == APH_E_STALE);
    CHECK(client.CompleteEnable(oldEpoch, APH_E_UNAVAILABLE) == APH_E_STALE);
    CHECK(callCancelTarget(1) == 38);

    // Event barriers hold Enable on the stack while cancellation is delivered.
    // No restore may run until that callback really returns, whatever it returns.
    for (const APH_Result returned : {APH_OK, APH_PENDING, APH_E_UNAVAILABLE}) {
        ResetProbe(cancelProbe); cancelProbe.returnValue = returned;
        const uint64_t epoch = hub::begin_enable(owner); CHECK(epoch > oldEpoch);
        APH_Result finish = APH_OK;
        std::thread worker([&] {
            const APH_Result result = hub::enable(owner, BlockingEnable, epoch);
            CHECK(result == returned);
            finish = hub::finish_enable(owner, result, epoch);
        });
        CHECK(WaitForSingleObject(cancelProbe.entered, 5000) == WAIT_OBJECT_0);
        CHECK(client.Disable() == APH_PENDING);
        CHECK(WaitForSingleObject(cancelProbe.notified, 5000) == WAIT_OBJECT_0);
        CHECK(client.Disable() == APH_PENDING && client.Checkpoint() == APH_PENDING);
        CHECK(client.Status(status) == APH_OK && status.state == APH_OWNER_DISABLING);
        CHECK(cancelProbe.restores == 0 && hub::owner_business_flights(owner) != 0);
        CHECK(hub::begin_enable(owner) == 0);
        CHECK(client.CompleteEnable(epoch, APH_OK) == APH_E_STALE);
        CHECK(client.CompleteEnable(epoch, APH_E_UNAVAILABLE) == APH_E_STALE);
        SetEvent(cancelProbe.resume); worker.join(); CHECK(finish == APH_E_STALE);
        State(client, APH_OWNER_DISABLED);
        CHECK(cancelProbe.notifications == 1 && cancelProbe.restores > 0);
        CHECK(client.Status(status) == APH_OK && status.resource_count == resources);
        oldEpoch = epoch;
    }

    // A new epoch cannot be completed, failed or invoked by the previous one.
    ResetProbe(cancelProbe);
    uint64_t epoch = hub::begin_enable(owner); CHECK(epoch > oldEpoch);
    for (const APH_Result returned : {APH_OK, APH_PENDING, APH_E_UNAVAILABLE})
        CHECK(hub::finish_enable(owner, returned, oldEpoch) == APH_E_STALE);
    CHECK(client.CompleteEnable(oldEpoch, APH_OK) == APH_E_STALE);
    CHECK(client.CompleteEnable(oldEpoch, APH_E_UNAVAILABLE) == APH_E_STALE);
    CHECK(hub::enable(owner, BlockingEnable, oldEpoch) == APH_E_STALE);
    CHECK(client.Status(status) == APH_OK && status.state == APH_OWNER_ENABLING && status.activation_epoch == epoch);
    CHECK(CancelHook::Request(client, 1, CancelTarget, CancelBusiness,
        "i32(i32)", "cancel.target") == APH_PENDING);
    CHECK(CancelHook::Advance() == APH_OK);
    CHECK(hub::enable(owner, EarlyComplete, epoch) == APH_PENDING);
    CHECK(hub::finish_enable(owner, APH_PENDING, epoch) == APH_E_STALE);
    State(client, APH_OWNER_ACTIVE); CHECK(callCancelTarget(1) == 138);
    CHECK(client.Disable() == APH_PENDING); State(client, APH_OWNER_DISABLED);

    // Simultaneous success/cancellation: either completion linearizes first,
    // but neither order can reopen the gate after Disable has been accepted.
    HANDLE start = CreateEventW(nullptr, TRUE, FALSE, nullptr); CHECK(start);
    for (unsigned i = 0; i < 16; ++i) {
        ResetProbe(cancelProbe); cancelProbe.allowRestore = 0; ResetEvent(start);
        epoch = hub::begin_enable(owner); CHECK(epoch);
        APH_Result completed = APH_PENDING, cancelled = APH_OK;
        std::thread completion([&] {
            CHECK(WaitForSingleObject(start, 5000) == WAIT_OBJECT_0);
            completed = client.CompleteEnable(epoch, APH_OK);
        });
        std::thread cancellation([&] {
            CHECK(WaitForSingleObject(start, 5000) == WAIT_OBJECT_0);
            cancelled = client.Disable();
        });
        SetEvent(start); completion.join(); cancellation.join();
        CHECK((completed == APH_OK || completed == APH_E_STALE) && cancelled == APH_PENDING);
        CHECK(client.Status(status) == APH_OK && status.state == APH_OWNER_DISABLING);
        CHECK(!hub::admit_owner(owner, epoch) && callCancelTarget(1) == 38);
        CHECK(client.CompleteEnable(epoch, APH_E_UNAVAILABLE) == APH_E_STALE);
        InterlockedExchange(&cancelProbe.allowRestore, 1); State(client, APH_OWNER_DISABLED);
        CHECK(cancelProbe.notifications == 1);
    }
    CloseHandle(start);

    // A late Pending/OK/error must not clear or restart the cleanup deadline.
    ResetProbe(timeoutProbe); timeoutProbe.allowRestore = 0;
    applepie::Client timed;
    const APH_LifecycleDesc timedLife{sizeof(timedLife), 100, CancelNotice, CancelRestore, &timeoutProbe};
    Add(timed, "sdk.cancel-timeout", false, &timedLife);
    epoch = hub::begin_enable(timed.owner()); CHECK(epoch);
    CHECK(timed.Disable() == APH_PENDING);
    CHECK(WaitForSingleObject(timeoutProbe.notified, 5000) == WAIT_OBJECT_0);
    for (const APH_Result returned : {APH_OK, APH_PENDING, APH_E_UNAVAILABLE})
        CHECK(hub::finish_enable(timed.owner(), returned, epoch) == APH_E_STALE);
    State(timed, APH_OWNER_NEEDS_RESTART);
    CHECK(timeoutProbe.notifications == 1 && !hub::begin_enable(timed.owner()));
    CHECK(timed.CompleteEnable(epoch, APH_OK) == APH_E_STALE);
    printf("PASS enable cancellation: pre-entry rejection; admitted callback drains; epoch isolation; 16 completion races; retained claims; cleanup timeout\n");
}

static volatile LONG native_originals;
using NumberHook = applepie::NativeHook<101, int(*)(int)>;
static __declspec(noinline) int Number(int n) { InterlockedIncrement(&native_originals); return n + 2; }
static int NumberBusiness(int n) { return NumberHook::Original(n) * 3; }
static int WrongBusiness(int n) { return n; }
static applepie::Client numbers, second, sharedA, sharedB, types, blocked, faults, slotOwner;
static applepie::Hook rawHook;
static APH_NativeProc rawOriginal;
using RawFunction = int(*)(int, float, const int*);
static __declspec(noinline) int RawTarget(int n, float scale, const int* p) {
    InterlockedIncrement(&native_originals); return n + (int)scale + *p;
}
// A custom typed entry keeps complete control of its ABI and callback behavior.
static int RawEntry(int n, float scale, const int* p) {
    APH_HookCall call{}; unsigned phase = APH_ORIGINAL_NOT_ENTERED; int result = 0;
    const auto* api = rawHook.api();
    APH_Result entered = api->enter_call(rawHook.owner(), rawHook.handle(), &call);
    __try {
        __try {
            phase = APH_ORIGINAL_RUNNING;
            result = ((RawFunction)rawOriginal)(n, scale, p);
            phase = APH_ORIGINAL_RETURNED;
            if (entered == APH_OK && (call.flags & APH_CALL_BUSINESS)) result += 7;
        } __except (APH_EntryFilter(api, rawHook.owner(), rawHook.handle(),
            GetExceptionInformation(), phase, APH_RECOVERY_NONE)) {}
    } __finally { if (entered == APH_OK) api->leave_call(rawHook.owner(), &call); }
    return result;
}
struct Pair { uint64_t a, b; };
using PairHook = applepie::NativeHook<102, Pair(*)(uint64_t)>;
static __declspec(noinline) Pair PairTarget(uint64_t n) { InterlockedIncrement(&native_originals); return {n, n + 1}; }
static Pair PairBusiness(uint64_t n) { Pair p = PairHook::Original(n); p.b += 9; return p; }

// Exercise client-side ABI checks, including a legitimate older Runtime prefix.
// These are service responses at the public boundary, not changes to Core.
static APH_Api bridge;
static int serviceMode;
static volatile LONG serviceCopies, clientQueries;
static APH_Result (APH_CALL *realQuery)(APH_Owner, APH_Hook, uint32_t, APH_HookStatus*);
static APH_Result APH_CALL CountQuery(APH_Owner owner, APH_Hook hook, uint32_t bytes, APH_HookStatus* out) {
    InterlockedIncrement(&clientQueries); return realQuery(owner, hook, bytes, out);
}
static APH_Result APH_CALL ServiceResponse(APH_Owner owner, uint64_t id, uint32_t version, uint32_t bytes, void* out) {
    InterlockedIncrement(&serviceCopies);
    if (serviceMode == 5) return APH_E_CAPABILITY;
    APH_Result r = hub::api()->copy_service(owner, id, version, bytes, out);
    if (r != APH_OK) return r;
    if (id == APH_SERVICE_HOOKS) {
        auto* a = (APH_HookApi*)out;
        if (serviceMode == 1) a->size = 8;
        else if (serviceMode == 2) a->abi_major = 99;
        else if (serviceMode == 3) a->enter_call = nullptr;
        else if (serviceMode == 4) ++a->runtime_instance;
        else { realQuery = a->query_hook; a->query_hook = CountQuery; }
    } else if (id == APH_SERVICE_RUNTIME && serviceMode == 6) {
        auto* a = (APH_RuntimeApi*)out; a->size = APH_RUNTIME_API_V1_PREFIX_BYTES; a->probe_open = nullptr;
    }
    return r;
}
using ProbeHook = applepie::NativeHook<104, int(*)(int)>;
static __declspec(noinline) int ProbeTarget(int n) { InterlockedIncrement(&native_originals); return n - 1; }
static int ProbeBusiness(int n) { return ProbeHook::Original(n); }
static applepie::Client serviceOwner, serviceClient;
static void ServiceChecks() {
    Add(serviceOwner, "sdk.services");
    bridge = *hub::api(); bridge.copy_service = ServiceResponse;
    const APH_AttachArgs args{sizeof(args), 0, serviceOwner.owner(), &bridge};
    CHECK(serviceClient.Attach(&args) == APH_OK && serviceCopies == 0);
    APH_HookApi hooks{}; hooks.size = 17;
    for (serviceMode = 1; serviceMode <= 4; ++serviceMode)
        CHECK(serviceClient.Service(hooks) == APH_E_ABI && hooks.size == 17);
    CHECK(serviceClient.Service(hooks) == APH_E_CAPABILITY && hooks.size == 17);
    serviceMode = 6; APH_RuntimeApi runtime{};
    CHECK(serviceClient.Service(runtime) == APH_OK && runtime.size == APH_RUNTIME_API_V1_PREFIX_BYTES && !runtime.probe_open);
    serviceMode = 0;
    applepie::NativeOptions invalid{}; invalid.domain = APH_ANIMATION;
    CHECK(ProbeHook::Request(serviceClient, 1, ProbeTarget, ProbeBusiness, "i32(i32)", "probe", invalid) == APH_E_ARGUMENT);
    CHECK(ProbeHook::Request(serviceClient, 1, ProbeTarget, ProbeBusiness, "i32(i32)", "probe") >= 0);
    Await([] { return ProbeHook::Advance(); }, "instrumented hook");
    const LONG copies = serviceCopies, queries = clientQueries;
    bool values = true;
    for (int i = 0; i < 10000; ++i) values = values && ProbeTarget(i) == i - 1;
    CHECK(values && serviceCopies == copies && clientQueries == queries);
    printf("PASS services: lazy acquisition; ABI validation; optional prefix; zero client polls/copies in 10000 native calls\n");
}

static void NativeChecks() {
    Add(numbers, "sdk.numbers"); Add(second, "sdk.second");
    APH_RuntimeApi runtime{}; APH_RuntimeSnapshot snapshot{};
    CHECK(numbers.Service(runtime) == APH_OK);
    CHECK(runtime.snapshot(sizeof(snapshot), &snapshot) == APH_OK && snapshot.state == APH_RUNTIME_WAITING);
    applepie::NativeOptions options{}; options.automatic_ready = false;
    CHECK(NumberHook::Request(numbers, 1, Number, NumberBusiness, "i32(i32)", "number", options) == APH_PENDING);
    CHECK(Number(2) == 4); // Request has not published original or activated.
    Await([] { return NumberHook::Advance(); }, "native activation");
    const LONG before = native_originals;
    CHECK(Number(2) == 4 && native_originals == before + 1); // explicit group commit
    CHECK(NumberHook::PublishReady() == APH_OK);
    CHECK(Number(2) == 12);
    CHECK(NumberHook::Request(numbers, 1, Number, WrongBusiness, "i32(i32)", "number", options) == APH_E_KEY_REUSED);
    CHECK(NumberHook::Request(numbers, 2, Number, NumberBusiness, "i32(i32)", "number", options) == APH_E_KEY_REUSED);
    CHECK(NumberHook::Request(second, 1, Number, NumberBusiness, "i32(i32)", "number", options) == APH_E_KEY_REUSED);
    using Conflict = applepie::NativeHook<103, int(*)(int)>;
    CHECK(Conflict::Request(second, 1, Number, WrongBusiness, "i32(i32)", "conflict") == APH_E_CONFLICT);
    CHECK(!Conflict::handle());
    APH_HookStatus status{}; CHECK(NumberHook::Status(status) == APH_OK && status.in_flight == 0);

    CHECK(numbers.Disable() == APH_PENDING); State(numbers, APH_OWNER_DISABLED);
    CHECK(Number(2) == 4);
    const auto originalClaim = NumberHook::handle(); Enable(numbers);
    CHECK(NumberHook::Request(numbers, 1, Number, NumberBusiness, "i32(i32)", "number", options) >= 0);
    CHECK(NumberHook::Advance() == APH_OK && NumberHook::handle() == originalClaim && Number(2) == 12);

    APH_HookRequest request{};
    CHECK(applepie::MakeHookRequest(request, 2, (APH_NativeProc)RawTarget, (APH_NativeProc)RawEntry,
        "i32(i32,f32,const i32*)", "custom ABI") == APH_OK);
    CHECK(rawHook.Request(numbers, request) >= 0);
    Await([] { return rawHook.Advance(&rawOriginal); }, "handwritten entry");
    request.native_signature[strlen(request.native_signature) + 1] = 'x';
    CHECK(rawHook.Request(numbers, request) >= 0); // C-string identity ignores unused padding.
    request.reserved = 1;
    CHECK(rawHook.Request(numbers, request) == APH_E_ARGUMENT);
    const int input = 4; CHECK(RawTarget(2, 3.0f, &input) == 16);
    APH_NativeProc wrongSlot = nullptr; CHECK(rawHook.Advance(&wrongSlot) == APH_E_ARGUMENT);
    CHECK(PairHook::Request(numbers, 3, PairTarget, PairBusiness, "pair16(u64) MSVC-sret", "pair") >= 0);
    Await([] { return PairHook::Advance(); }, "aggregate return");
    const LONG pairBefore = native_originals;
    const Pair p = PairTarget(41); CHECK(p.a == 41 && p.b == 51 && native_originals == pairBefore + 1);
    CHECK(runtime.snapshot(sizeof(snapshot), &snapshot) == APH_OK && snapshot.state == APH_RUNTIME_WAITING);
    Await([] { return NumberHook::Retire(); }, "native retirement");
    CHECK(NumberHook::Status(status) == APH_OK && status.state == APH_HOOK_RETIRED_PINNED);
    CHECK(NumberHook::Advance() == APH_E_REVOKED && Number(2) == 4);
    printf("PASS native: no game readiness; exact ABI; manual publish; conflicts; re-enable; retirement\n");
}

static volatile LONG shared_originals;
static bool recurseSetMain;
static __declspec(noinline) void SetMainTarget(void* self, void* entity, int32_t reason, void* method) {
    InterlockedIncrement(&shared_originals);
    if (recurseSetMain && reason > 0) SetMainTarget(self, entity, reason - 1, method);
}
using SetA = applepie::endfield::SetMainI32<201>;
using SetB = applepie::endfield::SetMainI32<202>;
struct Observation { unsigned before = 0, after = 0; int32_t reason = 0; void* self = nullptr; bool suppressed = false; };
static Observation observationA, observationB;
static applepie::Subscription subA, subB;
static APH_Result APH_CALL OnSetMain(APH_Owner, const APH_SharedEvent* event, void* context) {
    auto& seen = *(Observation*)context;
    if (event->phase == APH_BEFORE) ++seen.before; else ++seen.after;
    CHECK(event->payload_bytes == sizeof(APH_EfSetMain));
    const auto& payload = *(const APH_EfSetMain*)event->payload;
    seen.reason = payload.reason; seen.self = payload.self;
    seen.suppressed = event->original_suppressed != 0;
    return APH_OK;
}
static void SharedChecks() {
    Add(sharedA, "sdk.shared.a"); Add(sharedB, "sdk.shared.b");
    CHECK(SetA::Request(sharedA, 1, SetMainTarget) >= 0);
    CHECK(SetB::Request(sharedB, 1, SetMainTarget) >= 0);
    CHECK(SetA::Listen(subA, OnSetMain, &observationA) == APH_OK);
    CHECK(SetB::Listen(subB, OnSetMain, &observationB) == APH_OK);
    Await([] { return SetA::Advance(); }, "shared provider");
    Await([] { return SetB::Advance(); }, "shared member");
    APH_SharedStatus a{}, b{};
    CHECK(SetA::Status(a) == APH_OK && a.is_provider && a.original);
    CHECK(SetB::Status(b) == APH_OK && !b.is_provider && !b.original && b.native_hook == a.native_hook);
    using WrongSignature = applepie::endfield::SetMainBool<203>;
    CHECK(WrongSignature::Request(second, 99, (WrongSignature::Function)SetMainTarget) == APH_E_SIGNATURE);
    CHECK(!WrongSignature::handle());
    applepie::SharedHook wrongContract; APH_SharedRequest incompatible{};
    CHECK(applepie::MakeSharedRequest(incompatible, 100, (APH_NativeProc)SetMainTarget, nullptr,
        "void(self*,Entity*,i32,MethodInfo*)", "incompatible.contract", 1, sizeof(APH_EfSetMain)) == APH_OK);
    CHECK(wrongContract.Request(second, incompatible) == APH_E_CONTRACT && !wrongContract.handle());
    int self = 1; SetMainTarget(&self, nullptr, 7, nullptr);
    CHECK(shared_originals == 1 && observationA.before == 1 && observationA.after == 1 && observationB.before == 1);
    CHECK(observationB.reason == 7 && observationB.self == &self);
    recurseSetMain = true; SetMainTarget(&self, nullptr, 1, nullptr); recurseSetMain = false;
    CHECK(shared_originals == 3 && observationA.before == 2 && observationB.after == 2);

    CHECK(sharedA.Disable() == APH_PENDING); State(sharedA, APH_OWNER_DISABLED);
    SetMainTarget(&self, nullptr, 8, nullptr);
    CHECK(observationA.before == 2 && observationB.before == 3 && shared_originals == 4);
    CHECK(subA.Close() == APH_OK); Enable(sharedA);
    CHECK(SetA::Listen(subA, OnSetMain, &observationA) == APH_OK);
    SetMainTarget(&self, nullptr, 9, nullptr); CHECK(observationA.before == 3 && observationB.before == 4);
    Await([] { return SetA::Retire(); }, "provider membership retirement");
    SetMainTarget(&self, nullptr, 10, nullptr);
    CHECK(observationA.before == 3 && observationB.before == 5 && shared_originals == 6);
    CHECK(SetB::Status(b) == APH_OK && b.state == APH_HOOK_ACTIVE && b.members == 1);
    CHECK(subA.Close() == APH_OK);
    printf("PASS shared: one native site; recursion; provider disable/re-enable/retire preserves member\n");
}

static __declspec(noinline) void BoolTarget(void*, void*, bool, void*) { InterlockedIncrement(&shared_originals); }
static __declspec(noinline) void IkTarget(void*, void*) { InterlockedIncrement(&shared_originals); }
static __declspec(noinline) void UpdateTarget(void*, float, void*) { InterlockedIncrement(&shared_originals); }
struct JobResult { uint64_t value, tag; };
static __declspec(noinline) void* JobTarget(void* result, void*, uint32_t count, void* dependency, void*) {
    InterlockedIncrement(&shared_originals);
    auto* data = (JobResult*)result; data->value = count + *(uint64_t*)dependency; data->tag = 99;
    return result;
}
static __declspec(noinline) void* SpecialTarget(void* result, void*, uint32_t count, void* dependency, void*) {
    InterlockedIncrement(&shared_originals);
    auto* data = (JobResult*)result; data->value = count + *(uint64_t*)dependency; data->tag = 100;
    return dependency; // Return register must not be replaced with the sret buffer.
}
using Bool = applepie::endfield::SetMainBool<210>;
using Ik = applepie::endfield::BipedIK<211>;
using Update = applepie::endfield::SMCUpdate<212>;
using Job = applepie::endfield::MorphJob<213>;
using Special = applepie::endfield::SpecialMorphJob<214>;
static applepie::Subscription boolSub, ikSub, updateSub, jobSub, specialSub, controllerConflict;
static bool suppressIk = true, sawSuppressed;
static unsigned ikBefore, ikAfter, jobBefore, jobAfter;
static float seenDelta;
static APH_Result APH_CALL OnIk(APH_Owner, const APH_SharedEvent* e, void*) {
    CHECK(e->payload_bytes == sizeof(APH_EfVoid));
    if (e->phase == APH_BEFORE) { ++ikBefore; return suppressIk ? APH_SUPPRESS : APH_OK; }
    ++ikAfter; sawSuppressed = e->original_suppressed != 0; return APH_OK;
}
static APH_Result APH_CALL OnUpdate(APH_Owner, const APH_SharedEvent* e, void*) {
    CHECK(e->payload_bytes == sizeof(APH_EfUpdate));
    seenDelta = ((const APH_EfUpdate*)e->payload)->delta_time; return APH_OK;
}
static APH_Result APH_CALL OnJob(APH_Owner, const APH_SharedEvent* e, void*) {
    const auto& p = *(const APH_EfJob*)e->payload;
    CHECK(e->payload_bytes == sizeof(p) && p.count == 3 && p.dependency && p.result);
    if (e->phase == APH_BEFORE) ++jobBefore;
    else { ++jobAfter; CHECK(((const JobResult*)p.result)->value == 10); }
    return APH_OK;
}
static void AdapterChecks() {
    Add(types, "sdk.types");
    CHECK(Bool::Request(types, 1, BoolTarget) >= 0); CHECK(Bool::Listen(boolSub, OnSetMain, &observationA) == APH_OK);
    CHECK(Ik::Request(types, 2, IkTarget) >= 0); CHECK(Ik::Listen(ikSub, OnIk, nullptr, 3, APH_CONTROLLER) == APH_OK);
    CHECK(Ik::Listen(controllerConflict, OnIk, nullptr, 3, APH_CONTROLLER) == APH_E_CONFLICT);
    CHECK(!controllerConflict.handle());
    CHECK(Update::Request(types, 3, UpdateTarget) >= 0); CHECK(Update::Listen(updateSub, OnUpdate) == APH_OK);
    CHECK(Job::Request(types, 4, JobTarget) >= 0); CHECK(Job::Listen(jobSub, OnJob) == APH_OK);
    CHECK(Special::Request(types, 5, SpecialTarget) >= 0); CHECK(Special::Listen(specialSub, OnJob) == APH_OK);
    Await([] { return Bool::Advance(); }, "bool"); Await([] { return Ik::Advance(); }, "IK");
    Await([] { return Update::Advance(); }, "update"); Await([] { return Job::Advance(); }, "job");
    Await([] { return Special::Advance(); }, "special job");
    BoolTarget(nullptr, nullptr, true, nullptr); CHECK(observationA.reason == 1);
    BoolTarget(nullptr, nullptr, false, nullptr); CHECK(observationA.reason == 0);
    const LONG before = shared_originals; IkTarget(nullptr, nullptr);
    CHECK(shared_originals == before && ikBefore == 1 && ikAfter == 1 && sawSuppressed);
    suppressIk = false; IkTarget(nullptr, nullptr);
    CHECK(shared_originals == before + 1 && ikBefore == 2 && ikAfter == 2 && !sawSuppressed);
    UpdateTarget(nullptr, 0.25f, nullptr); CHECK(seenDelta == 0.25f);
    uint64_t dependency = 7; JobResult result{};
    CHECK(JobTarget(&result, nullptr, 3, &dependency, nullptr) == &result && result.tag == 99);
    CHECK(SpecialTarget(&result, nullptr, 3, &dependency, nullptr) == &dependency && result.tag == 100);
    CHECK(jobBefore == 2 && jobAfter == 2);
    printf("PASS optional adapters: i32/bool, controlled IK, float, job argument/return ABI\n");
}

struct BindingCall {
    int action, callback, method, result;
    unsigned originals = 0;
    bool fail = false;
};
struct BindingSeen { unsigned before = 0, after = 0; void* result = nullptr; };
static void* BindingBody(void* self, void* action, void* callback, int32_t priority, void* method) {
    auto& call = *(BindingCall*)self; ++call.originals;
    CHECK(action == &call.action && callback == &call.callback && method == &call.method && priority == -17);
    if (call.fail) RaiseException(0xe0417002u, 0, 0, nullptr);
    return call.result ? &call.result : nullptr;
}
// Keep distinct native bodies, rather than compiler-generated JMP thunks that
// Core correctly rejects as an unknown preexisting patch.
static volatile LONG bindingEntries[3];
static __declspec(noinline) void* BindingTargetA(void* s, void* a, void* c, int32_t p, void* m) {
    InterlockedIncrement(&bindingEntries[0]); return BindingBody(s,a,c,p,m);
}
static __declspec(noinline) void* BindingTargetB(void* s, void* a, void* c, int32_t p, void* m) {
    InterlockedIncrement(&bindingEntries[1]); return BindingBody(s,a,c,p,m);
}
static __declspec(noinline) void* BindingFaultTarget(void* s, void* a, void* c, int32_t p, void* m) {
    InterlockedIncrement(&bindingEntries[2]); return BindingBody(s,a,c,p,m);
}
static APH_Result APH_CALL OnBinding(APH_Owner, const APH_SharedEvent* e, void* context) {
    CHECK(e->payload && e->payload_bytes == sizeof(APH_EfInputBinding) && !e->original_suppressed);
    const auto& p = *(const APH_EfInputBinding*)e->payload;
    auto& call = *(BindingCall*)p.self; auto& seen = *(BindingSeen*)context;
    CHECK(p.action_id == &call.action && p.callback == &call.callback && p.method == &call.method && p.priority == -17);
    if (e->phase == APH_BEFORE) { ++seen.before; CHECK(!p.result); }
    else { ++seen.after; CHECK(p.result == (call.result ? &call.result : nullptr)); seen.result = p.result; }
    return APH_OK;
}
template<class Provider, class Member> static void BindingOrderChecks(const char* providerId, const char* memberId,
    typename Provider::Function target) {
    static applepie::Client provider, member;
    Add(provider, providerId); Add(member, memberId);
    const APH_Result requested = Provider::Request(provider, 1, target);
    if (requested < 0) printf("input request failed: %s result=%d target=%p\n", providerId, requested, (void*)target);
    CHECK(requested >= 0);
    CHECK(Member::Request(member, 1, target) >= 0);
    BindingSeen a{}, b{}; applepie::Subscription sa, sb;
    CHECK(Provider::Listen(sa, OnBinding, &a) == APH_OK && Member::Listen(sb, OnBinding, &b) == APH_OK);
    Await([] { return Provider::Advance(); }, "input provider");
    Await([] { return Member::Advance(); }, "input member");
    APH_SharedStatus pa{}, pb{};
    CHECK(Provider::Status(pa) == APH_OK && Member::Status(pb) == APH_OK);
    CHECK(pa.is_provider && !pb.is_provider && pa.original && !pb.original && pa.native_hook == pb.native_hook);
    // Merely changing an existing exclusive hook's owner cannot join this site.
    applepie::Hook exclusive; APH_HookRequest request{};
    CHECK(applepie::MakeHookRequest(request, 2, (APH_NativeProc)target, (APH_NativeProc)BindingBody,
        "ptr(self*,string*,delegate*,i32,MethodInfo*)", "incompatible exclusive") == APH_OK);
    CHECK(exclusive.Request(member, request) < 0 && !exclusive.handle());
    BindingCall call{}; call.result = 1;
    typename Provider::Function volatile invoke = target;
    CHECK(invoke(&call,&call.action,&call.callback,-17,&call.method) == &call.result);
    CHECK(call.originals == 1 && a.after == 1 && b.after == 1 && a.result == &call.result && b.result == a.result);
    call.result = 0;
    CHECK(!invoke(&call,&call.action,&call.callback,-17,&call.method));
    CHECK(call.originals == 2 && a.after == 2 && b.after == 2 && !a.result && !b.result);
    CHECK(provider.Disable() == APH_PENDING); State(provider, APH_OWNER_DISABLED);
    call.result = 1;
    CHECK(invoke(&call,&call.action,&call.callback,-17,&call.method) == &call.result);
    CHECK(call.originals == 3 && a.after == 2 && b.after == 3);
    const auto old = sa.handle(); CHECK(sa.Close() == APH_OK); Enable(provider);
    CHECK(Provider::Listen(sa, OnBinding, &a) == APH_OK && sa.handle() != old);
    CHECK(invoke(&call,&call.action,&call.callback,-17,&call.method) == &call.result);
    CHECK(call.originals == 4 && a.after == 3 && b.after == 4);
    Await([] { return Provider::Retire(); }, "input provider retirement");
    CHECK(invoke(&call,&call.action,&call.callback,-17,&call.method) == &call.result);
    CHECK(call.originals == 5 && a.after == 3 && b.after == 5 && a.before == a.after && b.before == b.after);
    CHECK(Member::Status(pb) == APH_OK && pb.state == APH_HOOK_ACTIVE && pb.members == 1);
    CHECK(sa.Close() == APH_OK && sb.Close() == APH_OK);
    Await([] { return Member::Retire(); }, "input member retirement");
}
static bool CatchBindingFault(BindingCall* call) {
    __try { BindingFaultTarget(call,&call->action,&call->callback,-17,&call->method); }
    __except (GetExceptionCode() == 0xe0417002u ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
static void InputBindingChecks() {
    using A = applepie::endfield::CreateBindingByActionId<221>;
    using B = applepie::endfield::CreateBindingByActionId<222>;
    using C = applepie::endfield::CreateBindingByActionId<223>;
    using D = applepie::endfield::CreateBindingByActionId<224>;
    BindingOrderChecks<A,B>("sdk.input.a", "sdk.input.b", BindingTargetA);
    BindingOrderChecks<D,C>("sdk.input.d", "sdk.input.c", BindingTargetB);
    static applepie::Client faultOwner;
    using F = applepie::endfield::CreateBindingByActionId<225>;
    Add(faultOwner, "sdk.input.fault");
    CHECK(F::Request(faultOwner, 1, BindingFaultTarget) >= 0);
    applepie::Subscription sub; BindingSeen seen{};
    CHECK(F::Listen(sub, OnBinding, &seen) == APH_OK);
    Await([] { return F::Advance(); }, "input fault");
    BindingCall call{}; call.fail = true;
    CHECK(CatchBindingFault(&call) && call.originals == 1 && seen.before == 1 && seen.after == 0);
    APH_SharedStatus status{}; APH_OwnerStatus owner{};
    CHECK(F::Status(status) == APH_OK && !status.in_flight && status.state == APH_HOOK_NEEDS_RESTART);
    CHECK(faultOwner.Status(owner) == APH_OK && owner.fault_phase == APH_ORIGINAL_RUNNING);
    CHECK(sub.Close() == APH_OK);
    printf("PASS input binding: both providers; original once; exact arguments/null and non-null result; disable/renew/retire; fault without AFTER or replay\n");
}

// An entirely unrelated shared contract: plugin-defined, no Endfield adapter.
static applepie::SharedHook customProvider, customMember;
static APH_NativeProc customOriginal;
static __declspec(noinline) int CustomTarget(int value) { InterlockedIncrement(&shared_originals); return value * 2; }
struct CustomPayload { int value, result; };
static int CustomEntry(int value) {
    auto* api = customProvider.api(); APH_SharedCall call{}; unsigned phase = APH_ORIGINAL_NOT_ENTERED;
    const APH_Result entered = api->enter(customProvider.owner(), customProvider.handle(), &call);
    CustomPayload p{value, 0};
    __try {
        __try {
            if (entered == APH_OK) api->publish(&call, APH_BEFORE, &p, sizeof(p));
            phase = APH_ORIGINAL_RUNNING; p.result = ((int(*)(int))customOriginal)(value); phase = APH_ORIGINAL_RETURNED;
            if (entered == APH_OK) api->publish(&call, APH_AFTER, &p, sizeof(p));
        } __except ((api->native_fault(customProvider.owner(), customProvider.handle(), GetExceptionCode(), 0, phase), EXCEPTION_CONTINUE_SEARCH)) {}
    } __finally { if (entered == APH_OK) api->leave(&call); }
    return p.result;
}
static HANDLE callbackEntered, callbackContinue;
static applepie::Subscription blockingSub;
static APH_Result APH_CALL BlockedCallback(APH_Owner, const APH_SharedEvent* e, void*) {
    CHECK(e->payload_bytes == sizeof(CustomPayload));
    if (e->phase == APH_BEFORE) { SetEvent(callbackEntered); CHECK(WaitForSingleObject(callbackContinue, 5000) == WAIT_OBJECT_0); }
    return APH_OK;
}
static void CustomSharedChecks() {
    Add(blocked, "sdk.custom"); APH_SharedRequest request{};
    CHECK(applepie::MakeSharedRequest(request, 1, (APH_NativeProc)CustomTarget, (APH_NativeProc)CustomEntry,
        "i32(i32)", "example.number-observer", 1, sizeof(CustomPayload)) == APH_OK);
    CHECK(customProvider.Request(blocked, request) >= 0);
    request.native.native_entry = nullptr;
    CHECK(customMember.Request(second, request) >= 0);
    Await([] { return customProvider.Advance(&customOriginal); }, "custom provider");
    Await([] { return customMember.Advance(); }, "custom member without entry");
    callbackEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    callbackContinue = CreateEventW(nullptr, TRUE, FALSE, nullptr); CHECK(callbackEntered && callbackContinue);
    const APH_SharedListen desc{sizeof(desc), APH_BEFORE | APH_AFTER, APH_OBSERVER, APH_RECOVERY_NONE, BlockedCallback, nullptr};
    CHECK(blockingSub.Listen(customMember, desc) == APH_OK);
    const APH_Subscription retained = blockingSub.handle();
    std::thread worker([] { CHECK(CustomTarget(7) == 14); });
    CHECK(WaitForSingleObject(callbackEntered, 5000) == WAIT_OBJECT_0);
    CHECK(blockingSub.Close() == APH_PENDING && blockingSub.handle() == retained);
    CHECK(blockingSub.Listen(customMember, desc) == APH_E_BUSY);
    SetEvent(callbackContinue); worker.join();
    CHECK(blockingSub.Close() == APH_OK && !blockingSub.handle());
    CHECK(blockingSub.Listen(customMember, desc) == APH_OK && blockingSub.handle() != retained);
    CHECK(blockingSub.Close() == APH_OK); CloseHandle(callbackEntered); CloseHandle(callbackContinue);
    printf("PASS custom shared contract: entry-free member; deterministic in-flight unsubscribe\n");
}

static void SubscriptionEpochChecks() {
    applepie::Subscription old, replacement1, replacement2;
    CHECK(SetB::Listen(old, OnSetMain, &observationB) == APH_OK);
    const APH_Subscription previous = old.handle();
    CHECK(sharedB.Disable() == APH_PENDING); State(sharedB, APH_OWNER_DISABLED); Enable(sharedB);
    // Both old records have drained. Reuse both slots with new generations.
    CHECK(SetB::Listen(replacement1, OnSetMain, &observationB) == APH_OK);
    CHECK(SetB::Listen(replacement2, OnSetMain, &observationB) == APH_OK);
    CHECK(replacement1.handle() != previous && replacement2.handle() != previous);
    CHECK(old.Close() == APH_OK && !old.handle());
    CHECK(subB.Close() == APH_OK && !subB.handle());
    CHECK(replacement1.Close() == APH_OK && replacement2.Close() == APH_OK);
    printf("PASS subscriptions: old epoch and stale generations do not close renewed listeners\n");
}

static constexpr DWORD Injected = 0xe0417001u;
using FaultHook = applepie::NativeHook<301, int(*)(int)>;
static __declspec(noinline) int OriginalFault(int value) {
    InterlockedIncrement(&native_originals); RaiseException(Injected, 0, 0, nullptr); return value;
}
static int FaultBusiness(int value) { return FaultHook::Original(value); }
static bool CatchOriginalFault() {
    __try { OriginalFault(1); }
    __except (GetExceptionCode() == Injected ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
static applepie::Client sharedFaultOwner;
using SharedFault = applepie::endfield::SMCUpdate<302>;
static __declspec(noinline) void SharedOriginalFault(void*, float, void*) {
    InterlockedIncrement(&shared_originals); RaiseException(Injected, 0, 0, nullptr);
}
static bool CatchSharedFault() {
    __try { SharedOriginalFault(nullptr, 0.5f, nullptr); }
    __except (GetExceptionCode() == Injected ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
static applepie::Client beforeDispatchOwner, afterDispatchOwner;
using BeforeDispatch = applepie::endfield::SMCUpdate<303>;
using AfterDispatch = applepie::endfield::SMCUpdate<304>;
static applepie::Subscription beforeDispatchSub, afterDispatchSub;
static __declspec(noinline) void BeforeDispatchTarget(void*, float, void*) { InterlockedIncrement(&shared_originals); }
static __declspec(noinline) void AfterDispatchTarget(void*, float, void*) { InterlockedIncrement(&shared_originals); }
static APH_Result APH_CALL RejectDispatch(APH_Owner, const APH_SharedEvent* e, void* phase) {
    return e->phase == (uint32_t)(uintptr_t)phase ? APH_E_ARGUMENT : APH_OK;
}
static bool CatchDispatchFault(void(*target)(void*, float, void*)) {
    __try { target(nullptr, 0.5f, nullptr); }
    __except (GetExceptionCode() == 0xe0411001u ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
static void FaultChecks() {
    Add(faults, "sdk.faults");
    CHECK(FaultHook::Request(faults, 1, OriginalFault, FaultBusiness, "i32(i32)", "injected original fault") >= 0);
    Await([] { return FaultHook::Advance(); }, "fault hook");
    const LONG before = native_originals; CHECK(CatchOriginalFault() && native_originals == before + 1);
    APH_OwnerStatus owner{}; APH_HookStatus hook{};
    CHECK(faults.Status(owner) == APH_OK && APH_OwnerHasFault(owner.state) && owner.fault_phase == APH_ORIGINAL_RUNNING);
    CHECK(FaultHook::Status(hook) == APH_OK && hook.in_flight == 0 && !APH_native_frame);
    Add(sharedFaultOwner, "sdk.shared.fault");
    CHECK(SharedFault::Request(sharedFaultOwner, 1, SharedOriginalFault) >= 0);
    Await([] { return SharedFault::Advance(); }, "shared fault adapter");
    const LONG sharedBefore = shared_originals; CHECK(CatchSharedFault() && shared_originals == sharedBefore + 1);
    APH_SharedStatus shared{};
    CHECK(SharedFault::Status(shared) == APH_OK && shared.in_flight == 0 && shared.state == APH_HOOK_NEEDS_RESTART);
    CHECK(sharedFaultOwner.Status(owner) == APH_OK && owner.fault_phase == APH_ORIGINAL_RUNNING);
    Add(beforeDispatchOwner, "sdk.dispatch.before"); Add(afterDispatchOwner, "sdk.dispatch.after");
    CHECK(BeforeDispatch::Request(beforeDispatchOwner, 1, BeforeDispatchTarget) >= 0);
    CHECK(AfterDispatch::Request(afterDispatchOwner, 1, AfterDispatchTarget) >= 0);
    CHECK(BeforeDispatch::Listen(beforeDispatchSub, RejectDispatch, (void*)(uintptr_t)APH_BEFORE) == APH_OK);
    CHECK(AfterDispatch::Listen(afterDispatchSub, RejectDispatch, (void*)(uintptr_t)APH_AFTER) == APH_OK);
    Await([] { return BeforeDispatch::Advance(); }, "before dispatch fault");
    Await([] { return AfterDispatch::Advance(); }, "after dispatch fault");
    const LONG dispatchBefore = shared_originals;
    CHECK(CatchDispatchFault(BeforeDispatchTarget) && shared_originals == dispatchBefore);
    CHECK(CatchDispatchFault(AfterDispatchTarget) && shared_originals == dispatchBefore + 1);
    CHECK(BeforeDispatch::Status(shared) == APH_OK && shared.in_flight == 0);
    CHECK(beforeDispatchOwner.Status(owner) == APH_OK && owner.fault_phase == APH_ORIGINAL_NOT_ENTERED);
    CHECK(AfterDispatch::Status(shared) == APH_OK && shared.in_flight == 0);
    CHECK(afterDispatchOwner.Status(owner) == APH_OK && owner.fault_phase == APH_ORIGINAL_RETURNED);
    printf("PASS native/shared faults: original exceptions propagate; no replay; counters unwind\n");
    printf("PASS shared dispatch rejection: original skipped before failure; never replayed after failure\n");
}

static __declspec(noinline) void SlotGet() { InterlockedIncrement(&native_originals); }
static __declspec(noinline) void SlotSet() { InterlockedIncrement(&shared_originals); }
static void* slot = (void*)SlotGet;
static APH_PointerApi pointers;
static APH_PointerRequest patchRequest;
static void APH_CALL SlotBody(void*) {
    CHECK(slot == (void*)SlotSet);
    APH_Patch nested = 0; CHECK(pointers.request(slotOwner.owner(), &patchRequest, &nested) == APH_E_BUSY && !nested);
    ((void(*)())slot)();
}
static void PointerChecks() {
    Add(slotOwner, "sdk.slots"); APH_RuntimeApi runtime{}; APH_RuntimeSnapshot snapshot{};
    CHECK(slotOwner.Service(runtime) == APH_OK && slotOwner.Service(pointers) == APH_OK);
    CHECK(runtime.claim_source(slotOwner.owner()) == APH_OK);
    APH_RuntimeUpdate update{sizeof(update), APH_RUNTIME_SET_LIVE, 0, GetCurrentThreadId(), 0};
    CHECK(runtime.update(slotOwner.owner(), &update) == APH_OK);
    update.operation = APH_TARGET_BIND; update.target_identity = 123;
    CHECK(runtime.update(slotOwner.owner(), &update) == APH_OK);
    CHECK(runtime.snapshot(sizeof(snapshot), &snapshot) == APH_OK);
    APH_OwnerStatus owner{}; CHECK(slotOwner.Status(owner) == APH_OK);
    patchRequest.size = sizeof(patchRequest); patchRequest.scope = APH_PATCH_TEMPORARY;
    patchRequest.slot = &slot; patchRequest.expected = (void*)SlotGet; patchRequest.replacement = (void*)SlotSet;
    patchRequest.access = {sizeof(APH_Access), 7, owner.activation_epoch, snapshot.runtime_epoch, snapshot.target_generation};
    CHECK(APH_ModuleBuild((APH_NativeProc)&slot, &patchRequest.module_timestamp, &patchRequest.module_image_size));
    APH_WriteApi writes{}; APH_WriteLease lease = 0; APH_WriteCall call{};
    CHECK(slotOwner.Service(writes) == APH_OK);
    const APH_WriteRequest writeRequest{sizeof(writeRequest), APH_ANIMATION, patchRequest.access};
    CHECK(writes.request(slotOwner.owner(), &writeRequest, &lease) == APH_OK);
    CHECK(writes.enter(slotOwner.owner(), lease, &patchRequest.access, &call) == APH_OK);
    const LONG before = shared_originals;
    __try {
        CHECK(APH_WithPointerPatch(&pointers, slotOwner.owner(), &patchRequest, SlotBody, nullptr) == APH_OK);
        CHECK(slot == (void*)SlotGet && shared_originals == before + 1);
        patchRequest.expected = (void*)RawTarget;
        CHECK(APH_WithPointerPatch(&pointers, slotOwner.owner(), &patchRequest, SlotBody, nullptr) == APH_E_EXPECTED && slot == (void*)SlotGet);
    } __finally { CHECK(writes.leave(&call) == APH_OK); }
    CHECK(writes.release(slotOwner.owner(), lease) == APH_PENDING);
    CHECK(slotOwner.Checkpoint() == APH_OK);
    printf("PASS pointer scope: expected-value check; nested claim rejection; restoration\n");
}

static void ExampleChecks() {
    wchar_t path[MAX_PATH]{};
    DWORD chars = GetModuleFileNameW(nullptr, path, MAX_PATH); CHECK(chars && chars < MAX_PATH);
    wchar_t* file = wcsrchr(path, L'\\'); CHECK(file);
    wcscpy_s(file + 1, MAX_PATH - (size_t)(file + 1 - path), L"applepie_example.dll");
    HMODULE dll = LoadLibraryW(path); CHECK(dll);
    auto* manifest = (const APH_Manifest*)GetProcAddress(dll, "APH_PluginManifest");
    auto attach = (APH_AttachFn)GetProcAddress(dll, "APH_Attach");
    auto enable = (APH_EnableFn)GetProcAddress(dll, "APH_Enable");
    auto target = (int(*)(int))GetProcAddress(dll, "APH_ExampleTarget");
    auto count = (LONG(*)())GetProcAddress(dll, "APH_ExampleOriginalCalls");
    CHECK(manifest && attach && enable && target && count && manifest->size == sizeof(*manifest));
    MEMORY_BASIC_INFORMATION info{}; CHECK(VirtualQuery(manifest, &info, sizeof(info)) && info.Protect == PAGE_READONLY);
    const APH_Owner owner = hub::add_owner(*manifest, dll); CHECK(owner);
    CHECK(hub::attach(owner, attach) == APH_OK);
    applepie::Client observer;
    const APH_AttachArgs args{sizeof(args), 0, owner, hub::api()}; CHECK(observer.Attach(&args) == APH_OK);
    APH_LifecycleApi life{}; CHECK(observer.Service(life) == APH_OK);
    const uint64_t oldEpoch = hub::begin_enable(owner); CHECK(oldEpoch);
    hub::finish_enable(owner, hub::enable(owner, enable, oldEpoch), oldEpoch); State(observer, APH_OWNER_ACTIVE);
    CHECK(target(5) == 16 && count() == 1);
    CHECK(life.disable(owner) == APH_PENDING); State(observer, APH_OWNER_DISABLED);
    CHECK(target(5) == 6 && count() == 2);
    const uint64_t newEpoch = hub::begin_enable(owner); CHECK(newEpoch > oldEpoch);
    hub::finish_enable(owner, hub::enable(owner, enable, newEpoch), newEpoch); State(observer, APH_OWNER_ACTIVE);
    CHECK(life.complete_enable(owner, oldEpoch, APH_OK) == APH_E_STALE);
    CHECK(target(5) == 16 && count() == 3);
    printf("PASS compiled DLL: readonly manifest; attach; async enable; disable; re-enable; stale epoch\n");
}

extern "C" int AphTestNearJump(int);
extern "C" int AphTestShortJump(int);
extern "C" int AphTestIndirectJump(int);
extern "C" int AphTestChangedJump(int);
extern "C" int AphTestDuplicateJump(int);
static volatile LONG thunkOriginals, thunkBusinessCalls;
extern "C" __declspec(noinline) int AphTestThunkBody(int value) {
    InterlockedIncrement(&thunkOriginals); return value + 7;
}
template<uint64_t Tag> static int ThunkBusiness(int value) {
    InterlockedIncrement(&thunkBusinessCalls);
    return applepie::NativeHook<Tag, int(*)(int)>::Original(value) + 100;
}
template<uint64_t Tag> static void CheckThunk(applepie::Client& client,
    applepie::Client& peer, int(*target)(int), unsigned char opcode, const char* name) {
    using Hook = applepie::NativeHook<Tag, int(*)(int)>;
    using Conflict = applepie::NativeHook<Tag + 10, int(*)(int)>;
    unsigned char before[16]; memcpy(before, (void*)target, sizeof(before));
    CHECK(before[0] == opcode && (opcode != 0xff || before[1] == 0x25));
    int (*volatile invoke)(int) = target;
    CHECK(invoke(5) == 12);
    CHECK(Hook::Request(client, Tag, target, ThunkBusiness<Tag>, "i32(i32)", name) == APH_PENDING);
    Await([] { return Hook::Advance(); }, name);
    const APH_Hook claim = Hook::handle();
    LONG originals = thunkOriginals, business = thunkBusinessCalls;
    CHECK(invoke(5) == 112 && thunkOriginals == originals + 1 && thunkBusinessCalls == business + 1);
    CHECK(Conflict::Request(peer, Tag, target, ThunkBusiness<Tag + 10>, "i32(i32)", name) == APH_E_CONFLICT);
    CHECK(!Conflict::handle());
    CHECK(client.Disable() == APH_PENDING); State(client, APH_OWNER_DISABLED);
    originals = thunkOriginals; business = thunkBusinessCalls;
    CHECK(invoke(5) == 12 && thunkOriginals == originals + 1 && thunkBusinessCalls == business);
    Enable(client);
    CHECK(Hook::Request(client, Tag, target, ThunkBusiness<Tag>, "i32(i32)", name) >= 0);
    CHECK(Hook::Advance() == APH_OK && Hook::handle() == claim);
    originals = thunkOriginals;
    CHECK(invoke(5) == 112 && thunkOriginals == originals + 1);
    Await([] { return Hook::Retire(); }, "thunk retirement");
    CHECK(!memcmp(before, (void*)target, sizeof(before)) && invoke(5) == 12);
    CHECK(Conflict::Request(peer, Tag, target, ThunkBusiness<Tag + 10>, "i32(i32)", name) == APH_E_CONFLICT);
}
static __declspec(noinline) int DuplicateEntry(int value) { return value; }
static void ThunkChecks() {
    applepie::Client client, peer;
    Add(client, "sdk.thunks"); Add(peer, "sdk.thunk.peer");
    CheckThunk<950>(client, peer, AphTestNearJump, 0xe9, "near JMP");
    CheckThunk<951>(client, peer, AphTestShortJump, 0xeb, "short JMP");
    CheckThunk<952>(client, peer, AphTestIndirectJump, 0xff, "indirect JMP");

    // Simulate an independently reserved backend target without enabling it.
    // The backend conflict must still fail even though opcode rejection is gone.
    void* original = nullptr;
    CHECK(MH_CreateHook((void*)AphTestDuplicateJump, (void*)DuplicateEntry, &original) == MH_OK && original);
    APH_HookRequest request{}; APH_Hook claim = 0; APH_HookStatus status{};
    CHECK(applepie::MakeHookRequest(request, 1, (APH_NativeProc)AphTestDuplicateJump,
        (APH_NativeProc)DuplicateEntry, "i32(i32)", "backend conflict") == APH_OK);
    CHECK(hub::hooks_api()->request_hook(peer.owner(), &request, &claim) == APH_PENDING && claim);
    Await([&] {
        CHECK(hub::hooks_api()->query_hook(peer.owner(), claim, sizeof(status), &status) == APH_OK);
        return status.state == APH_HOOK_FAILED ? APH_OK : APH_PENDING;
    }, "backend conflict");
    CHECK(status.result == APH_E_FOREIGN_PATCH && !status.original);
    CHECK(MH_RemoveHook((void*)AphTestDuplicateJump) == MH_OK);
    printf("PASS PE thunks: E9, EB, FF25 install/original/disable/re-enable/restore; address and backend conflicts retained\n");
}
static void EntryChangeChecks() {
    applepie::Client client; Add(client, "sdk.changed.entry");
    APH_HookRequest request{}; APH_Hook claim = 0; APH_HookStatus status{};
    CHECK(applepie::MakeHookRequest(request, 1, (APH_NativeProc)AphTestChangedJump,
        (APH_NativeProc)DuplicateEntry, "i32(i32)", "changed entry") == APH_OK);
    CHECK(hub::hooks_api()->request_hook(client.owner(), &request, &claim) == APH_PENDING);
    Await([&] {
        CHECK(hub::hooks_api()->query_hook(client.owner(), claim, sizeof(status), &status) == APH_OK);
        return status.state == APH_HOOK_PREPARED ? APH_OK : APH_PENDING;
    }, "prepared entry");
    // Prepared is a deterministic barrier: no activation is queued yet.
    DWORD protection = 0, ignored = 0;
    CHECK(VirtualProtect((void*)AphTestChangedJump, 1, PAGE_EXECUTE_READWRITE, &protection));
    *(volatile unsigned char*)AphTestChangedJump = 0xcc;
    CHECK(FlushInstructionCache(GetCurrentProcess(), (void*)AphTestChangedJump, 1));
    CHECK(VirtualProtect((void*)AphTestChangedJump, 1, protection, &ignored));
    CHECK(hub::hooks_api()->activate_hook(client.owner(), claim, status.original) == APH_PENDING);
    Await([&] {
        CHECK(hub::hooks_api()->query_hook(client.owner(), claim, sizeof(status), &status) == APH_OK);
        return status.state == APH_HOOK_NEEDS_RESTART ? APH_OK : APH_PENDING;
    }, "changed entry rejection");
    CHECK(status.result == APH_E_NEEDS_RESTART && *(volatile unsigned char*)AphTestChangedJump == 0xcc);
    APH_OwnerStatus owner{};
    CHECK(client.Status(owner) == APH_OK && owner.state == APH_OWNER_NEEDS_RESTART);
    CHECK(hub::hooks_api()->release_hook(client.owner(), claim) == APH_E_NEEDS_RESTART);
    CHECK(!thunkOriginals);
    printf("PASS changed entry: activation refused; external bytes preserved; owner quarantined and claim retained\n");
}

int main(int argc, char** argv) {
    CHECK(hub::initialize() == APH_OK);
    if (argc == 2 && !strcmp(argv[1], "entry-changed")) { EntryChangeChecks(); return 0; }
    CHECK(argc == 1);
    applepie::Client unattached; APH_HookApi unavailable{};
    CHECK(unattached.Attach(nullptr) == APH_E_ARGUMENT && unattached.Service(unavailable) == APH_E_UNAVAILABLE);
    ServiceChecks(); NativeChecks(); ThunkChecks(); SharedChecks(); AdapterChecks(); CustomSharedChecks();
    SubscriptionEpochChecks(); ExampleChecks(); InputBindingChecks(); CancellationChecks(); FaultChecks(); PointerChecks();
    printf("PASS all SDK integration checks: %ld assertions\n", checks);
    return 0;
}
