#include "internal.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace {

typedef void (*ModInitFn)(const PetrichorHost*);
typedef void (*ModShutdownFn)(void);

struct NativeMod {
    void*         handle;
    ModShutdownFn shutdown;
};

std::vector<NativeMod> s_mods;
const PetrichorHost*   s_host = nullptr;

int native_handles(const char* type) { return std::strcmp(type, "native") == 0; }
int native_init(const PetrichorHost* host) { s_host = host; return 1; }

int native_load(const char* dir, const PetrichorManifest* m) {
    if (!m->entry[0]) {
        petrichor::plog("mod", "%s: native mod needs an 'entry' library", m->id);
        return 0;
    }
    std::string path = std::string(dir) + "/" + m->entry;
    void* h = petrichor::plat::dynOpen(path.c_str());
    if (!h) {
        petrichor::plog("mod", "%s: load failed: %s", m->id, petrichor::plat::dynError());
        return 0;
    }
    ModInitFn init = (ModInitFn)petrichor::plat::dynSym(h, "MOD_Init");
    if (!init) {
        petrichor::plog("mod", "%s: no MOD_Init export", m->id);
        petrichor::plat::dynClose(h);
        return 0;
    }
    init(s_host);
    s_mods.push_back({h, (ModShutdownFn)petrichor::plat::dynSym(h, "MOD_Shutdown")});
    petrichor::plog("mod", "loaded native %s", m->id);
    return 1;
}

void native_shutdown() {
    for (auto& nm : s_mods) {
        if (nm.shutdown) nm.shutdown();
        petrichor::plat::dynClose(nm.handle);
    }
    s_mods.clear();
}

const PetrichorBackend s_backend = { "native", native_handles, native_init, native_load, native_shutdown };

} // namespace

namespace petrichor {
void native_backend_register() { petrichor_register_backend(&s_backend); }
}
