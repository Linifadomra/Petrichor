#pragma once
#include <stdint.h>

#define PETRICHOR_API_VERSION 3

struct PetrichorArg     { const char* name; const char* kind; const char* view; };
struct PetrichorField   { const char* name; unsigned offset; const char* kind; int len; const char* view; };
struct PetrichorEnumVal { const char* name; long long value; };

struct IPetrichorReflectApi {
    virtual int         fn_count    (const char* flat) const = 0;
    virtual const char* fn_mangled  (const char* flat, int i) const = 0;
    virtual const char* fn_loc      (const char* flat, int i) const = 0;
    virtual const char* resolve_at  (const char* flat, const char* file) const = 0;
    virtual const char* resolve_sig (const char* flat, const char* sig) const = 0;
    virtual int         fn_params   (const char* mangled, const PetrichorArg** out) const = 0;
    virtual const char* fn_self_view(const char* mangled) const = 0;
    virtual const char* fn_ret      (const char* mangled) const = 0;
    virtual int         struct_fields(const char* name, const PetrichorField** out) const = 0;
    virtual int         enum_values (const char* name, const PetrichorEnumVal** out) const = 0;
    virtual int         global_addr (const char* name, const char** kind, void** addr) const = 0;
    virtual void        mem_read    (void* base, int off, const char* kind, void* out) const = 0;
    virtual void        mem_write   (void* base, int off, const char* kind, const void* in) const = 0;
    virtual int         mem_read_str(void* base, int off, int cap, char* out) const = 0;
    virtual void        mem_write_str(void* base, int off, int cap, const char* s) const = 0;
    virtual ~IPetrichorReflectApi() = default;
};

struct IPetrichorMixinApi {
    using Fn = void(*)(void* ctx, void* modctx);
    virtual bool        before (const char* sym, Fn fn, void* modctx, int32_t priority, const char* tag) const = 0;
    virtual bool        after  (const char* sym, Fn fn, void* modctx, int32_t priority, const char* tag) const = 0;
    virtual bool        replace(const char* sym, Fn fn, void* modctx, int32_t priority, const char* tag) const = 0;
    virtual const char* inspect(const char* sym) const = 0;
    virtual ~IPetrichorMixinApi() = default;
};

struct PetrichorEvents {
    void (*pre_frame)(void);
    void (*post_frame)(void);
    void (*scene_load)(const char* stage, int32_t point);
};

struct IPetrichorHost {
    virtual void        log           (const char* tag, const char* msg) const = 0;
    virtual const char* mods_dir      () const = 0;
    virtual void*       alloc         (uint32_t bytes) = 0;
    virtual void*       resolve       (const char* sym) const = 0;
    virtual void        call          (const char* sym, void** args, uint32_t n) = 0;
    virtual void        subscribe_events(const PetrichorEvents* ev) = 0;

    virtual const IPetrichorMixinApi*   mixin()            const { return nullptr; }
    virtual const IPetrichorReflectApi* reflect()          const { return nullptr; }
    virtual void*                       game_api()         const { return nullptr; }
    virtual const char*                 store_dir()        const { return nullptr; }
    virtual const char* const*          blocked_prefixes() const { return nullptr; }

    virtual ~IPetrichorHost() = default;
};

// Hook context handed to a mixin callback
struct PetrichorMixinCtx {
    void*    self;
    void*    ret;
    uint8_t  cancelled;
    void*    user;
    void**   args;
    int      arg_count;
};
typedef void (*PetrichorMixinFn)(PetrichorMixinCtx* ctx, void* modctx);

void petrichor_run (IPetrichorHost& host);
void petrichor_tick(IPetrichorHost& host, float delta);
void petrichor_stop(IPetrichorHost& host);

#ifdef __cplusplus
struct lua_State;
namespace petrichor {
    using ModuleFactory = int(*)(lua_State*);
    void luau_register_module(const char* name, ModuleFactory factory);
}
#endif