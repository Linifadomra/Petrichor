#pragma once

#include "lua.h"
#include "petrichor/petrichor.h"

#include <vector>
#include <string>

namespace petrichor::async {

void boot(lua_State* L);

void tick(lua_State* L, float delta);

void stop(lua_State* L);

void schedule_step(int step_ref, std::vector<double> results = {});
void schedule_step_str(int step_ref, std::string result);
void schedule_hook_ctx(int step_ref, PetrichorMixinCtx* ctx);
void schedule_timer(float seconds, int step_ref);

void register_module();

} // namespace petrichor::async