#pragma once
#include <string>
#include "lua.h"

namespace petrichor::net {
    void boot();
    void stop();
    void tick();
    void cancel_inflight_for_mod(const std::string& mod_id);
    int  require_module(lua_State* L, const std::string& mod_id);
}