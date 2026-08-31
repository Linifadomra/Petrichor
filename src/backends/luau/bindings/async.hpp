/*
 * Petrichor.Async Luau library wiring
 * Copyright (C) 2026 Linifadomra Org.
 *
 * Licensed under the MIT license.
 * See LICENSE for details.
 */
#pragma once

#include "lua.h"
#include "petrichor/petrichor.h"

#include <vector>
#include <string>

namespace petrichor::async {

void boot(lua_State* L);

void tick(lua_State* L, float delta);

void stop(lua_State* L);

void schedule_step(lua_State* L, int ref, std::vector<double> r = {});
void schedule_step_str(lua_State* L, int ref, std::string r);
void schedule_timer(lua_State* L, float secs, int ref);
void cancel_pending_for_thread(lua_State* L);

void register_module();

} // namespace petrichor::async