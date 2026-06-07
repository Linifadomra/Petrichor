#include "internal.hpp"
#include <cstring>

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
        return luau_loadMod(dir, m.id, m.type, m.entry, store_root);
    }

    void tick(float delta) override { luau_tick(delta); }
    void shutdown()        override { luau_stop(); }
};

static LuauBackend s_luau_backend;
void luau_backend_register() { petrichor_register_backend(&s_luau_backend); }

} // namespace petrichor