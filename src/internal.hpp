#pragma once
#include "petrichor/petrichor.h"
#include "petrichor/backend.h"

namespace petrichor {

extern const PetrichorHost* g_host;

// Routes formatted output to host->log(tag, msg).
void plog(const char* tag, const char* fmt, ...);

namespace plat {
void*       dynOpen(const char* path);
void*       dynSym(void* handle, const char* name);
void        dynClose(void* handle);
const char* dynError();
const char* exeDir();   // directory of the running executable, trailing slash
}

// CLR host (clr_host.cpp)
bool clr_boot(const PetrichorHost* host);
bool clr_loadMod(const char* dir, const char* id, const char* type, const char* entry);
void clr_stop();

// Built-in backend registration (backends/*.cpp)
void clr_backend_register();
void native_backend_register();

} // namespace petrichor
