#pragma once
// OPTIONAL Endfield contracts. No offsets, reflection, target discovery, runtime
// attestation or game writes. Only use after validating the actual game's ABI.
#include "applepie_client.h"
#include "applepie_endfield_contracts.h"

namespace applepie::endfield {
namespace detail {
struct SetMainI32Spec {
    using Function = void(*)(void*, void*, int32_t, void*);
    using Payload = APH_EfSetMain;
    static constexpr const char* signature = "void(self*,Entity*,i32,MethodInfo*)";
    static constexpr const char* contract = APH_EF_SETMAIN_I32;
    static constexpr uint32_t policy = APH_ORIGINAL_ONCE;
    static Payload Pack(void* self, void* entity, int32_t reason, void* method) {
        return {self, entity, reason, 0, method};
    }
};
struct SetMainBoolSpec {
    using Function = void(*)(void*, void*, bool, void*);
    using Payload = APH_EfSetMain;
    static constexpr const char* signature = "void(self*,Entity*,bool8,MethodInfo*)";
    static constexpr const char* contract = APH_EF_SETMAIN_BOOL;
    static constexpr uint32_t policy = APH_ORIGINAL_ONCE;
    static Payload Pack(void* self, void* entity, bool reason, void* method) {
        return {self, entity, reason ? 1 : 0, 0, method};
    }
};
struct BipedIKSpec {
    using Function = void(*)(void*, void*);
    using Payload = APH_EfVoid;
    static constexpr const char* signature = "void(self*,MethodInfo*)";
    static constexpr const char* contract = APH_EF_BIPED;
    static constexpr uint32_t policy = APH_CONTROLLED_VOID;
    static Payload Pack(void* self, void* method) { return {self, method}; }
};
struct SMCUpdateSpec {
    using Function = void(*)(void*, float, void*);
    using Payload = APH_EfUpdate;
    static constexpr const char* signature = "void(self*,f32,MethodInfo*)";
    static constexpr const char* contract = APH_EF_UPDATE;
    static constexpr uint32_t policy = APH_ORIGINAL_ONCE;
    static Payload Pack(void* self, float delta, void* method) { return {self, delta, 0, method}; }
};
// Matches the v1 contract already used by AntiKick and EndfieldVR.
struct GameAppUpdateSpec {
    using Function = void(*)(void*, void*);
    using Payload = APH_EfVoid;
    static constexpr const char* signature = "void(self*,MethodInfo*)";
    static constexpr const char* contract = APH_EF_GAMEAPP_UPDATE;
    static constexpr uint32_t policy = APH_ORIGINAL_ONCE;
    static Payload Pack(void* self, void* method) { return {self, method}; }
};
struct MorphJobSpec {
    // Verified IL2CPP native form: explicit 16-byte result buffer and dependency
    // address. Do not substitute a guessed C++ struct-by-value signature.
    using Function = void*(*)(void*, void*, uint32_t, void*, void*);
    using Payload = APH_EfJob;
    static constexpr const char* signature = "ptr(sret16*,self*,u32,job16*,MethodInfo*)";
    static constexpr const char* contract = APH_EF_JOB;
    static constexpr uint32_t policy = APH_ORIGINAL_ONCE;
    static Payload Pack(void* result, void* self, uint32_t count, void* dependency, void* method) {
        return {self, count, 0, dependency, method, result};
    }
    // Payload.result is the explicit output buffer, not the return register.
    static void Returned(Payload&, void*) {}
};
struct SpecialMorphJobSpec : MorphJobSpec {
    static constexpr const char* contract = "endfield.SkeletalMorphCore.SpecialMorphJob.schedule";
};
struct InputBindingSpec {
    using Function = void*(*)(void*, void*, void*, int32_t, void*);
    using Payload = APH_EfInputBinding;
    static constexpr const char* signature = "ptr(self*,string*,delegate*,i32,MethodInfo*)";
    static constexpr const char* contract = APH_EF_INPUT_BINDING;
    static constexpr uint32_t policy = APH_ORIGINAL_ONCE;
    static Payload Pack(void* self, void* action, void* callback, int32_t priority, void* method) {
        return {self, action, callback, priority, 0, method, nullptr};
    }
    static void Returned(Payload& payload, void* result) { payload.result = result; }
};

template<unsigned Tag, class Spec, class Function = typename Spec::Function> class Adapter;
template<unsigned Tag, class Spec, class R, class... A> class Adapter<Tag, Spec, R(*)(A...)> {
    inline static SharedHook hook_{};
    inline static APH_NativeProc original_ = nullptr;
    using Payload = typename Spec::Payload;
    static_assert(std::is_trivially_copyable_v<Payload> && sizeof(Payload) <= 256, "borrowed POD payload");
    static_assert(Spec::policy != APH_CONTROLLED_VOID || std::is_void_v<R>, "only void can be suppressed");

    static LONG Fault(EXCEPTION_POINTERS* ex, uint32_t phase) {
        hook_.api()->native_fault(hook_.owner(), hook_.handle(), ex->ExceptionRecord->ExceptionCode,
            (uint64_t)(uintptr_t)ex->ExceptionRecord->ExceptionAddress, phase);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static APH_Result Publish(APH_SharedCall* call, APH_Result entered, uint32_t phase, const Payload* p) {
        if (entered != APH_OK || !(call->flags & APH_SHARED_DISPATCH)) return APH_OK;
        const APH_Result result = hook_.api()->publish(call, phase, p, sizeof(*p));
        // Preserve the native fault boundary when Core rejects dispatch. Do
        // not silently continue after a failed callback or replay original.
        if (result < 0) RaiseException(0xe0411001u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return result;
    }
    static R Entry(A... args) {
        APH_SharedCall call{};
        const APH_Result entered = hook_.api()->enter(hook_.owner(), hook_.handle(), &call);
        Payload payload = Spec::Pack(args...);
        uint32_t phase = APH_ORIGINAL_NOT_ENTERED;
        if constexpr (std::is_void_v<R>) {
            __try {
                __try {
                    const APH_Result decision = Publish(&call, entered, APH_BEFORE, &payload);
                    if (Spec::policy != APH_CONTROLLED_VOID || decision != APH_SUPPRESS) {
                        phase = APH_ORIGINAL_RUNNING;
                        ((Function)original_)(args...);
                        phase = APH_ORIGINAL_RETURNED;
                    }
                    Publish(&call, entered, APH_AFTER, &payload);
                } __except (Fault(GetExceptionInformation(), phase)) {}
            } __finally { if (entered == APH_OK) hook_.api()->leave(&call); }
        } else {
            R result{};
            __try {
                __try {
                    Publish(&call, entered, APH_BEFORE, &payload);
                    phase = APH_ORIGINAL_RUNNING;
                    result = ((Function)original_)(args...);
                    phase = APH_ORIGINAL_RETURNED;
                    Spec::Returned(payload, result);
                    Publish(&call, entered, APH_AFTER, &payload);
                } __except (Fault(GetExceptionInformation(), phase)) {}
            } __finally { if (entered == APH_OK) hook_.api()->leave(&call); }
            return result;
        }
    }
public:
    using Function = R(*)(A...);
    static APH_Result Request(const Client& client, uint64_t key, Function target) {
        APH_SharedRequest request{};
        APH_Result r = MakeSharedRequest(request, key, (APH_NativeProc)target, (APH_NativeProc)Entry,
            Spec::signature, Spec::contract, 1, sizeof(Payload), Spec::policy);
        return r == APH_OK ? hook_.Request(client, request) : r;
    }
    static APH_Result Advance() { return hook_.Advance(&original_); }
    static APH_Result Status(APH_SharedStatus& out) { return hook_.Status(out); }
    static APH_SharedClaim handle() { return hook_.handle(); }
    static const SharedHook& registration() { return hook_; }
    static APH_Result Listen(Subscription& subscription, APH_SharedCallback callback, void* context = nullptr,
        uint32_t phases = APH_BEFORE | APH_AFTER, uint32_t role = APH_OBSERVER, uint32_t recovery = APH_RECOVERY_NONE) {
        const APH_SharedListen desc{sizeof(desc), phases, role, recovery, callback, context};
        return subscription.Listen(hook_, desc);
    }
    // Terminal membership retirement. Original/provider context is NEVER reset:
    // another plugin may continue to use this module's entry after it returns OK.
    static APH_Result Retire() { return hook_.Retire(); }
};
} // namespace detail

template<unsigned Tag> using SetMainI32 = detail::Adapter<Tag, detail::SetMainI32Spec>;
template<unsigned Tag> using SetMainBool = detail::Adapter<Tag, detail::SetMainBoolSpec>;
template<unsigned Tag> using BipedIK = detail::Adapter<Tag, detail::BipedIKSpec>;
template<unsigned Tag> using SMCUpdate = detail::Adapter<Tag, detail::SMCUpdateSpec>;
template<unsigned Tag> using GameAppUpdate = detail::Adapter<Tag, detail::GameAppUpdateSpec>;
template<unsigned Tag> using MorphJob = detail::Adapter<Tag, detail::MorphJobSpec>;
template<unsigned Tag> using SpecialMorphJob = detail::Adapter<Tag, detail::SpecialMorphJobSpec>;
template<unsigned Tag> using CreateBindingByActionId = detail::Adapter<Tag, detail::InputBindingSpec>;
// MorphJob/SpecialMorphJob AFTER means the scheduling call returned, not that
// the scheduled job has finished. No thread or write authority is implied.
} // namespace applepie::endfield
