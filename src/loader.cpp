#include "internal.hpp"
#include "petrichor/petrichor.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <string>
#include <vector>

namespace {

std::vector<IBackend*> s_backends;

void copyJsonString(const nlohmann::json& doc, const char* key, char* out, size_t outSize) {
    if (!doc.contains(key) || !doc[key].is_string()) return;
    const auto s = doc[key].get<std::string>();
    std::strncpy(out, s.c_str(), outSize - 1);
    out[outSize - 1] = '\0';
}

bool readManifest(const char* dir, PetrichorManifest& m) {
    const std::string path = std::string(dir) + "/mod.json";
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    nlohmann::json doc;
    try {
        f >> doc;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    if (!doc.is_object()) return false;

    m.id[0] = m.name[0] = m.type[0] = m.entry[0] = '\0';
    copyJsonString(doc, "id", m.id, sizeof(m.id));
    copyJsonString(doc, "name", m.name, sizeof(m.name));
    copyJsonString(doc, "type", m.type, sizeof(m.type));
    copyJsonString(doc, "entry", m.entry, sizeof(m.entry));
    m.apiVersion = doc.value("apiVersion", 1);
    return m.id[0] && m.type[0];
}

IBackend* backendFor(const char* type) {
    for (auto* b : s_backends)
        if (b->handles(type)) return b;
    return nullptr;
}

void loader_run(IPetrichorHost& host) {
    for (auto* b : s_backends)
        if (!b->init(host)) petrichor::plog(PetrichorLogLevel::Error, "mod", "backend '%s' failed to init", b->name());

    const char* modsDir = host.mods_dir();
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!modsDir || !fs::exists(modsDir, ec) || !fs::is_directory(modsDir, ec)) {
        petrichor::plog(PetrichorLogLevel::Warn, "mod", "no mods directory");
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
            petrichor::plog(PetrichorLogLevel::Warn, "mod", "%s needs api %d", m.id, m.apiVersion);
            continue;
        }
        IBackend* b = backendFor(m.type);
        if (!b) {
            petrichor::plog(PetrichorLogLevel::Error, "mod", "%s: no backend for type '%s'", m.id, m.type);
            continue;
        }
        if (b->load(dir.c_str(), m)) count++;
    }
    petrichor::plog(PetrichorLogLevel::Info, "mod", "%d mod(s) loaded", count);
}

} // namespace

void petrichor_register_backend(IBackend* b) {
    if (b) s_backends.push_back(b);
}

void petrichor_run(IPetrichorHost& host) {
    petrichor::g_host = &host;
    petrichor::luau_backend_register();
    //petrichor::native_backend_register();
    loader_run(host);
}

void petrichor_stop(IPetrichorHost& host) {
    for (auto* b : s_backends) b->shutdown();
}

void petrichor_tick(IPetrichorHost& host, float delta) {
    for (auto* b : s_backends) b->tick(delta);
}

void petrichor_reload_mod(const char* id) {
    for (auto* b : s_backends) b->reload(id);
}

void petrichor_unload_mod(const char* id) {
    for (auto* b : s_backends) b->unload(id);
}

std::vector<std::string> petrichor_poll_changes() {
    return petrichor::luau_poll_changes();
}
