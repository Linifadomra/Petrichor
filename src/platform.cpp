#include "internal.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace petrichor {

const PetrichorHost* g_host = nullptr;

void plog(const char* tag, const char* fmt, ...) {
    if (!g_host || !g_host->log) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_host->log(tag, buf);
}

namespace plat {

#if defined(_WIN32)
void* dynOpen(const char* path) { return (void*)LoadLibraryA(path); }
void* dynSym(void* h, const char* name) { return (void*)GetProcAddress((HMODULE)h, name); }
void  dynClose(void* h) { FreeLibrary((HMODULE)h); }
const char* dynError() { return ""; }
#else
void* dynOpen(const char* path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
void* dynSym(void* h, const char* name) { return dlsym(h, name); }
void  dynClose(void* h) { dlclose(h); }
const char* dynError() { const char* e = dlerror(); return e ? e : ""; }
#endif

const char* exeDir() {
    static std::string dir;
    if (!dir.empty()) return dir.c_str();

    char path[4096];
#if defined(__APPLE__)
    uint32_t n = sizeof(path);
    if (_NSGetExecutablePath(path, &n) != 0) return "";
#elif defined(_WIN32)
    if (GetModuleFileNameA(nullptr, path, sizeof(path)) == 0) return "";
#else
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) return "";
    path[n] = '\0';
#endif
    std::string p(path);
    size_t slash = p.find_last_of("/\\");
    dir = (slash == std::string::npos) ? "" : p.substr(0, slash + 1);
    return dir.c_str();
}

} // namespace plat
} // namespace petrichor
