#pragma once
#include "applepie.h"
/* Client-owned adapters; Core never infers a C++ signature from a string.
 * Windows x64 IL2CPP, validated at runtime before registration. Pointers in
 * these borrowed payloads are identities, not a GAME permission grant. */
#pragma pack(push,8)
typedef struct APH_EfSetMain {void* self;void* entity;int32_t reason;uint32_t reserved;void* method;} APH_EfSetMain;
typedef struct APH_EfVoid {void* self;void* method;} APH_EfVoid;
typedef struct APH_EfUpdate {void* self;float delta_time;uint32_t reserved;void* method;} APH_EfUpdate;
typedef struct APH_EfJob {
    void* self;uint32_t count,reserved;
    const void* dependency;const void* method;const void* result;
} APH_EfJob;
/* result is null in BEFORE and the unchanged native return value in AFTER.
 * All references are borrowed for this call; retaining them needs a separate
 * lifetime/binding-generation policy in the consuming plugin. */
typedef struct APH_EfInputBinding {
    void* self;void* action_id;void* callback;
    int32_t priority;uint32_t reserved;
    void* method;void* result;
} APH_EfInputBinding;
#pragma pack(pop)
/* AFTER of this job contract is schedule-return, NEVER completion of the work. */
#define APH_EF_SETMAIN_I32 "endfield.SetMainCharacter.reason-i32"
#define APH_EF_SETMAIN_BOOL "endfield.SetMainCharacter.reason-bool"
#define APH_EF_BIPED "endfield.BipedIK.UpdateSolver.controlled-void"
#define APH_EF_UPDATE "endfield.SkeletalMorphCore.Update"
#define APH_EF_JOB "endfield.SkeletalMorphCore.MorphJob.schedule"
#define APH_EF_INPUT_BINDING "endfield.InputManager.CreateBindingByActionId"
