#include "internal.hpp"
#include <cstring>

namespace {

int luau_handles(const char* type) {
    return strcmp(type, "luau") == 0 ||
           strcmp(type, "lua")  == 0 ||
           strcmp(type, "script") == 0;
}

int  luau_init(const PetrichorHost* host) { return petrichor::luau_boot(host) ? 1 : 0; }
int  luau_load(const char* dir, const PetrichorManifest* m) {
    return petrichor::luau_loadMod(dir, m->id, m->type, m->entry) ? 1 : 0;
}
void luau_shutdown() { petrichor::luau_stop(); }
void luau_tick(float delta) { petrichor::luau_tick(delta); }

const PetrichorBackend s_backend = { "luau", luau_handles, luau_init, luau_load, luau_tick, luau_shutdown };


} // namespace

namespace petrichor {
void luau_backend_register() { petrichor_register_backend(&s_backend); }
}
