#pragma once
#include <functional>
#include <string>

struct lua_State;

namespace petrichor::https {

using ContextRunner = std::function<void(const std::string& mod_id, const std::function<void()>& fn)>;

void boot();
void stop(lua_State* L);

int require_module(lua_State* L, const char* mod_id);

void cancel_inflight_for_mod(lua_State* L, const char* mod_id);

bool poll(lua_State* L, const ContextRunner& run_in_ctx);

} // namespace petrichor::https
