#include "internal.hpp"
#include "hook_runtime.inc"

#include <lua.h>
#include <lualib.h>
#include <luacode.h>  // luau_compile

#include <cstring>
#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
namespace {

const PetrichorHost* s_host = nullptr;
lua_State* s_L = nullptr;

void** s_cur_args = nullptr;
void*  s_cur_ret  = nullptr;

// Read/write a single ffi-kind value at p.
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
    if      (strcmp(kind, "ptr") == 0) *(void**)p  = lua_tolightuserdata(L, vidx);
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

// Map a readable name (flat "A_b" or qualified "A::b") to its mangled symbol via
// the reflect manifest, so mods use real names instead of raw mangled ones.
// Falls back to the given name (already-mangled or non-manifest symbols).
const char* resolveName(const char* name) {
    if (!name || !s_host || !s_host->reflect) return name;
    std::string flat(name);
    for (size_t p = flat.find("::"); p != std::string::npos; p = flat.find("::", p))
        flat.replace(p, 2, "_");
    const char* m = s_host->reflect->fn_mangled(flat.c_str(), 0);
    return m ? m : name;
}

// ---------------------------------------------------------------------------
// Host bindings exposed to Luau
// ---------------------------------------------------------------------------

// Mixin.register(symbol, phase, fn, priority, tag)
// phase: "before" | "after" | "replace"
int l_mixin_register(lua_State* L) {
    const char* sym      = resolveName(luaL_checkstring(L, 1));
    const char* phase    = luaL_checkstring(L, 2);
    int         priority = (int)luaL_optinteger(L, 4, 0);
    const char* tag      = luaL_optstring(L, 5, nullptr);

    luaL_checktype(L, 3, LUA_TFUNCTION);

    // Store fn in registry so we can pass a stable pointer
    int ref = lua_ref(L, 3);

    // Callback that fires the stored Luau function
    auto cb = [](PetrichorMixinCtx* ctx, void* modctx) {
        lua_State* L2 = s_L;
        int ref2 = (int)(intptr_t)modctx;
        lua_rawgeti(L2, LUA_REGISTRYINDEX, ref2);

        lua_newtable(L2);
        lua_pushlightuserdata(L2, ctx->self);
        lua_setfield(L2, -2, "self");
        lua_pushlightuserdata(L2, ctx->ret);
        lua_setfield(L2, -2, "ret");
        lua_pushboolean(L2, ctx->cancelled);
        lua_setfield(L2, -2, "cancelled");

        void** saved = s_cur_args;
        s_cur_args = ctx->args;
        if (lua_pcall(L2, 1, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L2, -1);
            s_host->log("luau", err ? err : "unknown error in mixin callback");
            lua_pop(L2, 1);
        }
        s_cur_args = saved;
    };

    if (!s_host || !s_host->mixin) {
        lua_pushboolean(L, 0);
        return 1;
    }

    uint8_t ok = 0;
    if      (strcmp(phase, "before")  == 0) ok = s_host->mixin->before (sym, cb, (void*)(intptr_t)ref, priority, tag);
    else if (strcmp(phase, "after")   == 0) ok = s_host->mixin->after  (sym, cb, (void*)(intptr_t)ref, priority, tag);
    else if (strcmp(phase, "replace") == 0) ok = s_host->mixin->replace(sym, cb, (void*)(intptr_t)ref, priority, tag);
    else luaL_error(L, "unknown phase '%s'", phase);

    lua_pushboolean(L, ok);
    return 1;
}

// Mixin.call(symbol, kinds, values) -> nil
// kinds[i]/values[i] are parallel; for a member fn index 1 is "ptr" (self).
int l_mixin_call(lua_State* L) {
    const char* sym = resolveName(luaL_checkstring(L, 1));
    if (!s_host || !s_host->call) { lua_pushnil(L); return 1; }
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TTABLE);

    int n = (int)lua_objlen(L, 2);
    if (n > 16) n = 16;

    uint64_t storage[16] = {};
    void*    argp[16];
    for (int i = 0; i < n; i++) {
        lua_rawgeti(L, 2, i + 1);                  // kind
        const char* kind = luaL_checkstring(L, -1);
        lua_rawgeti(L, 3, i + 1);                  // value
        mem_store(L, kind, &storage[i], -1);
        lua_pop(L, 2);
        argp[i] = &storage[i];
    }

    s_host->call(sym, argp, (uint32_t)n);
    lua_pushnil(L);  // return value not captured yet
    return 1;
}

// Mixin.read(self, offset, kind) -> number | lightuserdata
int l_mem_read(lua_State* L) {
    char* p = (char*)lua_tolightuserdata(L, 1) + luaL_checkinteger(L, 2);
    mem_push(L, luaL_checkstring(L, 3), p);
    return 1;
}

// Mixin.write(self, offset, kind, value)
int l_mem_write(lua_State* L) {
    char* p = (char*)lua_tolightuserdata(L, 1) + luaL_checkinteger(L, 2);
    mem_store(L, luaL_checkstring(L, 3), p, 4);
    return 0;
}

int l_mem_read_str(lua_State* L) {
    const char* p = (const char*)lua_tolightuserdata(L, 1) + luaL_checkinteger(L, 2);
    int len = (int)luaL_checkinteger(L, 3);
    int n = 0;
    while (n < len && p[n]) n++;
    lua_pushlstring(L, p, n);
    return 1;
}

int l_mem_write_str(lua_State* L) {
    char* p = (char*)lua_tolightuserdata(L, 1) + luaL_checkinteger(L, 2);
    int len = (int)luaL_checkinteger(L, 3);
    size_t slen = 0;
    const char* s = luaL_checklstring(L, 4, &slen);
    int n = (int)slen;
    if (n > len - 1) n = len - 1;
    for (int i = 0; i < n; i++) p[i] = s[i];
    p[n] = 0;
    return 0;
}

// Mixin.arg(i, kind) -> the i-th argument of the hook currently running
int l_mixin_arg(lua_State* L) {
    int i = (int)luaL_checkinteger(L, 1);
    if (!s_cur_args) luaL_error(L, "Mixin.arg called outside a hook");
    mem_push(L, luaL_checkstring(L, 2), s_cur_args[i]);
    return 1;
}

// Mixin.set_arg(i, kind, value)
int l_mixin_set_arg(lua_State* L) {
    int i = (int)luaL_checkinteger(L, 1);
    if (!s_cur_args) luaL_error(L, "Mixin.set_arg called outside a hook");
    mem_store(L, luaL_checkstring(L, 2), s_cur_args[i], 3);
    return 0;
}

// Mixin.resolve(symbol) -> lightuserdata (address) or nil
int l_mixin_resolve(lua_State* L) {
    const char* sym = resolveName(luaL_checkstring(L, 1));
    void* p = (s_host && s_host->resolve) ? s_host->resolve(sym) : nullptr;
    if (p) lua_pushlightuserdata(L, p); else lua_pushnil(L);
    return 1;
}

// Mixin.cstr(s) -> lightuserdata (a persistent NUL-terminated copy of s)
int l_mixin_cstr(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    char* buf = (char*)(s_host && s_host->alloc ? s_host->alloc((uint32_t)(n + 1)) : nullptr);
    if (!buf) { lua_pushnil(L); return 1; }
    memcpy(buf, s, n);
    buf[n] = 0;
    lua_pushlightuserdata(L, buf);
    return 1;
}

// Log.info / Log.warn / Log.err
int l_log(lua_State* L) {
    const char* tag = luaL_checkstring(L, 1);
    const char* msg = luaL_checkstring(L, 2);
    if (s_host) s_host->log(tag, msg);
    return 0;
}

// ---------------------------------------------------------------------------
// Builder metatable  (returned by Mixin.builder(sym))
// ---------------------------------------------------------------------------
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
    lua_pushvalue(L, 1); // return self for chaining
    return 1;
}

int l_builder_before  (lua_State* L) { return l_builder_phase(L, "before");  }
int l_builder_after   (lua_State* L) { return l_builder_phase(L, "after");   }
int l_builder_replace (lua_State* L) { return l_builder_phase(L, "replace"); }

int l_builder_register(lua_State* L) {
    Builder* b = (Builder*)luaL_checkudata(L, 1, "PetrichorBuilder");
    for (auto& e : b->entries) {
        // Re-use l_mixin_register logic inline
        auto cb = [](PetrichorMixinCtx* ctx, void* modctx) {
            lua_State* L2 = s_L;
            int ref2 = (int)(intptr_t)modctx;
            lua_rawgeti(L2, LUA_REGISTRYINDEX, ref2);
            lua_newtable(L2);
            lua_pushlightuserdata(L2, ctx->self);
            lua_setfield(L2, -2, "self");
            lua_pushlightuserdata(L2, ctx->ret);
            lua_setfield(L2, -2, "ret");
            lua_pushboolean(L2, ctx->cancelled);
            lua_setfield(L2, -2, "cancelled");
            void** saved = s_cur_args;
            s_cur_args = ctx->args;
            if (lua_pcall(L2, 1, 0, 0) != LUA_OK) {
                const char* err = lua_tostring(L2, -1);
                s_host->log("luau", err ? err : "unknown error in mixin callback");
                lua_pop(L2, 1);
            }
            s_cur_args = saved;
        };
        const char* tag = e.tag.empty() ? nullptr : e.tag.c_str();
        if      (e.phase == "before")  s_host->mixin->before (b->sym.c_str(), cb, (void*)(intptr_t)e.ref, e.priority, tag);
        else if (e.phase == "after")   s_host->mixin->after  (b->sym.c_str(), cb, (void*)(intptr_t)e.ref, e.priority, tag);
        else if (e.phase == "replace") s_host->mixin->replace(b->sym.c_str(), cb, (void*)(intptr_t)e.ref, e.priority, tag);
    }
    return 0;
}

int l_builder_gc(lua_State* L) {
    Builder* b = (Builder*)luaL_checkudata(L, 1, "PetrichorBuilder");
    b->~Builder();
    return 0;
}

// Mixin.builder(symbol) -> Builder
int l_mixin_builder(lua_State* L) {
    const char* sym = luaL_checkstring(L, 1);
    Builder* b = (Builder*)lua_newuserdata(L, sizeof(Builder));
    new (b) Builder();
    b->sym = sym;
    if (luaL_newmetatable(L, "PetrichorBuilder")) {
        lua_newtable(L);
        lua_pushcfunction(L, l_builder_before,   "before");   lua_setfield(L, -2, "before");
        lua_pushcfunction(L, l_builder_after,    "after");    lua_setfield(L, -2, "after");
        lua_pushcfunction(L, l_builder_replace,  "replace");  lua_setfield(L, -2, "replace");
        lua_pushcfunction(L, l_builder_register, "register"); lua_setfield(L, -2, "register");
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, l_builder_gc, "__gc");
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    return 1;
}

// ---------------------------------------------------------------------------
// Augment._native reflect + hook primitives
// ---------------------------------------------------------------------------

const PetrichorReflectApi* RX() { return s_host ? s_host->reflect : nullptr; }

int l_n_fn_count(lua_State* L) {
    lua_pushinteger(L, RX() ? RX()->fn_count(luaL_checkstring(L, 1)) : 0);
    return 1;
}

int l_n_fn_mangled(lua_State* L) {
    const char* m = RX() ? RX()->fn_mangled(luaL_checkstring(L, 1), (int)luaL_checkinteger(L, 2)) : nullptr;
    if (m) lua_pushstring(L, m); else lua_pushnil(L);
    return 1;
}

int l_n_fn_loc(lua_State* L) {
    const char* s = RX() ? RX()->fn_loc(luaL_checkstring(L, 1), (int)luaL_checkinteger(L, 2)) : "";
    lua_pushstring(L, s ? s : "");
    return 1;
}

int l_n_resolve_at(lua_State* L) {
    const char* m = RX() ? RX()->resolve_at(luaL_checkstring(L, 1), luaL_checkstring(L, 2)) : nullptr;
    if (m) lua_pushstring(L, m); else lua_pushnil(L);
    return 1;
}

int l_n_resolve_sig(lua_State* L) {
    const char* m = RX() ? RX()->resolve_sig(luaL_checkstring(L, 1), luaL_checkstring(L, 2)) : nullptr;
    if (m) lua_pushstring(L, m); else lua_pushnil(L);
    return 1;
}

int l_n_self_view(lua_State* L) {
    const char* s = RX() ? RX()->fn_self_view(luaL_checkstring(L, 1)) : "";
    lua_pushstring(L, s ? s : "");
    return 1;
}

int l_n_ret(lua_State* L) {
    const char* s = RX() ? RX()->fn_ret(luaL_checkstring(L, 1)) : "void";
    lua_pushstring(L, s ? s : "void");
    return 1;
}

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

    auto cb = [](PetrichorMixinCtx* ctx, void* modctx) {
        lua_State* L2 = s_L;
        lua_rawgeti(L2, LUA_REGISTRYINDEX, (int)(intptr_t)modctx);
        lua_newtable(L2);
        lua_pushlightuserdata(L2, ctx->self);  lua_setfield(L2, -2, "self");
        lua_pushlightuserdata(L2, ctx->ret);   lua_setfield(L2, -2, "ret");
        lua_pushboolean(L2, ctx->cancelled);   lua_setfield(L2, -2, "cancelled");
        void** sa = s_cur_args; void* sr = s_cur_ret;
        s_cur_args = ctx->args; s_cur_ret = ctx->ret;
        if (lua_pcall(L2, 1, 1, 0) != LUA_OK) {
            const char* err = lua_tostring(L2, -1);
            s_host->log("luau", err ? err : "hook error");
            lua_pop(L2, 1);
        } else {
            ctx->cancelled = (uint8_t)lua_toboolean(L2, -1);
            lua_pop(L2, 1);
        }
        s_cur_args = sa; s_cur_ret = sr;
    };

    if (!s_host || !s_host->mixin) { lua_pushboolean(L, 0); return 1; }
    void* mc = (void*)(intptr_t)ref;
    uint8_t ok = 0;
    if      (!strcmp(phase, "before"))  ok = s_host->mixin->before (mangled, cb, mc, 0, nullptr);
    else if (!strcmp(phase, "after"))   ok = s_host->mixin->after  (mangled, cb, mc, 0, nullptr);
    else if (!strcmp(phase, "replace")) ok = s_host->mixin->replace(mangled, cb, mc, 0, nullptr);
    else luaL_error(L, "unknown phase '%s'", phase);
    lua_pushboolean(L, ok);
    return 1;
}

int l_require_hook(lua_State* L) {
    size_t bc = 0;
    char* code = luau_compile(PETRICHOR_HOOK_RUNTIME, strlen(PETRICHOR_HOOK_RUNTIME), nullptr, &bc);
    int rc = luau_load(L, "Augment.Hook", code, bc, 0);
    free(code);
    if (rc != LUA_OK) luaL_error(L, "Augment.Hook compile: %s", lua_tostring(L, -1));
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) luaL_error(L, "Augment.Hook load: %s", lua_tostring(L, -1));
    return 1;
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

// ---------------------------------------------------------------------------
// Module loader  (require("Augment.Mixin") etc.)
// ---------------------------------------------------------------------------
int l_require_mixin(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_mixin_register, "register"); lua_setfield(L, -2, "register");
    lua_pushcfunction(L, l_mixin_builder,  "builder");  lua_setfield(L, -2, "builder");
    lua_pushcfunction(L, l_mixin_call,     "call");     lua_setfield(L, -2, "call");
    lua_pushcfunction(L, l_mem_read,       "read");     lua_setfield(L, -2, "read");
    lua_pushcfunction(L, l_mem_write,      "write");    lua_setfield(L, -2, "write");
    lua_pushcfunction(L, l_mem_read_str,   "read_str"); lua_setfield(L, -2, "read_str");
    lua_pushcfunction(L, l_mem_write_str,  "write_str");lua_setfield(L, -2, "write_str");
    lua_pushcfunction(L, l_mixin_arg,      "arg");      lua_setfield(L, -2, "arg");
    lua_pushcfunction(L, l_mixin_set_arg,  "set_arg");  lua_setfield(L, -2, "set_arg");
    lua_pushcfunction(L, l_mixin_resolve,  "resolve");  lua_setfield(L, -2, "resolve");
    lua_pushcfunction(L, l_mixin_cstr,     "cstr");     lua_setfield(L, -2, "cstr");
    return 1;
}

int l_require_log(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_log, "log"); lua_setfield(L, -2, "info");
    lua_pushcfunction(L, l_log, "log"); lua_setfield(L, -2, "warn");
    lua_pushcfunction(L, l_log, "log"); lua_setfield(L, -2, "err");
    return 1;
}

// Custom require that resolves built-in petrichor modules then falls back
// to file-based loading from the mod directory.
std::string s_mod_dir;

int l_require(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);

    lua_getfield(L, LUA_REGISTRYINDEX, "_petrichor_modcache");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "_petrichor_modcache");
    }
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);

    if (strcmp(name, "Augment._native") == 0) {
        l_require_native(L);
    } else if (strcmp(name, "Augment.Hook") == 0) {
        l_require_hook(L);
    } else if (strcmp(name, "Augment.Mixin") == 0) {
        l_require_mixin(L);
    } else if (strcmp(name, "Petrichor.Log") == 0) {
        l_require_log(L);
    } else {
        std::string path = s_mod_dir + "/";
        for (const char* p = name; *p; p++)
            path += (*p == '.') ? '/' : *p;
        path += ".luau";

        FILE* f = fopen(path.c_str(), "rb");
        if (!f) luaL_error(L, "module '%s' not found (%s)", name, path.c_str());
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string src(sz, '\0');
        fread(&src[0], 1, sz, f);
        fclose(f);

        size_t bcSize = 0;
        char* bc = luau_compile(src.c_str(), src.size(), nullptr, &bcSize);
        int rc = luau_load(L, name, bc, bcSize, 0);
        free(bc);
        if (rc != LUA_OK) luaL_error(L, "compile '%s': %s", name, lua_tostring(L, -1));
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) luaL_error(L, "load '%s': %s", name, lua_tostring(L, -1));
    }

    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name);
    return 1;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
namespace petrichor {

bool luau_boot(const PetrichorHost* host) {
    s_host = host;
    s_L = luaL_newstate();
    if (!s_L) return false;

    luaL_openlibs(s_L);

    lua_pushcfunction(s_L, l_require, "require");
    lua_setglobal(s_L, "require");

    plog("luau", "Luau host started");
    return true;
}

bool luau_loadMod(const char* dir, const char* id, const char* type, const char* entry) {
    if (!s_L) return false;

    s_mod_dir = dir;

    std::string entryFile = std::string(dir) + "/" + (entry && entry[0] ? entry : "main.luau");

    FILE* f = fopen(entryFile.c_str(), "rb");
    if (!f) {
        plog("luau", "entry not found: %s", entryFile.c_str());
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string src(sz, '\0');
    fread(&src[0], 1, sz, f);
    fclose(f);

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(src.c_str(), src.size(), nullptr, &bytecodeSize);
    int rc = luau_load(s_L, id, bytecode, bytecodeSize, 0);
    free(bytecode);

    if (rc != LUA_OK) {
        plog("luau", "compile error in %s: %s", id, lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return false;
    }

    if (lua_pcall(s_L, 0, 1, 0) != LUA_OK) {
        plog("luau", "runtime error in %s: %s", id, lua_tostring(s_L, -1));
        lua_pop(s_L, 1);
        return false;
    }

    lua_pop(s_L, 1); // pop returned mod table
    plog("luau", "loaded mod: %s", id);
    return true;
}

void luau_stop() {
    if (s_L) {
        lua_close(s_L);
        s_L = nullptr;
    }
    s_host = nullptr;
}

} // namespace petrichor