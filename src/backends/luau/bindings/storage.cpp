#include "storage.hpp"
#include "internal.hpp"
#include "lualib.h"
#include "luacode.h"
#include "prelude_storage_inc.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

struct StorageCtx {
    std::string mod_id;
    std::string mod_dir;
    std::string store_root;
};

StorageCtx* get_ctx(lua_State* L) {
    return (StorageCtx*)lua_touserdata(L, lua_upvalueindex(1));
}

int l_read_file(lua_State* L) {
    auto* ctx = get_ctx(L);
    std::string path = ctx->store_root + "/" + ctx->mod_id + "/store.json";
    std::ifstream f(path, std::ios::binary);
    if (!f) { lua_pushnil(L); return 1; }
    std::ostringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    lua_pushlstring(L, s.c_str(), s.size());
    return 1;
}

int l_write_file(lua_State* L) {
    size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    auto* ctx = get_ctx(L);
    std::string dir  = ctx->store_root + "/" + ctx->mod_id;
    std::string path = dir + "/store.json";
    petrichor::plog("storage", "writing to: %s", path.c_str());
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream f(path, std::ios::binary);
    if (f) f.write(data, (std::streamsize)len);
    return 0;
}

int l_read_asset(lua_State* L) {
    const char* rel = luaL_checkstring(L, 1);
    auto* ctx = get_ctx(L);
    std::string path = ctx->mod_dir + "/assets/" + rel;
    std::ifstream f(path, std::ios::binary);
    if (!f) { lua_pushnil(L); return 1; }
    std::ostringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    lua_pushlstring(L, s.c_str(), s.size());
    return 1;
}

} // namespace

namespace petrichor::storage {

int require_module(lua_State* L, const std::string& mod_id, const std::string& mod_dir, const std::string& store_root) {
    auto* ctx = (StorageCtx*)lua_newuserdatadtor(L, sizeof(StorageCtx),
        [](void* p) { static_cast<StorageCtx*>(p)->~StorageCtx(); });
    new (ctx) StorageCtx{ mod_id, mod_dir, store_root };

    lua_newtable(L);
    int table_idx = lua_gettop(L);
    int ud_idx = table_idx - 1;

    auto set = [&](const char* k, lua_CFunction f) {
        lua_pushvalue(L, ud_idx);
        lua_pushcclosure(L, f, k, 1);
        lua_setfield(L, table_idx, k);
    };
    set("read_file",  l_read_file);
    set("write_file", l_write_file);
    set("read_asset", l_read_asset);

    size_t bc = 0;
    char* bytecode = luau_compile((const char*)storage_luau, storage_luau_len, nullptr, &bc);
    int rc = luau_load(L, "Petrichor.Storage", bytecode, bc, 0);
    free(bytecode);
    if (rc != LUA_OK) luaL_error(L, "Petrichor.Storage: %s", lua_tostring(L, -1));

    lua_insert(L, -2);

    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
        luaL_error(L, "Petrichor.Storage: %s", lua_tostring(L, -1));
    lua_remove(L, -2);

    return 1;
}

} // namespace petrichor::storage