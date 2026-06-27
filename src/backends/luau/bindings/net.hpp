/*
 * Petrichor.Net platform-specific Luau library.
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
#include <string>
#include "lua.h"

namespace petrichor::net {
    void boot();
    void stop();
    void tick();
    void cancel_inflight_for_mod(const std::string& mod_id);
    int  require_module(lua_State* L, const std::string& mod_id);
}