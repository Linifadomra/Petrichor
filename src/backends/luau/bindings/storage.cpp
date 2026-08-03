/*
 * Petrichor.Storage Luau library.
 * Copyright (C) 2026 Linifadomra Org.
 *
 * This file is part of Petrichor.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
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

bool isWithin(const fs::path& base, const fs::path& resolved) {
    const std::string b = base.string();
    const std::string r = resolved.string();
    if (r.size() < b.size() || r.compare(0, b.size(), b) != 0) return false;
    return r.size() == b.size() || r[b.size()] == fs::path::preferred_separator;
}

StorageCtx* get_ctx(lua_State* L) {
    return (StorageCtx*)lua_touserdata(L, lua_upvalueindex(1));
}

int l_read_file(lua_State* L) {
    const char* name = luaL_optstring(L, 1, "store.json");
    auto* ctx = get_ctx(L);

    const fs::path base = (fs::path(ctx->store_root) / ctx->mod_id).lexically_normal();
    const fs::path resolved = (fs::path(ctx->store_root) / ctx->mod_id / name).lexically_normal();

    if (!isWithin(base, resolved)) {
        lua_pushnil(L); return 1;
    }

    std::ifstream f(resolved, std::ios::binary);
    if (!f) { lua_pushnil(L); return 1; }
    std::ostringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    lua_pushlstring(L, s.c_str(), s.size());
    return 1;
}

int l_write_file(lua_State* L) {
    const char* name = luaL_optstring(L, 1, "store.json");
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    auto* ctx = get_ctx(L);

    const fs::path base = (fs::path(ctx->store_root) / ctx->mod_id).lexically_normal();
    const fs::path resolved = (fs::path(ctx->store_root) / ctx->mod_id / name).lexically_normal();

    if (!isWithin(base, resolved)) {
        lua_pushboolean(L, 0); return 1;
    }

    std::error_code ec;
    fs::create_directories(resolved.parent_path(), ec);
    std::ofstream f(resolved, std::ios::binary);
    if (!f) { lua_pushboolean(L, 0); return 1; }
    f.write(data, (std::streamsize)len);
    lua_pushboolean(L, 1); return 1;
}

int l_file_path(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    auto* ctx = get_ctx(L);

    const fs::path base = (fs::path(ctx->store_root) / ctx->mod_id).lexically_normal();
    const fs::path resolved = (fs::path(ctx->store_root) / ctx->mod_id / name).lexically_normal();

    if (!isWithin(base, resolved)) {
        lua_pushnil(L); return 1;
    }

    const std::string s = resolved.string();
    lua_pushlstring(L, s.c_str(), s.size());
    return 1;
}

int l_read_asset(lua_State* L) {
    const char* rel = luaL_checkstring(L, 1);
    auto* ctx = get_ctx(L);

    const fs::path base = (fs::path(ctx->mod_dir) / "assets").lexically_normal();
    const fs::path resolved = (fs::path(ctx->mod_dir) / "assets" / rel).lexically_normal();
    if (!isWithin(base, resolved)) {
        lua_pushnil(L); return 1;
    }

    std::ifstream f(resolved, std::ios::binary);
    if (!f) { lua_pushnil(L); return 1; }
    std::ostringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    lua_pushlstring(L, s.c_str(), s.size());
    return 1;
}

int l_asset_path(lua_State* L) {
    const char* rel = luaL_checkstring(L, 1);
    auto* ctx = get_ctx(L);

    const fs::path base = (fs::path(ctx->mod_dir) / "assets").lexically_normal();
    const fs::path resolved = (fs::path(ctx->mod_dir) / "assets" / rel).lexically_normal();
    if (!isWithin(base, resolved)) {
        lua_pushnil(L); return 1;
    }

    const std::string s = resolved.string();
    lua_pushlstring(L, s.c_str(), s.size());
    return 1;
}

int l_list_assets(lua_State* L) {
    const char* rel = luaL_optstring(L, 1, "");
    auto* ctx = get_ctx(L);

    const fs::path base = (fs::path(ctx->mod_dir) / "assets").lexically_normal();
    fs::path dir = base;
    if (rel && rel[0] != '\0') {
        dir = (base / rel).lexically_normal();
    }

    lua_newtable(L);
    if (!isWithin(base, dir)) {
        return 1;
    }
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
    set("read_file",   l_read_file);
    set("write_file",  l_write_file);
    set("read_asset",  l_read_asset);
    set("file_path",   l_file_path);
    set("asset_path",  l_asset_path);
    set("list_assets", l_list_assets);

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