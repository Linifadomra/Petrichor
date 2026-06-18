#include "internal.hpp"
#include "petrichor/luau.h"
#include "petrichor/petrichor.h"

#include "prelude_standard_inc.h"
#include "prelude_petrichor_mod_inc.h"
#include "prelude_math_inc.h"
#include "prelude_timer_inc.h"
#include "prelude_json_inc.h"
#include "prelude_random_inc.h"
#include "prelude_hash_inc.h"
#include "prelude_bits_inc.h"
#include "prelude_events_inc.h"
#include "prelude_async_inc.h"

#include "backends/luau/bindings/async.hpp"
#include "backends/luau/bindings/net.hpp"
#include "backends/luau/bindings/storage.hpp"

#include <filesystem>
#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <cstring>
#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <vector>

static const struct { const char* name; const unsigned char* src; unsigned int len; } s_prelude_libs[] = {
    { "Petrichor.Standard", standard_luau,      standard_luau_len      },
    { "Petrichor.Math",     math_luau,          math_luau_len          },
    { "Petrichor.Json",     json_luau,          json_luau_len          },
    { "Petrichor.Random",   random_luau,        random_luau_len        },
    { "Petrichor.Hash",     hash_luau,          hash_luau_len          },
    { "Petrichor.Bits",     bits_luau,          bits_luau_len          },
    { "Petrichor.Mod",      petrichor_mod_luau, petrichor_mod_luau_len },
    { "Petrichor.Events",   events_luau,        events_luau_len        },
    { nullptr, nullptr, 0 }
};

namespace {

IPetrichorHost* s_host = nullptr;
lua_State*      s_L    = nullptr;

struct LuauModInstance {
    std::string id;
    int         ref;
    std::string dir;
    std::string entry;
    std::string type;
    std::string store_root;
    bool        dirty;
    std::filesystem::file_time_type last_modified;

    PetrichorManifest manifest() const {
        PetrichorManifest m{};
        std::strncpy(m.id,    id.c_str(),    sizeof(m.id)    - 1);
        std::strncpy(m.name,  id.c_str(),    sizeof(m.name)  - 1);
        std::strncpy(m.type,  type.c_str(),  sizeof(m.type)  - 1);
        std::strncpy(m.entry, entry.c_str(), sizeof(m.entry) - 1);
        m.apiVersion = PETRICHOR_API_VERSION;
        return m;
    }
};

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
    return 1;
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
        petrichor::plog(PetrichorLogLevel::Error, "luau", "exception in %s: %s", ctx, e.what());
    } catch (...) {
        petrichor::plog(PetrichorLogLevel::Error, "luau", "unknown exception in %s", ctx);
    }
    return false;
}

} // namespace

int l_log_level(lua_State* L, PetrichorLogLevel level) {
    const char* tag = luaL_checkstring(L, 1);
    const char* msg = luaL_checkstring(L, 2);
    if (!s_host) return 0;
    petrichor::plog(level, tag, "%s", msg);
    return 0;
}

int l_log_info (lua_State* L) { return l_log_level(L, PetrichorLogLevel::Info);  }
int l_log_warn (lua_State* L) { return l_log_level(L, PetrichorLogLevel::Warn);  }
int l_log_err  (lua_State* L) { return l_log_level(L, PetrichorLogLevel::Error); }
int l_log_debug(lua_State* L) { return l_log_level(L, PetrichorLogLevel::Debug); }

int l_require_log(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, l_log_info,  "info");  lua_setfield(L, -2, "info");
    lua_pushcfunction(L, l_log_warn,  "warn");  lua_setfield(L, -2, "warn");
    lua_pushcfunction(L, l_log_err,   "err");   lua_setfield(L, -2, "err");
    lua_pushcfunction(L, l_log_debug, "debug"); lua_setfield(L, -2, "debug");
    return 1;
}

/* ASYNC */

int l_async_defer(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    petrichor::async::schedule_step(lua_ref(L, 1));
    return 0;
}

int l_async_timer(lua_State* L) {
    float secs = (float)luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    petrichor::async::schedule_timer(secs, lua_ref(L, 2));
    return 0;
}

int l_require_petrichor_async(lua_State* L) {
    int msgh = push_msgh(L);

    lua_newtable(L);
    auto set = [&](const char* k, lua_CFunction f) {
        lua_pushcfunction(L, f, k); lua_setfield(L, -2, k);
    };
    set("defer", l_async_defer);
    set("timer", l_async_timer);

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

static int l_require_lua_chunk(lua_State* L, const char* name, const unsigned char* src, unsigned int len) {
    if (!petrichor::luau_load_module(L, name, src, len))
        luaL_error(L, "%s: failed to load", name);
    return 1;
}

int l_require_timer(lua_State* L) { return l_require_lua_chunk(L, "Petrichor.Timer", timer_luau, timer_luau_len); }

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

lua_State* lua_state() { return s_L; }

void luau_register_module(const char* name, ModuleFactory factory) {
    if (name && factory) s_module_registry[name] = factory;
}

bool luau_load_module(lua_State* L, const char* name, const unsigned char* src, unsigned int len) {
    size_t bc = 0;
    char* bytecode = luau_compile((const char*)src, len, nullptr, &bc);
    int rc = luau_load(L, name, bytecode, bc, 0);
    free(bytecode);
    if (rc != LUA_OK) {
        plog(PetrichorLogLevel::Error, "luau", "compile error in '%s': %s", name, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        plog(PetrichorLogLevel::Error, "luau", "load error in '%s': %s", name, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool luau_boot(IPetrichorHost& host) {
    s_host = &host;

    s_L = luaL_newstate();
    if (!s_L) return false;

    luaL_openlibs(s_L);

    lua_pushcfunction(s_L, l_require, "require");
    lua_setglobal(s_L, "require");

    s_module_registry["Petrichor.Log"]   = l_require_log;
    s_module_registry["Petrichor.Task"]  = l_require_petrichor_async;
    s_module_registry["Petrichor.Timer"] = l_require_timer;
    s_module_registry["Petrichor.Net"]   = petrichor::net::require_module;
    petrichor::net::boot();

    for (auto* e = s_prelude_libs; e->name; e++)
        run_prelude(s_L, e->name, e->src, e->len);

    plog(PetrichorLogLevel::Info, "luau", "Luau host started");
    return true;
}

bool luau_loadMod(const char* dir, const PetrichorManifest& m, const char* store_root) {
    if (!s_L) return false;

    const char* id    = m.id;
    const char* type  = m.type;
    const char* entry = m.entry[0] ? m.entry : "main.luau";

    const int base = lua_gettop(s_L);

    bool known = false;
    for (const auto& d : s_mod_dirs)
        if (d == dir) { known = true; break; }
    if (!known) s_mod_dirs.emplace_back(dir);

    const std::string entryFile = std::string(dir) + "/" + entry;
    FILE* f = fopen(entryFile.c_str(), "rb");
    if (!f) {
        plog(PetrichorLogLevel::Error, "luau", "entry not found: %s", entryFile.c_str());
        return false;
    }

    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        plog(PetrichorLogLevel::Error, "luau", "cannot read %s", entryFile.c_str());
        return false;
    }
    std::string src(static_cast<size_t>(sz), '\0');
    src.resize(fread(&src[0], 1, static_cast<size_t>(sz), f));
    fclose(f);

    // Inject per-mod Storage into module cache before running entry
    {
        lua_getfield(s_L, LUA_REGISTRYINDEX, "_petrichor_modcache");
        if (!lua_istable(s_L, -1)) {
            lua_pop(s_L, 1);
            lua_newtable(s_L);
            lua_pushvalue(s_L, -1);
            lua_setfield(s_L, LUA_REGISTRYINDEX, "_petrichor_modcache");
        }
        petrichor::storage::require_module(s_L, id, dir, store_root ? store_root : "");
        lua_setfield(s_L, -2, "Petrichor.Storage");
        lua_pop(s_L, 1);
    }

    int msgh = push_msgh(s_L);

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(src.c_str(), src.size(), nullptr, &bytecodeSize);
    const int rc = luau_load(s_L, id, bytecode, bytecodeSize, 0);
    free(bytecode);

    if (rc != LUA_OK) {
        plog(PetrichorLogLevel::Error, "luau", "compile error in %s: %s", id, lua_tostring(s_L, -1));
        lua_settop(s_L, base);
        return false;
    }
    if (lua_pcall(s_L, 0, 1, msgh) != LUA_OK) {
        plog(PetrichorLogLevel::Error, "luau", "runtime error in %s: %s", id, lua_tostring(s_L, -1));
        lua_settop(s_L, base);
        return false;
    }
    lua_remove(s_L, msgh);

    if (!lua_istable(s_L, -1)) {
        plog(PetrichorLogLevel::Error, "luau", "mod '%s' did not return a class table; skipping. Did you forget to return your class table?", id);
        lua_settop(s_L, base);
        return false;
    }

    static const char* const required[] = { "tick", "shutdown", nullptr };
    for (const char* const* method = required; *method; ++method) {
        lua_getfield(s_L, -1, *method);
        const bool ok = lua_isfunction(s_L, -1);
        lua_pop(s_L, 1);
        if (!ok) {
            plog(PetrichorLogLevel::Error, "luau", "mod '%s' missing required method '%s'; skipping. Please prefer mod(\"Name\") to raw metatables.", id, *method);
            lua_settop(s_L, base);
            return false;
        }
    }

    msgh = push_msgh(s_L);
    lua_getfield(s_L, -2, "new");
    if (!lua_isfunction(s_L, -1)) {
        plog(PetrichorLogLevel::Error, "luau", "mod '%s' class has no .new(); skipping. Please prefer mod(\"Name\") to raw metatables.", id);
        lua_settop(s_L, base);
        return false;
    }
    if (lua_pcall(s_L, 0, 1, msgh) != LUA_OK) {
        plog(PetrichorLogLevel::Error, "luau", "mod '%s' new() failed: %s", id, lua_tostring(s_L, -1));
        lua_settop(s_L, base);
        return false;
    }
    lua_remove(s_L, msgh);
    lua_remove(s_L, -2);

    const int ref = lua_ref(s_L, -1);
    lua_pop(s_L, 1);

    std::error_code ec;
    s_mods.push_back({
        std::string(id),
        ref,
        std::string(dir),
        std::string(entry),
        std::string(type),
        store_root ? std::string(store_root) : "",
        false,
        std::filesystem::last_write_time(entryFile, ec)
    });

    plog(PetrichorLogLevel::Info, "luau", "loaded mod: %s", id);
    return true;
}

bool luau_unloadMod(const char* id) {
    if (!s_L) return false;
    auto it = std::find_if(s_mods.begin(), s_mods.end(),
        [id](const LuauModInstance& m) { return m.id == id; });
    if (it == s_mods.end()) return false;

    luau_protected(it->id.c_str(), [&] {
        lua_rawgeti(s_L, LUA_REGISTRYINDEX, it->ref);
        int msgh = push_msgh(s_L);

        lua_getfield(s_L, -2, "save");
        if (lua_isfunction(s_L, -1)) {
            lua_pushvalue(s_L, -3);
            if (lua_pcall(s_L, 1, 0, msgh) != LUA_OK) lua_pop(s_L, 1);
        } else lua_pop(s_L, 1);

        lua_getfield(s_L, -2, "shutdown");
        lua_pushvalue(s_L, -3);
        if (lua_pcall(s_L, 1, 0, msgh) != LUA_OK) {
            plog(PetrichorLogLevel::Error, "luau", "shutdown error in %s: %s",
                it->id.c_str(), lua_tostring(s_L, -1));
            lua_pop(s_L, 1);
        }
        lua_pop(s_L, 2);
    });

    lua_unref(s_L, it->ref);
    s_mods.erase(it);
    plog(PetrichorLogLevel::Info, id, "mod unloaded.");
    return true;
}

bool luau_reloadMod(const char* id) {
    const auto it = std::find_if(s_mods.begin(), s_mods.end(),
        [id](const LuauModInstance& m) { return m.id == id; });
    if (it == s_mods.end()) return false;

    const std::string       dir        = it->dir;
    const std::string       store_root = it->store_root;
    const PetrichorManifest m          = it->manifest();

    if (!luau_unloadMod(id)) return false;
    return luau_loadMod(dir.c_str(), m, store_root.c_str());
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
                plog(PetrichorLogLevel::Error, "luau", "tick error in %s: %s", mod.id.c_str(), lua_tostring(s_L, -1));
                lua_pop(s_L, 1);
            }
            lua_pop(s_L, 2);
        });
    }
}

void luau_fire_event(const char* event, const char* json_payload) {
    if (!s_L || !event || !event[0]) return;

    luau_protected("fire_event", [&] {
        const int base = lua_gettop(s_L);
        int msgh = push_msgh(s_L);

        lua_getglobal(s_L, "require");
        lua_pushstring(s_L, "Petrichor.Events");
        if (lua_pcall(s_L, 1, 1, msgh) != LUA_OK) {
            plog(PetrichorLogLevel::Error, "luau", "fire_event: require Events failed: %s. Please inform Petrichor developers.", lua_tostring(s_L, -1));
            lua_settop(s_L, base);
            return;
        }

        lua_getfield(s_L, -1, "fire");
        lua_remove(s_L, -2);
        lua_pushstring(s_L, event);

        if (json_payload && json_payload[0]) {
            lua_getglobal(s_L, "require");
            lua_pushstring(s_L, "Petrichor.Json");
            if (lua_pcall(s_L, 1, 1, msgh) != LUA_OK) {
                plog(PetrichorLogLevel::Error, "luau", "fire_event: require Json failed: %s. Please inform Petrichor developers.", lua_tostring(s_L, -1));
                lua_settop(s_L, base);
                return;
            }
            lua_getfield(s_L, -1, "decode");
            lua_remove(s_L, -2);
            lua_pushstring(s_L, json_payload);
            if (lua_pcall(s_L, 1, 1, msgh) != LUA_OK) {
                plog(PetrichorLogLevel::Error, "luau", "fire_event: json decode failed: %s", lua_tostring(s_L, -1));
                lua_settop(s_L, base);
                return;
            }
        } else {
            lua_pushnil(s_L);
        }

        if (lua_pcall(s_L, 2, 0, msgh) != LUA_OK)
            plog(PetrichorLogLevel::Error, "luau", "fire_event '%s' failed: %s", event, lua_tostring(s_L, -1));

        lua_settop(s_L, base);
    });
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
                if (lua_pcall(s_L, 1, 0, msgh) != LUA_OK) lua_pop(s_L, 1);
            } else {
                lua_pop(s_L, 1);
            }

            lua_getfield(s_L, -2, "shutdown");
            lua_pushvalue(s_L, -3);
            if (lua_pcall(s_L, 1, 0, msgh) != LUA_OK) {
                plog(PetrichorLogLevel::Error, "luau", "shutdown error in %s: %s", mod.id.c_str(), lua_tostring(s_L, -1));
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
    s_L     = nullptr;
    s_host  = nullptr;
    s_mod_dirs.clear();
}

std::vector<std::string> luau_poll_changes() {
    std::vector<std::string> changed;
    for (auto& mod : s_mods) {
        const std::string entryFile = mod.dir + "/" + mod.entry;
        std::error_code ec;
        auto mtime = std::filesystem::last_write_time(entryFile, ec);
        if (ec) continue;

        if (mod.dirty) {
            mod.dirty = false;
            changed.push_back(mod.id);
            continue;
        }

        if (mtime != mod.last_modified) {
            mod.last_modified = mtime;
            mod.dirty = true;
        }
    }
    return changed;
}

} // namespace petrichor
