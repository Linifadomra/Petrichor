/*
 * LuauBackend interface class
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

#include "petrichor/backend.h"
#include "internal.hpp"
#include <cstring>
#include <vector>

namespace petrichor {

struct LuauBackend final : IBackend {
    const char* name() const override { return "luau"; }

    bool handles(const char* type) const override {
        return strcmp(type, "luau")   == 0 ||
               strcmp(type, "lua")    == 0 ||
               strcmp(type, "script") == 0;
    }

    bool init(IPetrichorHost& host) override {
        return luau_boot(host);
    }

    bool load(const char* dir, const PetrichorManifest& m) override {
        const char* store_root = petrichor::g_host ? petrichor::g_host->store_dir() : nullptr;
        return luau_loadMod(dir, m, store_root);
    }

    void tick(float delta)                  override { luau_tick(delta);           }
    void shutdown()                         override { luau_stop();                }
    bool reload(const char* id)             override { return luau_reloadMod(id);  }
    bool unload(const char* id)             override { return luau_unloadMod(id);  }
    std::vector<std::string> poll_changes() override { return luau_poll_changes(); }
};

static LuauBackend s_luau_backend;
void luau_backend_register() { petrichor_register_backend(&s_luau_backend); }

} // namespace petrichor