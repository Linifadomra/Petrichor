#pragma once
#include "lua.h"

namespace petrichor::net {
    void boot();
    void stop();
    void tick(lua_State* L);
    int  require_module(lua_State* L);
}