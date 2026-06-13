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

int l_asset_path(lua_State* L) {
    const char* rel = luaL_checkstring(L, 1);
    auto* ctx = get_ctx(L);
    std::string path = ctx->mod_dir + "/assets/" + rel;
    lua_pushlstring(L, path.c_str(), path.size());
    return 1;
}

int l_write_asset(lua_State* L) {
    const char* rel = luaL_checkstring(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    auto* ctx = get_ctx(L);
    std::string path = ctx->mod_dir + "/assets/" + rel;
    std::error_code ec;
    fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        lua_pushboolean(L, 0);
        return 1;
    }
    f.write(data, (std::streamsize)len);
    lua_pushboolean(L, 1);
    return 1;
}

int l_list_assets(lua_State* L) {
    const char* rel = luaL_optstring(L, 1, "");
    auto* ctx = get_ctx(L);
    fs::path dir = fs::path(ctx->mod_dir) / "assets";
    if (rel && rel[0] != '\0') {
        dir /= rel;
    }
    lua_newtable(L);
    int index = 1;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return 1;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        lua_pushstring(L, entry.path().filename().string().c_str());
        lua_rawseti(L, -2, index++);
    }
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
    set("write_asset", l_write_asset);
    set("list_assets", l_list_assets);
    set("asset_path", l_asset_path);

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