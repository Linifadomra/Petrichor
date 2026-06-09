#pragma once
#include "lua.h"

namespace petrichor::audio {
    void boot();
    void stop_all();
    int  require_module(lua_State* L);
}
