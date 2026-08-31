/*
 * Internal use - backend header.
 * Copyright (C) 2026 Linifadomra Org.
 *
 * Licensed under the MIT license.
 * See LICENSE for details.
 */

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

struct SemVer {
    int major{}, minor{}, patch{};
    bool valid;

    SemVer() = default;
    SemVer(int major, int minor, int patch, bool valid) : major(major), minor(minor), patch(patch), valid(valid) {}
    SemVer(const char* v) {
        std::sscanf(v, "%d.%d.%d", &this->major, &this->minor, &this->patch);
        this->valid = !(major == 0 && minor == 0 && patch == 0);
    }

    static SemVer parse(const char* v) { return SemVer(v); }

    auto operator<=>(const SemVer&) const = default;
    bool operator==(const SemVer&) const = default;
};

extern SemVer g_host_version;
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