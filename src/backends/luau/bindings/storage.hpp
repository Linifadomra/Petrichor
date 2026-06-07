#pragma once
#include "lua.h"
#include <string>

namespace petrichor::storage {
    int require_module(lua_State* L, const std::string& mod_id, const std::string& mod_dir, const std::string& store_root);
}