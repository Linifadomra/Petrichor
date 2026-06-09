#include "internal.hpp"
#include "petrichor/petrichor.h"
#include <cstring>
#include <string>
#include <vector>

namespace petrichor {

struct NativeBackend final : IBackend {
    const char* name() const override { return "native"; }

    bool handles(const char* type) const override {
        return strcmp(type, "native") == 0;
    }

    bool init(IPetrichorHost& host) override {
        s_host = &host;
        return true;
    }

    bool load(const char* dir, const PetrichorManifest& m) override {
        if (!m.entry[0]) {
            plog(PetrichorLogLevel::Error, "mod", "%s: native mod needs an 'entry' library", m.id);
            return false;
        }
        std::string path = std::string(dir) + "/" + m.entry;
        void* h = plat::dynOpen(path.c_str());
        if (!h) {
            plog(PetrichorLogLevel::Error, "mod", "%s: load failed: %s", m.id, plat::dynError());
            return false;
        }
        using ModInitFn     = void(*)(IPetrichorHost*);
        using ModShutdownFn = void(*)(void);
        auto init_fn = (ModInitFn)plat::dynSym(h, "MOD_Init");
        if (!init_fn) {
            plog(PetrichorLogLevel::Error, "mod", "%s: no MOD_Init export", m.id);
            plat::dynClose(h);
            return false;
        }
        init_fn(s_host);
        s_mods.push_back({h, (ModShutdownFn)plat::dynSym(h, "MOD_Shutdown")});
        plog(PetrichorLogLevel::Info, "mod", "loaded native %s", m.id);
        return true;
    }

    void tick(float) override {}

    void shutdown() override {
        for (auto& nm : s_mods) {
            if (nm.shutdown_fn) nm.shutdown_fn();
            plat::dynClose(nm.handle);
        }
        s_mods.clear();
    }

private:
    struct NativeMod { void* handle; void(*shutdown_fn)(void); };
    std::vector<NativeMod> s_mods;
    IPetrichorHost*        s_host = nullptr;
};

static NativeBackend s_native_backend;
void native_backend_register() { petrichor_register_backend(&s_native_backend); }

} // namespace petrichor