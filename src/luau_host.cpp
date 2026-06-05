#include "internal.hpp"

#include <lua.h>
#include <lualib.h>
#include <luacode.h>  // luau_compile

#include <cstring>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
namespace {

const PetrichorHost* s_host = nullptr;
lua_State* s_L = nullptr;

// ---------------------------------------------------------------------------
// Host bindings exposed to Luau
// ---------------------------------------------------------------------------

// Mixin.register(symbol, phase, fn, priority, tag)
// phase: "before" | "after" | "replace"
int l_mixin_register(lua_State* L) {
    const char* sym      = luaL_checkstring(L, 1);
    const char* phase    = luaL_checkstring(L, 2);
    int         priority = (int)luaL_optinteger(L, 4, 0);
    const char* tag      = luaL_optstring(L, 5, nullptr);

    luaL_checktype(L, 3, LUA_TFUNCTION);

    // Store fn in registry so we can pass a stable pointer
    lua_pushvalue(L, 3);
    int ref = lua_ref(L, LUA_REGISTRYINDEX);

    // Callback that fires the stored Luau function
    auto cb = [](PetrichorMixinCtx* ctx, void* modctx) {
        lua_State* L2 = s_L;
        int ref2 = (int)(intptr_t)modctx;
        lua_rawgeti(L2, LUA_REGISTRYINDEX, ref2);

        // Push ctx table
        lua_newtable(L2);
        lua_pushlightuserdata(L2, ctx->self);
        lua_setfield(L2, -2, "self");
        lua_pushinteger(L2, ctx->ret);
        lua_setfield(L2, -2, "ret");
        lua_pushboolean(L2, ctx->cancelled);
        lua_setfield(L2, -2, "cancelled");

        if (lua_pcall(L2, 1, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(L2, -1);
            s_host->log("luau", err ? err : "unknown error in mixin callback");
            lua_pop(L2, 1);
        }
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

// Mixin.call(symbol, args_table) -> any
int l_mixin_call(lua_State* L) {
    const char* sym = luaL_checkstring(L, 1);
    if (!s_host || !s_host->resolve) {
        lua_pushnil(L);
        return 1;
    }
    // For now just resolve and return the pointer as a light userdata;
    // actual typed dispatch is handled by the generated call() wrappers.
    void* fn = s_host->resolve(sym);
    if (!fn) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlightuserdata(L, fn);
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
    lua_pushvalue(L, 2);
    int ref = lua_ref(L, LUA_REGISTRYINDEX);
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
            lua_pushinteger(L2, ctx->ret);
            lua_setfield(L2, -2, "ret");
            lua_pushboolean(L2, ctx->cancelled);
            lua_setfield(L2, -2, "cancelled");
            if (lua_pcall(L2, 1, 0, 0) != LUA_OK) {
                const char* err = lua_tostring(L2, -1);
                s_host->log("luau", err ? err : "unknown error in mixin callback");
                lua_pop(L2, 1);
            }
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
// Module loader  (require("Augment.Mixin") etc.)
// ---------------------------------------------------------------------------
int l_require_mixin(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_mixin_register, "register"); lua_setfield(L, -2, "register");
    lua_pushcfunction(L, l_mixin_builder,  "builder");  lua_setfield(L, -2, "builder");
    lua_pushcfunction(L, l_mixin_call,     "call");     lua_setfield(L, -2, "call");
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

int l_loader_builtin(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    if (strcmp(name, "Augment.Mixin") == 0) { l_require_mixin(L); return 1; }
    if (strcmp(name, "Petrichor.Log") == 0)  { l_require_log(L);   return 1; }
    lua_pushnil(L);
    return 1;
}

int l_loader_file(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);

    // Convert "Foo.Bar.Baz" -> "Foo/Bar/Baz.luau"
    std::string path = s_mod_dir + "/";
    for (const char* p = name; *p; p++)
        path += (*p == '.') ? '/' : *p;
    path += ".luau";

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        lua_pushfstring(L, "\n\tno file '%s'", path.c_str());
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string src(sz, '\0');
    fread(&src[0], 1, sz, f);
    fclose(f);

    // Compile with Luau compiler
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(src.c_str(), src.size(), nullptr, &bytecodeSize);
    int rc = luau_load(L, name, bytecode, bytecodeSize, 0);
    free(bytecode);

    if (rc != LUA_OK) {
        // Return error string for require to report
        return 1; // error is already on stack
    }
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

    // Register loaders: builtin first, then file
    lua_getglobal(s_L, "package");
    lua_getfield(s_L, -1, "loaders");
    int n = (int)lua_objlen(s_L, -1);
    lua_pushcfunction(s_L, l_loader_builtin, "loader_builtin");
    lua_rawseti(s_L, -2, n + 1);
    lua_pushcfunction(s_L, l_loader_file, "loader_file");
    lua_rawseti(s_L, -2, n + 2);
    lua_pop(s_L, 2);

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