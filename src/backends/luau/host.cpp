#include "internal.hpp"
#include "petrichor/luau.h"

#include "prelude_hook_runtime_inc.h"
#include "prelude_standard_inc.h"
#include "prelude_petrichor_mod_inc.h"
#include "prelude_math_inc.h"
#include "prelude_timer_inc.h"
#include "prelude_json_inc.h"
#include "prelude_events_inc.h"

#include "prelude_async_inc.h"
#include "backends/luau/bindings/async.hpp"
#include "backends/luau/bindings/net.hpp"
#include "backends/luau/bindings/storage.hpp"

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <cstring>
#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

static const struct { const char* name; const unsigned char* src; unsigned int len; } s_prelude_libs[] = {
    { "Augment.Hook",       hook_runtime_luau,  hook_runtime_luau_len },
    { "Petrichor.Standard", standard_luau,      standard_luau_len     }, // globals; newclass(), etc.
    { "Petrichor.Math",     math_luau,          math_luau_len         },
    { "Petrichor.Json",     json_luau,          json_luau_len         },
    { "Petrichor.Mod",      petrichor_mod_luau, petrichor_mod_luau_len },
    { "Petrichor.Events",   events_luau,        events_luau_len },
    { nullptr, nullptr, 0 }
}; // add to register_preludes func

namespace {

struct HookContextGuard {
    void**& args_ref;
    void*&  ret_ref;
    void**  saved_args;
    void*   saved_ret;

    HookContextGuard(void**& args_ref, void*& ret_ref, void** new_args, void* new_ret)
        : args_ref(args_ref), ret_ref(ret_ref)
        , saved_args(args_ref), saved_ret(ret_ref)
    {
        args_ref = new_args;
        ret_ref  = new_ret;
    }

    ~HookContextGuard() {
        args_ref = saved_args;
        ret_ref  = saved_ret;
    }
};

IPetrichorHost* s_host = nullptr;
lua_State*           s_L      = nullptr;
void**               s_cur_args = nullptr;
void*                s_cur_ret  = nullptr;

struct LuauModInstance { std::string id; int ref; std::string mod_dir; std::string store_root; bool dirty; };
static std::vector<LuauModInstance> s_mods;

using ModuleFactory = int(*)(lua_State*);
std::unordered_map<std::string, ModuleFactory> s_module_registry;

static int l_msgh(lua_State* L) {
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2);
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    if (lua_pcall(L, 2, 1, 0) == LUA_OK) return 1;
    lua_pop(L, 1);
    return 1; // fallback: original message still at index 1
}

static int push_msgh(lua_State* L) {
    lua_pushcfunction(L, l_msgh, "msgh");
    return lua_gettop(L);
}

template<typename Fn>
static bool luau_protected(const char* ctx, Fn&& fn) {
    try {
        fn();
        return true;
    } catch (const std::exception& e) {
        petrichor::plog("luau", "exception in %s: %s", ctx, e.what());
    } catch (...) {
        petrichor::plog("luau", "unknown exception in %s", ctx);
    }
    return false;
}

}

void mem_push(lua_State* L, const char* kind, void* p) {
    if      (strcmp(kind, "u8")  == 0) lua_pushnumber(L, *(uint8_t*)p);
    else if (strcmp(kind, "i8")  == 0) lua_pushnumber(L, *(int8_t*)p);
    else if (strcmp(kind, "u16") == 0) lua_pushnumber(L, *(uint16_t*)p);
    else if (strcmp(kind, "i16") == 0) lua_pushnumber(L, *(int16_t*)p);
    else if (strcmp(kind, "u32") == 0) lua_pushnumber(L, *(uint32_t*)p);
    else if (strcmp(kind, "i32") == 0) lua_pushnumber(L, *(int32_t*)p);
    else if (strcmp(kind, "u64") == 0) lua_pushnumber(L, (double)*(uint64_t*)p);
    else if (strcmp(kind, "i64") == 0) lua_pushnumber(L, (double)*(int64_t*)p);
    else if (strcmp(kind, "f32") == 0) lua_pushnumber(L, *(float*)p);
    else if (strcmp(kind, "f64") == 0) lua_pushnumber(L, *(double*)p);
    else if (strcmp(kind, "ptr") == 0) lua_pushlightuserdata(L, *(void**)p);
    else if (strcmp(kind, "pmf") == 0) lua_pushlightuserdata(L, *(void**)p);
    else luaL_error(L, "unknown kind '%s'", kind);
}

void mem_store(lua_State* L, const char* kind, void* p, int vidx) {
    if      (strcmp(kind, "ptr") == 0) *(void**)p = lua_tolightuserdata(L, vidx);
    else if (strcmp(kind, "pmf") == 0) { *(void**)p = lua_tolightuserdata(L, vidx); ((void**)p)[1] = nullptr; }
    else if (strcmp(kind, "f32") == 0) *(float*)p  = (float)luaL_checknumber(L, vidx);
    else if (strcmp(kind, "f64") == 0) *(double*)p = luaL_checknumber(L, vidx);
    else {
        int64_t v = (int64_t)luaL_checknumber(L, vidx);
        if      (strcmp(kind, "u8")  == 0) *(uint8_t*)p  = (uint8_t)v;
        else if (strcmp(kind, "i8")  == 0) *(int8_t*)p   = (int8_t)v;
        else if (strcmp(kind, "u16") == 0) *(uint16_t*)p = (uint16_t)v;
        else if (strcmp(kind, "i16") == 0) *(int16_t*)p  = (int16_t)v;
        else if (strcmp(kind, "u32") == 0) *(uint32_t*)p = (uint32_t)v;
        else if (strcmp(kind, "i32") == 0) *(int32_t*)p  = (int32_t)v;
        else if (strcmp(kind, "u64") == 0) *(uint64_t*)p = (uint64_t)v;
        else if (strcmp(kind, "i64") == 0) *(int64_t*)p  = v;
        else luaL_error(L, "unknown kind '%s'", kind);
    }
}

const char* resolveName(const char* name) {
    if (!name || !s_host || !s_host->reflect()) return name;
    std::string flat(name);
    for (size_t p = flat.find("::"); p != std::string::npos; p = flat.find("::", p))
        flat.replace(p, 2, "_");
    const char* m = s_host->reflect()->fn_mangled(flat.c_str(), 0);
    return m ? m : name;
}

void fire_hook(PetrichorMixinCtx* ctx, int ref) {
    lua_State* L = s_L;

    int msgh = push_msgh(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);

    lua_newtable(L);
    lua_pushlightuserdata(L, ctx->self); lua_setfield(L, -2, "self");
    lua_pushlightuserdata(L, ctx->ret);  lua_setfield(L, -2, "ret");
    lua_pushboolean(L, ctx->cancelled);  lua_setfield(L, -2, "cancelled");

    HookContextGuard guard(s_cur_args, s_cur_ret, ctx->args, ctx->ret);

    luau_protected("fire_hook", [&] {
        if (lua_pcall(L, 1, 1, msgh) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            s_host->log("luau", err ? err : "hook error");
            lua_pop(L, 1);
        } else {
            if (!lua_isnil(L, -1)) ctx->cancelled = (uint8_t)lua_toboolean(L, -1);
            lua_pop(L, 1);
        }
    });

    lua_remove(L, msgh);
}

int l_mixin_register(lua_State* L) {
    const char* sym      = resolveName(luaL_checkstring(L, 1));
    const char* phase    = luaL_checkstring(L, 2);
    int         priority = (int)luaL_optinteger(L, 4, 0);
    const char* tag      = luaL_optstring(L, 5, nullptr);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    int ref = lua_ref(L, 3);
    auto cb = +[](void* vctx, void* mc) {
        fire_hook(static_cast<PetrichorMixinCtx*>(vctx), (int)(intptr_t)mc);
    };
    if (!s_host || !s_host->mixin()) { lua_pushboolean(L, 0); return 1; }
    uint8_t ok = 0;
    if      (strcmp(phase, "before")  == 0) ok = s_host->mixin()->before (sym, cb, (void*)(intptr_t)ref, priority, tag);
    else if (strcmp(phase, "after")   == 0) ok = s_host->mixin()->after  (sym, cb, (void*)(intptr_t)ref, priority, tag);
    else if (strcmp(phase, "replace") == 0) ok = s_host->mixin()->replace(sym, cb, (void*)(intptr_t)ref, priority, tag);
    else luaL_error(L, "unknown phase '%s'", phase);
    lua_pushboolean(L, ok);
    return 1;
}

int l_mixin_call(lua_State* L) {
    const char* sym = resolveName(luaL_checkstring(L, 1));
    if (!s_host) { lua_pushnil(L); return 1; }
    
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TTABLE);
    int n = (int)lua_objlen(L, 2);
    if (n > 16) n = 16;
    uint64_t storage[16] = {};
    void* argp[16];
    for (int i = 0; i < n; i++) {
        lua_rawgeti(L, 2, i + 1); const char* kind = luaL_checkstring(L, -1);
        lua_rawgeti(L, 3, i + 1);
        mem_store(L, kind, &storage[i], -1);
        lua_pop(L, 2);
        argp[i] = &storage[i];
    }
    s_host->call(sym, argp, (uint32_t)n);
    lua_pushnil(L);
    return 1;
}

int l_mem_read(lua_State* L) {
    char* p = (char*)lua_tolightuserdata(L, 1) + luaL_checkinteger(L, 2);
    mem_push(L, luaL_checkstring(L, 3), p);
    return 1;
}

int l_mem_write(lua_State* L) {
    char* p = (char*)lua_tolightuserdata(L, 1) + luaL_checkinteger(L, 2);
    mem_store(L, luaL_checkstring(L, 3), p, 4);
    return 0;
}

int l_mem_read_str(lua_State* L) {
    const char* p = (const char*)lua_tolightuserdata(L, 1) + luaL_checkinteger(L, 2);
    int len = (int)luaL_checkinteger(L, 3);
    int n = 0; while (n < len && p[n]) n++;
    lua_pushlstring(L, p, n);
    return 1;
}

int l_mem_write_str(lua_State* L) {
    char* p = (char*)lua_tolightuserdata(L, 1) + luaL_checkinteger(L, 2);
    int len = (int)luaL_checkinteger(L, 3);
    size_t slen = 0; const char* s = luaL_checklstring(L, 4, &slen);
    int n = (int)slen; if (n > len - 1) n = len - 1;
    for (int i = 0; i < n; i++) p[i] = s[i];
    p[n] = 0;
    return 0;
}

int l_mixin_arg(lua_State* L) {
    int i = (int)luaL_checkinteger(L, 1);
    if (!s_cur_args) luaL_error(L, "Mixin.arg called outside a hook");
    mem_push(L, luaL_checkstring(L, 2), s_cur_args[i]);
    return 1;
}

int l_mixin_set_arg(lua_State* L) {
    int i = (int)luaL_checkinteger(L, 1);
    if (!s_cur_args) luaL_error(L, "Mixin.set_arg called outside a hook");
    mem_store(L, luaL_checkstring(L, 2), s_cur_args[i], 3);
    return 0;
}

int l_mixin_resolve(lua_State* L) {
    const char* sym = resolveName(luaL_checkstring(L, 1));
    void* p = s_host ? s_host->resolve(sym) : nullptr;
    if (p) lua_pushlightuserdata(L, p); else lua_pushnil(L);
    return 1;
}

int l_mixin_cstr(lua_State* L) {
    size_t n = 0; const char* s = luaL_checklstring(L, 1, &n);
    char* buf = s_host ? (char*)s_host->alloc((uint32_t)(n + 1)) : nullptr;
    if (!buf) { lua_pushnil(L); return 1; }
    memcpy(buf, s, n); buf[n] = 0;
    lua_pushlightuserdata(L, buf);
    return 1;
}

bool petrichor::luau_load_module(lua_State* L, const char* name, const unsigned char* src, unsigned int len) {
    size_t bc = 0;
    char* bytecode = luau_compile((const char*)src, len, nullptr, &bc);
    int rc = luau_load(L, name, bytecode, bc, 0);
    free(bytecode);
    if (rc != LUA_OK) {
        plog("luau", "compile error in '%s': %s", name, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        plog("luau", "load error in '%s': %s", name, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

/* ASYNC */

int l_async_defer(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    int ref = lua_ref(L, 1);
    petrichor::async::schedule_step(ref);
    return 0;
}

int l_async_timer(lua_State* L) {
    float secs = (float)luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    petrichor::async::schedule_timer(secs, lua_ref(L, 2));
    return 0;
}

int l_async_hook(lua_State* L) {
    const char* sym   = resolveName(luaL_checkstring(L, 1));
    const char* phase = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    int step_ref = lua_ref(L, 3);

    auto cb = +[](void* vctx, void* mc) {
        petrichor::async::schedule_hook_ctx(
            (int)(intptr_t)mc,
            static_cast<PetrichorMixinCtx*>(vctx)
        );
    };

    if (!s_host || !s_host->mixin()) return 0;
    if      (!strcmp(phase, "before"))  s_host->mixin()->before (sym, cb, (void*)(intptr_t)step_ref, 0, nullptr);
    else if (!strcmp(phase, "after"))   s_host->mixin()->after  (sym, cb, (void*)(intptr_t)step_ref, 0, nullptr);
    else if (!strcmp(phase, "replace")) s_host->mixin()->replace(sym, cb, (void*)(intptr_t)step_ref, 0, nullptr);
    return 0;
}

int l_log_level(lua_State* L, const char* level) {
    const char* tag = luaL_checkstring(L, 1);
    const char* msg = luaL_checkstring(L, 2);
    if (!s_host) return 0;
    if (level) {
        char buf[512];
        snprintf(buf, sizeof(buf), "[%s] %s", level, msg);
        s_host->log(tag, buf);
    } else {
        s_host->log(tag, msg);
    }
    return 0;
}
int l_log_info(lua_State* L) { return l_log_level(L, nullptr); }
int l_log_warn(lua_State* L) { return l_log_level(L, "warn"); }
int l_log_err (lua_State* L) { return l_log_level(L, "err"); }

struct Builder {
    std::string sym;
    struct Entry { std::string phase; int ref; int priority; std::string tag; };
    std::vector<Entry> entries;
};

int l_builder_phase(lua_State* L, const char* phase) {
    Builder* b = (Builder*)luaL_checkudata(L, 1, "PetrichorBuilder");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int priority = (int)luaL_optinteger(L, 3, 0);
    const char* tag = luaL_optstring(L, 4, nullptr);
    int ref = lua_ref(L, 2);
    b->entries.push_back({ phase, ref, priority, tag ? tag : "" });
    lua_pushvalue(L, 1);
    return 1;
}
int l_builder_before (lua_State* L) { return l_builder_phase(L, "before");  }
int l_builder_after  (lua_State* L) { return l_builder_phase(L, "after");   }
int l_builder_replace(lua_State* L) { return l_builder_phase(L, "replace"); }

int l_builder_register(lua_State* L) {
    Builder* b = (Builder*)luaL_checkudata(L, 1, "PetrichorBuilder");
    auto cb = +[](void* vctx, void* mc) { fire_hook(static_cast<PetrichorMixinCtx*>(vctx), (int)(intptr_t)mc); };
    for (auto& e : b->entries) {
        const char* tag = e.tag.empty() ? nullptr : e.tag.c_str();
        if      (e.phase == "before")  s_host->mixin()->before (b->sym.c_str(), cb, (void*)(intptr_t)e.ref, e.priority, tag);
        else if (e.phase == "after")   s_host->mixin()->after  (b->sym.c_str(), cb, (void*)(intptr_t)e.ref, e.priority, tag);
        else if (e.phase == "replace") s_host->mixin()->replace(b->sym.c_str(), cb, (void*)(intptr_t)e.ref, e.priority, tag);
    }
    return 0;
}

int l_mixin_builder(lua_State* L) {
    const char* sym = luaL_checkstring(L, 1);
    Builder* b = (Builder*)lua_newuserdatadtor(L, sizeof(Builder),
        [](void* p) { static_cast<Builder*>(p)->~Builder(); });
    new (b) Builder();
    b->sym = sym;
    if (luaL_newmetatable(L, "PetrichorBuilder")) {
        lua_newtable(L);
        lua_pushcfunction(L, l_builder_before,   "before");   lua_setfield(L, -2, "before");
        lua_pushcfunction(L, l_builder_after,    "after");    lua_setfield(L, -2, "after");
        lua_pushcfunction(L, l_builder_replace,  "replace");  lua_setfield(L, -2, "replace");
        lua_pushcfunction(L, l_builder_register, "register"); lua_setfield(L, -2, "register");
        lua_setfield(L, -2, "__index");
    }
    lua_setmetatable(L, -2);
    return 1;
}

const IPetrichorReflectApi* RX() { return s_host ? s_host->reflect() : nullptr; }

int l_n_fn_count   (lua_State* L) { lua_pushinteger(L, RX() ? RX()->fn_count(luaL_checkstring(L, 1)) : 0); return 1; }
int l_n_fn_mangled (lua_State* L) { const char* m = RX() ? RX()->fn_mangled(luaL_checkstring(L, 1), (int)luaL_checkinteger(L, 2)) : nullptr; if (m) lua_pushstring(L, m); else lua_pushnil(L); return 1; }
int l_n_fn_loc     (lua_State* L) { const char* s = RX() ? RX()->fn_loc(luaL_checkstring(L, 1), (int)luaL_checkinteger(L, 2)) : ""; lua_pushstring(L, s ? s : ""); return 1; }
int l_n_resolve_at (lua_State* L) { const char* m = RX() ? RX()->resolve_at(luaL_checkstring(L, 1), luaL_checkstring(L, 2)) : nullptr; if (m) lua_pushstring(L, m); else lua_pushnil(L); return 1; }
int l_n_resolve_sig(lua_State* L) { const char* m = RX() ? RX()->resolve_sig(luaL_checkstring(L, 1), luaL_checkstring(L, 2)) : nullptr; if (m) lua_pushstring(L, m); else lua_pushnil(L); return 1; }
int l_n_self_view  (lua_State* L) { const char* s = RX() ? RX()->fn_self_view(luaL_checkstring(L, 1)) : ""; lua_pushstring(L, s ? s : ""); return 1; }
int l_n_ret        (lua_State* L) { const char* s = RX() ? RX()->fn_ret(luaL_checkstring(L, 1)) : "void"; lua_pushstring(L, s ? s : "void"); return 1; }

int l_n_params(lua_State* L) {
    const PetrichorArg* a = nullptr;
    int n = RX() ? RX()->fn_params(luaL_checkstring(L, 1), &a) : 0;
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, a[i].name ? a[i].name : ""); lua_setfield(L, -2, "name");
        lua_pushstring(L, a[i].kind);                  lua_setfield(L, -2, "kind");
        lua_pushstring(L, a[i].view ? a[i].view : ""); lua_setfield(L, -2, "view");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int l_n_fields(lua_State* L) {
    const PetrichorField* f = nullptr;
    int n = RX() ? RX()->struct_fields(luaL_checkstring(L, 1), &f) : 0;
    lua_createtable(L, n, 0);
    for (int i = 0; i < n; i++) {
        lua_createtable(L, 0, 5);
        lua_pushstring(L, f[i].name);                  lua_setfield(L, -2, "name");
        lua_pushinteger(L, (int)f[i].offset);          lua_setfield(L, -2, "offset");
        lua_pushstring(L, f[i].kind);                  lua_setfield(L, -2, "kind");
        lua_pushinteger(L, f[i].len);                  lua_setfield(L, -2, "len");
        lua_pushstring(L, f[i].view ? f[i].view : ""); lua_setfield(L, -2, "view");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int l_n_enum_values(lua_State* L) {
    const PetrichorEnumVal* v = nullptr;
    int n = RX() ? RX()->enum_values(luaL_checkstring(L, 1), &v) : 0;
    lua_createtable(L, 0, n);
    for (int i = 0; i < n; i++) {
        lua_pushinteger(L, (lua_Integer)v[i].value);
        lua_setfield(L, -2, v[i].name);
    }
    return 1;
}

int l_n_argptr(lua_State* L) {
    int i = (int)luaL_checkinteger(L, 1);
    if (!s_cur_args) luaL_error(L, "argptr called outside a hook");
    lua_pushlightuserdata(L, s_cur_args[i]);
    return 1;
}

int l_n_register(lua_State* L) {
    const char* mangled = luaL_checkstring(L, 1);
    const char* phase   = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    int ref = lua_ref(L, 3);
    auto cb = +[](void* vctx, void* mc) { fire_hook(static_cast<PetrichorMixinCtx*>(vctx), (int)(intptr_t)mc); };
    if (!s_host || !s_host->mixin()) { lua_pushboolean(L, 0); return 1; }
    void* mc = (void*)(intptr_t)ref;
    uint8_t ok = 0;
    if      (!strcmp(phase, "before"))  ok = s_host->mixin()->before (mangled, cb, mc, 0, nullptr);
    else if (!strcmp(phase, "after"))   ok = s_host->mixin()->after  (mangled, cb, mc, 0, nullptr);
    else if (!strcmp(phase, "replace")) ok = s_host->mixin()    ->replace(mangled, cb, mc, 0, nullptr);
    else luaL_error(L, "unknown phase '%s'", phase);
    lua_pushboolean(L, ok);
    return 1;
}

static void run_prelude(lua_State* L, const char* name, const unsigned char* src, unsigned int len) {
    lua_getfield(L, LUA_REGISTRYINDEX, "_petrichor_modcache");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "_petrichor_modcache");
    }
    int cache_idx = lua_gettop(L);
    if (!petrichor::luau_load_module(L, name, src, len)) {
        lua_pop(L, 1);
        return;
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, cache_idx, name);
    lua_pop(L, 2);
}

int l_require_native(lua_State* L) {
    lua_newtable(L);
    auto set = [&](const char* k, lua_CFunction f) { lua_pushcfunction(L, f, k); lua_setfield(L, -2, k); };
    set("fnCount",    l_n_fn_count);
    set("fnMangled",  l_n_fn_mangled);
    set("fnLoc",      l_n_fn_loc);
    set("resolveAt",  l_n_resolve_at);
    set("resolveSig", l_n_resolve_sig);
    set("params",     l_n_params);
    set("selfView",   l_n_self_view);
    set("retKind",    l_n_ret);
    set("fields",     l_n_fields);
    set("enumValues", l_n_enum_values);
    set("read",       l_mem_read);
    set("write",      l_mem_write);
    set("readStr",    l_mem_read_str);
    set("writeStr",   l_mem_write_str);
    set("arg",        l_mixin_arg);
    set("setArg",     l_mixin_set_arg);
    set("argptr",     l_n_argptr);
    set("register",   l_n_register);
    return 1;
}

int l_require_mixin(lua_State* L) {
    lua_newtable(L);
    auto set = [&](const char* k, lua_CFunction f) { lua_pushcfunction(L, f, k); lua_setfield(L, -2, k); };
    set("register", l_mixin_register);
    set("builder",  l_mixin_builder);
    set("call",     l_mixin_call);
    set("read",     l_mem_read);
    set("write",    l_mem_write);
    set("read_str", l_mem_read_str);
    set("write_str",l_mem_write_str);
    set("arg",      l_mixin_arg);
    set("set_arg",  l_mixin_set_arg);
    set("resolve",  l_mixin_resolve);
    set("cstr",     l_mixin_cstr);
    return 1;
}

int l_require_log(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_log_info, "info"); lua_setfield(L, -2, "info");
    lua_pushcfunction(L, l_log_warn, "warn"); lua_setfield(L, -2, "warn");
    lua_pushcfunction(L, l_log_err,  "err");  lua_setfield(L, -2, "err");
    return 1;
}

static int l_require_lua_chunk(lua_State* L, const char* name, const unsigned char* src, unsigned int len) {
    if (!petrichor::luau_load_module(L, name, src, len))
        luaL_error(L, "%s: failed to load", name);
    return 1;
}

int l_require_timer (lua_State* L) { return l_require_lua_chunk(L, "Petrichor.Timer", timer_luau, timer_luau_len); }

int l_require_petrichor_async(lua_State* L) {
    int msgh = push_msgh(L);

    lua_newtable(L);
    auto set = [&](const char* k, lua_CFunction f) {
        lua_pushcfunction(L, f, k); lua_setfield(L, -2, k);
    };
    set("defer", l_async_defer);
    set("timer", l_async_timer);
    set("hook",  l_async_hook);

    size_t bc = 0;
    char* bytecode = luau_compile((const char*)async_luau, async_luau_len, nullptr, &bc);
    int rc = luau_load(L, "Petrichor.Task", bytecode, bc, 0);
    free(bytecode);
    if (rc != LUA_OK) luaL_error(L, "Petrichor.Task: %s", lua_tostring(L, -1));

    lua_insert(L, -2);

    if (lua_pcall(L, 1, 1, msgh) != LUA_OK)
        luaL_error(L, "Petrichor.Task: %s", lua_tostring(L, -1));

    lua_remove(L, msgh);
    return 1;
}

std::vector<std::string> s_mod_dirs;

int l_require(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);

    int msgh = push_msgh(L);

    lua_getfield(L, LUA_REGISTRYINDEX, "_petrichor_modcache");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "_petrichor_modcache");
    }
    int cache_idx = lua_gettop(L);

    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1)) {
        lua_remove(L, msgh);
        return 1;
    }
    lua_pop(L, 1);

    for (auto* e = s_prelude_libs; e->name; e++) {
        if (strcmp(name, e->name) == 0) {
            size_t bc = 0;
            char* bytecode = luau_compile((const char*)e->src, e->len, nullptr, &bc);
            int rc = luau_load(L, e->name, bytecode, bc, 0);
            free(bytecode);
            if (rc != LUA_OK) luaL_error(L, "prelude compile '%s': %s", name, lua_tostring(L, -1));
            if (lua_pcall(L, 0, 1, msgh) != LUA_OK) luaL_error(L, "prelude load '%s': %s", name, lua_tostring(L, -1));
            lua_pushvalue(L, -1);
            lua_setfield(L, cache_idx, name);
            lua_remove(L, msgh);
            return 1;
        }
    }

    auto it = s_module_registry.find(name);
    if (it != s_module_registry.end()) {
        it->second(L);
    } else {
        std::string rel;
        for (const char* p = name; *p; p++) rel += (*p == '.') ? '/' : *p;
        rel += ".luau";

        FILE* f = nullptr;
        for (const auto& dir : s_mod_dirs) {
            std::string path = dir + "/" + rel;
            f = fopen(path.c_str(), "rb");
            if (f) break;
        }
        if (!f) luaL_error(L, "module '%s' not found", name);

        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz < 0) { fclose(f); luaL_error(L, "module '%s': cannot read", name); }
        std::string src((size_t)sz, '\0');
        src.resize(fread(&src[0], 1, (size_t)sz, f));
        fclose(f);

        size_t bcSize = 0;
        char* bc = luau_compile(src.c_str(), src.size(), nullptr, &bcSize);
        int rc = luau_load(L, name, bc, bcSize, 0);
        free(bc);
        if (rc != LUA_OK) luaL_error(L, "compile '%s': %s", name, lua_tostring(L, -1));
        if (lua_pcall(L, 0, 1, msgh) != LUA_OK) luaL_error(L, "load '%s': %s", name, lua_tostring(L, -1));
    }

    lua_pushvalue(L, -1);
    lua_setfield(L, cache_idx, name);
    lua_remove(L, msgh);
    return 1;
}

namespace petrichor {

void luau_register_module(const char* name, ModuleFactory factory) {
    if (name && factory) s_module_registry[name] = factory;
}

bool luau_boot(IPetrichorHost& host) {
    s_host = &host;

    s_L = luaL_newstate();
    if (!s_L) return false;

    luaL_openlibs(s_L);

    lua_pushcfunction(s_L, l_require, "require");
    lua_setglobal(s_L, "require");

    s_module_registry["Augment.Native"]    = l_require_native;
    s_module_registry["Augment.Mixin"]     = l_require_mixin;
    
    s_module_registry["Petrichor.Log"]     = l_require_log;
    s_module_registry["Petrichor.Task"]    = l_require_petrichor_async;
    s_module_registry["Petrichor.Timer"]   = l_require_timer;

    s_module_registry["Petrichor.Net"]     = petrichor::net::require_module;
    petrichor::net::boot();

    for (auto* e = s_prelude_libs; e->name; e++)
        run_prelude(s_L, e->name, e->src, e->len);

    plog("luau", "Luau host started");
    return true;
}

bool luau_loadMod(const char* dir, const char* id, const char* type, const char* entry, const char* store_root) {
    if (!s_L) return false;

    const int base = lua_gettop(s_L);

    bool known = false;
    for (const auto& d : s_mod_dirs) if (d == dir) { known = true; break; }
    if (!known) s_mod_dirs.emplace_back(dir);

    std::string entryFile = std::string(dir) + "/" + (entry && entry[0] ? entry : "main.luau");

    FILE* f = fopen(entryFile.c_str(), "rb");
    if (!f) { plog("luau", "entry not found: %s", entryFile.c_str()); return false; }

    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); plog("luau", "cannot read %s", entryFile.c_str()); return false; }
    std::string src((size_t)sz, '\0');
    src.resize(fread(&src[0], 1, (size_t)sz, f));
    fclose(f);

    int msgh = push_msgh(s_L);

    {
        lua_getfield(s_L, LUA_REGISTRYINDEX, "_petrichor_modcache");
        if (!lua_istable(s_L, -1)) {
            lua_pop(s_L, 1);
            lua_newtable(s_L);
            lua_pushvalue(s_L, -1);
            lua_setfield(s_L, LUA_REGISTRYINDEX, "_petrichor_modcache");
        }
        petrichor::storage::require_module(s_L, std::string(id), std::string(dir), store_root ? std::string(store_root) : "");
        lua_setfield(s_L, -2, "Petrichor.Storage");
        lua_pop(s_L, 1);
    }

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(src.c_str(), src.size(), nullptr, &bytecodeSize);
    int rc = luau_load(s_L, id, bytecode, bytecodeSize, 0);
    free(bytecode);

    if (rc != LUA_OK) {
        plog("luau", "compile error in %s: %s", id, lua_tostring(s_L, -1));
        lua_settop(s_L, base);
        return false;
    }

    if (lua_pcall(s_L, 0, 1, msgh) != LUA_OK) {
        plog("luau", "runtime error in %s: %s", id, lua_tostring(s_L, -1));
        lua_settop(s_L, base);
        return false;
    }
    lua_remove(s_L, msgh);

    if (!lua_istable(s_L, -1)) {
        plog("luau", "mod '%s' did not return a class table", id);
        lua_settop(s_L, base);
        return false;
    }

    const char* required[] = { "tick", "shutdown", nullptr };
    for (const char** m = required; *m; m++) {
        lua_getfield(s_L, -1, *m);
        bool ok = lua_isfunction(s_L, -1);
        lua_pop(s_L, 1);
        if (!ok) {
            plog("luau", "mod '%s' missing required method '%s'", id, *m);
            lua_settop(s_L, base);
            return false;
        }
    }

    msgh = push_msgh(s_L);
    lua_getfield(s_L, -2, "new");
    if (!lua_isfunction(s_L, -1)) {
        plog("luau", "mod '%s' class has no .new()", id);
        lua_settop(s_L, base);
        return false;
    }

    if (lua_pcall(s_L, 0, 1, msgh) != LUA_OK) {
        plog("luau", "mod '%s' new() failed: %s", id, lua_tostring(s_L, -1));
        lua_settop(s_L, base);
        return false;
    }
    lua_remove(s_L, msgh);
    lua_remove(s_L, -2);

    int ref = lua_ref(s_L, -1);
    lua_pop(s_L, 1);

    s_mods.push_back({ std::string(id), ref, std::string(dir), store_root ? std::string(store_root) : "", false });
    plog("luau", "loaded mod: %s", id);
    return true;
}

void luau_tick(float delta) {
    if (!s_L) return;
    petrichor::async::tick(s_L, delta);
    for (auto& mod : s_mods) {
        luau_protected(mod.id.c_str(), [&] {
            lua_rawgeti(s_L, LUA_REGISTRYINDEX, mod.ref);
            int msgh = push_msgh(s_L);
            lua_getfield(s_L, -2, "tick");
            lua_pushvalue(s_L, -3);
            lua_pushnumber(s_L, delta);
            if (lua_pcall(s_L, 2, 0, msgh) != LUA_OK) {
                plog("luau", "tick error in %s: %s", mod.id.c_str(), lua_tostring(s_L, -1));
                lua_pop(s_L, 1);
            }
            lua_pop(s_L, 2);
        });
    }
}

void luau_stop() {
    if (!s_L) return;
    for (auto& mod : s_mods) {
        luau_protected(mod.id.c_str(), [&] {
            lua_rawgeti(s_L, LUA_REGISTRYINDEX, mod.ref);
            int msgh = push_msgh(s_L);

            lua_getfield(s_L, -2, "save");
            if (lua_isfunction(s_L, -1)) {
                lua_pushvalue(s_L, -3);
                if (lua_pcall(s_L, 1, 0, msgh) != LUA_OK)
                    lua_pop(s_L, 1);
            } else {
                lua_pop(s_L, 1);
            }

            lua_getfield(s_L, -2, "shutdown");
            lua_pushvalue(s_L, -3);
            if (lua_pcall(s_L, 1, 0, msgh) != LUA_OK) {
                plog("luau", "shutdown error in %s: %s", mod.id.c_str(), lua_tostring(s_L, -1));
                lua_pop(s_L, 1);
            }
            lua_pop(s_L, 2);
        });
        lua_unref(s_L, mod.ref);
    }
    s_mods.clear();
    petrichor::async::stop(s_L);
    petrichor::net::stop();
    lua_close(s_L);
    s_L = nullptr;
    s_host = nullptr;
    s_mod_dirs.clear();
}

} // namespace petrichor