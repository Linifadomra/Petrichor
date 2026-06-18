#pragma once
#include "petrichor/petrichor.h"

struct IBackend {
    virtual const char* name()                              const  = 0;
    virtual bool handles(const char* type)                  const  = 0;
    virtual bool init(IPetrichorHost& host)                        = 0;
    virtual bool load(const char* dir, const PetrichorManifest& m) = 0;
    virtual bool reload(const char* id)                            = 0;
    virtual bool unload(const char* id)                            = 0;
    virtual void tick(float delta)                                 = 0;
    virtual void shutdown()                                        = 0;
    virtual std::vector<PetrichorManifest> get_mods()              = 0;
    virtual std::vector<std::string>       poll_changes()          = 0;
    virtual ~IBackend() = default;
};

void petrichor_register_backend(IBackend* b);