#pragma once
#include "petrichor/petrichor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PetrichorManifest {
    char id[64];
    char name[128];
    char type[32];
    char entry[128];
    int  apiVersion;
} PetrichorManifest;

// A loader for a mod `type`. petrichor ships csharp + native backends; the host
// can register more (e.g. CR's .rel backend) before calling petrichor_run.
typedef struct PetrichorBackend {
    const char* name;
    int  (*handles)(const char* type);
    int  (*init)(const PetrichorHost* host);
    int  (*load)(const char* dir, const PetrichorManifest* m);
    void (*shutdown)(void);
} PetrichorBackend;

void petrichor_register_backend(const PetrichorBackend* b);

#ifdef __cplusplus
}
#endif
