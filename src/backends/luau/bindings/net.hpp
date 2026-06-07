#pragma once
#include "lua.h"

namespace petrichor::net {
    void boot();
    void stop();
    int  require(lua_State* L);
    void tick(lua_State* L);
}