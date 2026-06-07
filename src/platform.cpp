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

IPetrichorHost* g_host = nullptr;

void plog(const char* tag, const char* fmt, ...) {
    if (!g_host) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_host->log(tag, buf);
}

namespace plat {

#if defined(_WIN32)
void* dynOpen(const char* path) { 
    int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    std::wstring wpath(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath.data(), len);
    return (void*)LoadLibraryW(wpath.c_str());
}
void* dynSym(void* h, const char* name) { return (void*)GetProcAddress((HMODULE)h, name); }
void  dynClose(void* h) { FreeLibrary((HMODULE)h); }
const char* dynError() { return ""; }
#else
void* dynOpen(const char* path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
void* dynSym(void* h, const char* name) { return dlsym(h, name); }
void  dynClose(void* h) { dlclose(h); }
const char* dynError() { const char* e = dlerror(); return e ? e : ""; }
#endif

std::string wide_to_utf8(const std::wstring& w) {
#if defined(_WIN32)
    if (w.empty()) return {};

    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), size, nullptr, nullptr);
    return out;
#else
    return std::string(w.begin(), w.end());
#endif
}

std::basic_string<fxr_char> to_fxr(const std::string& s) {
#if defined(_WIN32)
    return std::wstring(s.begin(), s.end());
#else
    return s;
#endif
};

const std::string& exeDir() {
    static std::string dir;
    if (!dir.empty()) return dir;


#if defined(_WIN32)
    wchar_t path[4096];
    if (GetModuleFileNameW(nullptr, path, 4096) == 0) return dir;

    std::wstring w(path);
    dir = wide_to_utf8(w);
#elif defined(__APPLE__)
    char path[4096];
    uint32_t n = sizeof(path);
    if (_NSGetExecutablePath(path, &n) != 0) 
        return dir;  

    dir = path;
#elif defined(__linux__)
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) 
        return dir;

    path[n] = '\0';
    dir = path;
#endif

    size_t slash = dir.find_last_of("/\\");
    if (slash != std::string::npos)
        dir = dir.substr(0, slash + 1);

    return dir;
}

} // namespace plat
} // namespace petrichor
