#include "internal.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::vector<IBackend*> s_backends;

bool jsonString(const char* json, const char* key, char* out, int outSize) {
    char search[96];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    while (*p && *p != ':') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return false;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < outSize - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

int jsonInt(const char* json, const char* key, int def) {
    char search[96];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(json, search);
    if (!p) return def;
    p += strlen(search);
    while (*p && *p != ':') p++;
    if (*p != ':') return def;
    return atoi(p + 1);
}

bool readManifest(const char* dir, PetrichorManifest& m) {
    std::string path = std::string(dir) + "/mod.json";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) fread(&buf[0], 1, (size_t)n, f);
    fclose(f);
    m.id[0] = m.name[0] = m.type[0] = m.entry[0] = '\0';
    jsonString(buf.c_str(), "id",         m.id,    sizeof(m.id));
    jsonString(buf.c_str(), "name",       m.name,  sizeof(m.name));
    jsonString(buf.c_str(), "type",       m.type,  sizeof(m.type));
    jsonString(buf.c_str(), "entry",      m.entry, sizeof(m.entry));
    m.apiVersion = jsonInt(buf.c_str(), "apiVersion", 1);
    return m.id[0] && m.type[0];
}

IBackend* backendFor(const char* type) {
    for (auto* b : s_backends)
        if (b->handles(type)) return b;
    return nullptr;
}

void loader_run(IPetrichorHost& host) {
    for (auto* b : s_backends)
        if (!b->init(host)) petrichor::plog("mod", "backend '%s' failed to init", b->name());

    const char* modsDir = host.mods_dir();
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!modsDir || !fs::exists(modsDir, ec) || !fs::is_directory(modsDir, ec)) {
        petrichor::plog("mod", "no mods directory");
        return;
    }

    int count = 0;
    for (const auto& entry : fs::directory_iterator(modsDir, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        std::string dir = entry.path().string();
        PetrichorManifest m;
        if (!readManifest(dir.c_str(), m)) continue;
        if (m.apiVersion > PETRICHOR_API_VERSION) {
            petrichor::plog("mod", "%s needs api %d", m.id, m.apiVersion);
            continue;
        }
        IBackend* b = backendFor(m.type);
        if (!b) {
            petrichor::plog("mod", "%s: no backend for type '%s'", m.id, m.type);
            continue;
        }
        if (b->load(dir.c_str(), m)) count++;
    }
    petrichor::plog("mod", "%d mod(s) loaded", count);
}

} // namespace

void petrichor_register_backend(IBackend* b) {
    if (b) s_backends.push_back(b);
}

void petrichor_run(IPetrichorHost& host) {
    petrichor::g_host = &host;
    petrichor::luau_backend_register();
    petrichor::native_backend_register();
    loader_run(host);
}

void petrichor_stop(IPetrichorHost& host) {
    for (auto* b : s_backends) b->shutdown();
}

void petrichor_tick(IPetrichorHost& host, float delta) {
    for (auto* b : s_backends) b->tick(delta);
}