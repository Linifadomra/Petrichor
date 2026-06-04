#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PETRICHOR_API_VERSION 1

// Frame/scene events the host fires; the managed host subscribes to these.
typedef struct PetrichorEvents {
    void (*pre_frame)(void);
    void (*post_frame)(void);
    void (*scene_load)(const char* stage, int32_t point);
} PetrichorEvents;

// Hook context handed to a mixin callback.
typedef struct PetrichorMixinCtx {
    void*   self;
    int32_t ret;
    uint8_t cancelled;
    void*   user;
} PetrichorMixinCtx;

typedef void (*PetrichorMixinFn)(PetrichorMixinCtx* ctx, void* modctx);

// The mixin/hook service the host implements (CR: cr_mixin -> Augment).
typedef struct PetrichorMixinApi {
    uint8_t (*before) (const char* sym, PetrichorMixinFn fn, void* modctx, int32_t priority, const char* tag);
    uint8_t (*after)  (const char* sym, PetrichorMixinFn fn, void* modctx, int32_t priority, const char* tag);
    uint8_t (*replace)(const char* sym, PetrichorMixinFn fn, void* modctx, int32_t priority, const char* tag);
    const char* (*inspect)(const char* sym);
} PetrichorMixinApi;

// Everything petrichor needs from the host. The game fills this and calls
// petrichor_run. petrichor never reads `game_api` -- it forwards it to mods,
// which are the only code that knows its real type.
typedef struct PetrichorHost {
    uint32_t version;                                   // PETRICHOR_API_VERSION
    void        (*log)(const char* tag, const char* msg);
    const char* (*mods_dir)(void);
    void*       (*alloc)(uint32_t size);
    void        (*subscribe_events)(const PetrichorEvents* ev);
    const PetrichorMixinApi* mixin;
    void*       (*resolve)(const char* name);           // symbol/service lookup
    void*       game_api;                               // opaque to petrichor
} PetrichorHost;

// Called once by the game, after its subsystems are up, to discover + load mods.
void petrichor_run(const PetrichorHost* host);

#ifdef __cplusplus
}
#endif
