#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PETRICHOR_API_VERSION 2

// Frame/scene events the host fires; the managed host subscribes to these.
typedef struct PetrichorEvents {
    void (*pre_frame)(void);
    void (*post_frame)(void);
    void (*scene_load)(const char* stage, int32_t point);
} PetrichorEvents;

// Hook context handed to a mixin callback.
typedef struct PetrichorMixinCtx {
    void*   self;
    void*   ret;
    uint8_t cancelled;
    void*   user;
    void**  args;     // hooked call's argument slots; args[i] points to arg i
} PetrichorMixinCtx;

typedef void (*PetrichorMixinFn)(PetrichorMixinCtx* ctx, void* modctx);

// The mixin/hook service the host implements (CR: cr_mixin -> Augment).
typedef struct PetrichorMixinApi {
    uint8_t (*before) (const char* sym, PetrichorMixinFn fn, void* modctx, int32_t priority, const char* tag);
    uint8_t (*after)  (const char* sym, PetrichorMixinFn fn, void* modctx, int32_t priority, const char* tag);
    uint8_t (*replace)(const char* sym, PetrichorMixinFn fn, void* modctx, int32_t priority, const char* tag);
    const char* (*inspect)(const char* sym);
} PetrichorMixinApi;

typedef struct PetrichorArg     { const char* name; const char* kind; const char* view; } PetrichorArg;
typedef struct PetrichorField   { const char* name; unsigned offset; const char* kind; int len; const char* view; } PetrichorField;
typedef struct PetrichorEnumVal { const char* name; long long value; } PetrichorEnumVal;

typedef struct PetrichorReflectApi {
    int         (*fn_count)(const char* flat);
    const char* (*fn_mangled)(const char* flat, int i);
    const char* (*fn_loc)(const char* flat, int i);
    const char* (*resolve_at)(const char* flat, const char* file);
    const char* (*resolve_sig)(const char* flat, const char* sig);
    int         (*fn_params)(const char* mangled, const PetrichorArg** out);
    const char* (*fn_self_view)(const char* mangled);
    const char* (*fn_ret)(const char* mangled);
    int         (*struct_fields)(const char* name, const PetrichorField** out);
    int         (*enum_values)(const char* name, const PetrichorEnumVal** out);
    int         (*global_addr)(const char* name, const char** kind, void** addr);
    void        (*mem_read)(void* base, int off, const char* kind, void* out);
    void        (*mem_write)(void* base, int off, const char* kind, const void* in);
    int         (*mem_read_str)(void* base, int off, int cap, char* out);
    void        (*mem_write_str)(void* base, int off, int cap, const char* s);
} PetrichorReflectApi;

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
    const PetrichorReflectApi* reflect;
    void*       (*resolve)(const char* name);           // symbol/service lookup
    void        (*call)(const char* sym, void** args, uint32_t nargs);  // invoke a game fn
    void*       game_api;                               // opaque to petrichor
} PetrichorHost;

// Called once by the game, after its subsystems are up, to discover + load mods.
void petrichor_run(const PetrichorHost* host);
void petrichor_tick(const PetrichorHost* host, float delta);
void petrichor_stop(const PetrichorHost* host);

#ifdef __cplusplus
}

struct lua_State;
namespace petrichor {
    using ModuleFactory = int(*)(lua_State*);
    void luau_register_module(const char* name, ModuleFactory factory);
}
#endif
