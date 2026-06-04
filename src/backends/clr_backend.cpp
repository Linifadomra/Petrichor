#include "internal.hpp"
#include <cstring>

namespace {

int clr_handles(const char* type) {
    return std::strcmp(type, "csharp") == 0 ||
           std::strcmp(type, "csharpscript") == 0 ||
           std::strcmp(type, "script") == 0;
}
int  clr_init(const PetrichorHost* host) { return petrichor::clr_boot(host); }
int  clr_load(const char* dir, const PetrichorManifest* m) {
    return petrichor::clr_loadMod(dir, m->id, m->type, m->entry);
}
void clr_shutdown() { petrichor::clr_stop(); }

const PetrichorBackend s_backend = { "csharp", clr_handles, clr_init, clr_load, clr_shutdown };

} // namespace

namespace petrichor {
void clr_backend_register() { petrichor_register_backend(&s_backend); }
}
