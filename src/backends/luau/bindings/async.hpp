/*
 * Petrichor.Async Luau library wiring
 * Copyright (C) 2026 Linifadomra Org.
 *
 * This file is part of Petrichor.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
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