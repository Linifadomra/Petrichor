#pragma once
#include <stddef.h>

struct lua_State;

namespace petrichor {
    bool luau_load_module(lua_State* L, const char* name, const unsigned char* src, unsigned int len);
}