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

void plog(PetrichorLogLevel level, const char* tag, const char* fmt, ...);

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

bool luau_boot(IPetrichorHost& host);
bool luau_loadMod(const char* dir, const PetrichorManifest& m, const char* store_root);
bool luau_reloadMod(const char* id);
bool luau_unloadMod(const char* id);
void luau_tick(float delta);
void luau_stop();
std::vector<std::string> luau_poll_changes();

void luau_backend_register();

void async_schedule_step(int step_ref, std::vector<double> results = {});
void async_schedule_step_str(int step_ref, std::string result);

} // namespace petrichor