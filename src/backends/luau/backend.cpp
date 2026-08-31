/*
 * LuauBackend interface class
 * Copyright (C) 2026 Linifadomra Org.
 *
 * Licensed under the MIT license.
 * See LICENSE for details.
 */

#include "petrichor/backend.h"
#include "internal.hpp"
#include <cstring>
#include <vector>

namespace petrichor {

struct LuauBackend final : IBackend {
    const char* name() const override { return "luau"; }

    bool handles(const char* type) const override {
        return type[0] == '\0'              ||
               strcmp(type, "luau")   == 0 ||
               strcmp(type, "lua")    == 0 ||
               strcmp(type, "script") == 0 ||
               strcmp(type, "asset")  == 0 ||
               strcmp(type, "mixed")  == 0;
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