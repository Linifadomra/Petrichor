#pragma once
#include "petrichor/petrichor.h"
#include "petrichor/backend.h"

#include <string>
#include <vector>

#ifdef _WIN32
using fxr_char = wchar_t;
#define FXR_TEXT(x) L##x
#else
using fxr_char = char;
#define FXR_TEXT(x) x
#endif

namespace petrichor {

extern const PetrichorHost* g_host;

// Routes formatted output to host->log(tag, msg).
void plog(const char* tag, const char* fmt, ...);

namespace plat {
void*       dynOpen(const char* path);
void*       dynSym(void* handle, const char* name);
void        dynClose(void* handle);
const char* dynError();
const std::string& exeDir();   // directory of the running executable, trailing slash

std::string wide_to_utf8(const std::wstring& w);
std::basic_string<fxr_char> to_fxr(const std::string& s);
}

struct LuauModInstance {
    std::string id;
    int         ref;
};

static std::vector<LuauModInstance> s_mods;

// Luau host (luau_host.cpp)
bool luau_boot(const PetrichorHost* host);
bool luau_loadMod(const char* dir, const char* id, const char* type, const char* entry);
void luau_stop();
void luau_tick(float delta);

// Built-in backend registration (backends/*.cpp)
void luau_backend_register();
void native_backend_register();

} // namespace petrichor
