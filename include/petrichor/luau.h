#pragma once
#include <stddef.h>

struct lua_State;

namespace petrichor {
    bool luau_load_module(lua_State* L, const char* name, const unsigned char* src, unsigned int len);

    void luau_fire_event(const char* event, const char* json_payload);

    // Main mod VM (nullptr before petrichor_run / after petrichor_stop).
    lua_State* luau_state();
}