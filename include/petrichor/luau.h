/// @internal
/// \cond

/*
 * Luau backend public API
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
#include <stddef.h>

struct lua_State;

namespace petrichor {
    bool luau_load_module(lua_State* L, const char* name, const unsigned char* src, unsigned int len);

    void luau_fire_event(const char* event, const char* json_payload);
}

/// \endcond