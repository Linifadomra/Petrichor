#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#define PETRICHOR_API_VERSION 3

struct PetrichorManifest {
    char id[64];
    char name[128];
    char kind[16]; // "code" | "asset" | "mixed"
    char type[32];
    char entry[128];
    char author[128];
    char version[32];
    std::string desc;
    std::vector<std::string> conflicts;
    int apiVersion;
};

struct PetrichorEvents {
    void (*pre_frame)(void);
    void (*post_frame)(void);
    void (*scene_load)(const char* stage, int32_t point);
};

enum class PetrichorLogLevel : uint8_t { Info = 0, Warn, Error, Debug };

struct IPetrichorHost {
    virtual void        log             (PetrichorLogLevel level, const char* tag, const char* msg) const = 0;
    virtual const char* mods_dir        () const = 0;
    virtual const char* temp_dir        () const = 0;
    virtual void*       alloc           (uint32_t bytes) = 0;
    virtual void*       resolve         (const char* sym) const = 0;
    virtual int         call            (const char* sym, void** args, uint32_t n, void* ret_out) = 0;
    virtual void        subscribe_events(const PetrichorEvents* ev) = 0;

    virtual void*                game_api()         const { return nullptr; }
    virtual const char*          store_dir()        const { return nullptr; }
    virtual const char* const*   blocked_prefixes() const { return nullptr; }

    virtual ~IPetrichorHost() = default;
};

struct PetrichorMixinCtx {
    void*    self;
    void*    ret;
    uint8_t  cancelled;
    void*    user;
    void**   args;
    int      arg_count;
};
typedef void (*PetrichorMixinFn)(PetrichorMixinCtx* ctx, void* modctx);

void petrichor_run (IPetrichorHost& host, const char* format = "prm");
void petrichor_tick(IPetrichorHost& host, float delta);
void petrichor_stop(IPetrichorHost& host);
void petrichor_unload_mod(const char* id);
void petrichor_reload_mod(const char* id);
std::vector<std::string> petrichor_poll_changes();
std::vector<PetrichorManifest> petrichor_get_mods();

#ifdef __cplusplus
struct lua_State;
namespace petrichor {
    using ModuleFactory = int(*)(lua_State*);
    void luau_register_module(const char* name, ModuleFactory factory);
    lua_State* lua_state();
}
#endif
