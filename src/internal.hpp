#pragma once
#include "lua.h"
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

extern IPetrichorHost* g_host;

void plog(const char* tag, const char* fmt, ...);

namespace plat {
    void*       dynOpen(const char* path);
    void*       dynSym(void* handle, const char* name);
    void        dynClose(void* handle);
    const char* dynError();
    const std::string& exeDir();
    std::string wide_to_utf8(const std::wstring& w);
    std::basic_string<fxr_char> to_fxr(const std::string& s);
}

using ModuleFactory = int(*)(lua_State*);
void luau_register_module(const char* name, ModuleFactory factory);

struct LuauModInstance { std::string id; int ref; };
static std::vector<LuauModInstance> s_mods;

bool luau_boot(IPetrichorHost& host);
bool luau_loadMod(const char* dir, const char* id, const char* type, const char* entry);
void luau_stop();
void luau_tick(float delta);

void luau_backend_register();
void native_backend_register();

} // namespace petrichor