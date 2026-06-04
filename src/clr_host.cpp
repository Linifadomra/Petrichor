#include "internal.hpp"

#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

hostfxr_handle   s_ctx   = nullptr;
hostfxr_close_fn s_close = nullptr;

typedef int (CORECLR_DELEGATE_CALLTYPE *init_fn)(const PetrichorHost*);
typedef int (CORECLR_DELEGATE_CALLTYPE *load_fn)(
    const fxr_char*,
    const fxr_char*,
    const fxr_char*,
    const fxr_char*
);

init_fn s_init = nullptr;
load_fn s_load = nullptr;

void* resolve(load_assembly_and_get_function_pointer_fn loadfn, const fxr_char* asmPath, const fxr_char* method) {
    void* fn = nullptr;
    int rc = loadfn(asmPath, FXR_TEXT("Petrichor.ModHost.Host, Petrichor.ModHost"),
                    method, UNMANAGEDCALLERSONLY_METHOD, nullptr, &fn);
    if (rc != 0 || !fn) {
        petrichor::plog("mod", "managed entry %s not found (%x)", method, (unsigned)rc);
        return nullptr;
    }
    return fn;
}

} // namespace

namespace petrichor {

bool clr_boot(const PetrichorHost* host) {
    fxr_char hostfxrPath[2048];
    size_t len = sizeof(hostfxrPath) / sizeof(char_t);
    if (get_hostfxr_path(hostfxrPath, &len, nullptr) != 0) {
        plog("mod", "hostfxr not found; set DOTNET_ROOT");
        return false;
    }

#if defined(_WIN32)
    void* lib = (void*)LoadLibraryW(hostfxrPath);
#else
    void* lib = plat::dynOpen(hostfxrPath);
#endif
    if (!lib) {
        plog("mod", "load hostfxr failed");
        return false;
    }

    auto init   = (hostfxr_initialize_for_runtime_config_fn)plat::dynSym(lib, "hostfxr_initialize_for_runtime_config");
    auto getdel = (hostfxr_get_runtime_delegate_fn)plat::dynSym(lib, "hostfxr_get_runtime_delegate");
    s_close     = (hostfxr_close_fn)plat::dynSym(lib, "hostfxr_close");

    std::string base    = std::string(plat::exeDir()) + "managed/";
    auto cfg     = plat::to_fxr(base + "Petrichor.ModHost.runtimeconfig.json");
    auto asmPath = plat::to_fxr(base + "Petrichor.ModHost.dll");

    if (init(cfg.c_str(), nullptr, &s_ctx) != 0 || !s_ctx) {
        plog("mod", "clr init failed");
        return false;
    }

    load_assembly_and_get_function_pointer_fn loadfn = nullptr;
    if (getdel(s_ctx, hdt_load_assembly_and_get_function_pointer, (void**)&loadfn) != 0 || !loadfn) {
        plog("mod", "get_runtime_delegate failed");
        return false;
    }

    s_init = (init_fn)resolve(loadfn, asmPath.c_str(), FXR_TEXT("ModHost_Init"));
    s_load = (load_fn)resolve(loadfn, asmPath.c_str(), FXR_TEXT("ModHost_LoadMod"));
    if (!s_init || !s_load) return false;

    s_init(host);
    plog("mod", "CoreCLR host started");
    return true;
}

bool clr_loadMod(const char* dir, const char* id, const char* type, const char* entry) {
    if (!s_load) return false;
    auto wdir   = plat::to_fxr(dir);
    auto wid    = plat::to_fxr(id);
    auto wtype  = plat::to_fxr(type);
    auto wentry = plat::to_fxr(entry ? entry : "");
    return s_load(wdir.c_str(), wid.c_str(), wtype.c_str(), wentry.c_str()) == 0;
}

void clr_stop() {
    if (s_ctx && s_close) {
        s_close(s_ctx);
        s_ctx = nullptr;
    }
}

} // namespace petrichor
