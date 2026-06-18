#include "internal.hpp"
#include "petrichor/backend.h"
#include "petrichor/petrichor.h"
#include "archive.hpp"

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
std::vector<PetrichorManifest> s_manifests;

void copyJsonString(const nlohmann::json& doc, const char* key, char* out, size_t outSize) {
    if (!doc.contains(key) || !doc[key].is_string()) return;
    const auto s = doc[key].get<std::string>();
    std::strncpy(out, s.c_str(), outSize - 1);
    out[outSize - 1] = '\0';
}

bool readManifest(const char* dir, PetrichorManifest& m) {
    const std::string base = dir;
    std::string path = base + "/mod.json";
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        path = base + "/manifest.json";
        f.open(path, std::ios::binary);
    }
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
    copyJsonString(doc, "author",  m.author,  sizeof(m.author));
    copyJsonString(doc, "version", m.version, sizeof(m.version));
    copyJsonString(doc, "kind", m.kind, sizeof(m.kind));

    // fallback: derive from type if kind not specified
    if (!m.kind[0]) {
        const std::string_view t = m.type;
        if (t == "luau" || t == "lua" || t == "script")
            std::strncpy(m.kind, "code", sizeof(m.kind));
        else
            std::strncpy(m.kind, "asset", sizeof(m.kind));
    }

    m.desc = doc.value("desc", doc.value("description", ""));

    if (doc.contains("conflicts") && doc["conflicts"].is_array())
        for (const auto& c : doc["conflicts"])
            if (c.is_string()) m.conflicts.push_back(c.get<std::string>());    
    m.apiVersion = doc.value("apiVersion", 1);
    return m.id[0] && m.type[0];
}

IBackend* backendFor(const char* type) {
    for (auto* b : s_backends)
        if (b->handles(type)) return b;
    return nullptr;
}

void remove_temps(IPetrichorHost& host) {
    const char* tmpRoot = host.temp_dir();
    if (!tmpRoot || tmpRoot[0] == '\0') {
        petrichor::plog(PetrichorLogLevel::Warn, "mod", "temp_dir() returned empty, skipping cleanup");
        return;
    }

    const std::filesystem::path root(tmpRoot);
    if (!root.is_absolute()) {
        petrichor::plog(PetrichorLogLevel::Warn, "mod", "temp_dir() returned non-absolute path '%s', skipping cleanup", tmpRoot);
        return;
    }

    const std::filesystem::path petrichorTemp = root / "petrichor";
    std::error_code ec;
    std::filesystem::remove_all(petrichorTemp, ec);
    if (ec) petrichor::plog(PetrichorLogLevel::Warn, "mod", "failed to clean temp dir '%s': %s",
                            petrichorTemp.string().c_str(), ec.message().c_str());
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return text;
}

void loader_run(IPetrichorHost& host, const char* format) {
    for (auto* b : s_backends)
        if (!b->init(host)) petrichor::plog(PetrichorLogLevel::Error, "mod", "backend '%s' failed to init", b->name());

    const char* modsDir = host.mods_dir();
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!modsDir || !fs::exists(modsDir, ec) || !fs::is_directory(modsDir, ec)) {
        petrichor::plog(PetrichorLogLevel::Warn, "mod", "no mods directory");
        return;
    }

    remove_temps(host);

    const std::string ext = format ? (std::string(".") + format) : "";

    int count = 0;
    for (const auto& entry : fs::directory_iterator(modsDir, ec)) {
        if (ec) break;
        std::string dir = entry.path().string();
        
        std::string extractedDir;
        if (!ext.empty() && lower(entry.path().extension().string()) == lower(ext)) {
            const char* tmpRoot = host.temp_dir();
            extractedDir = archive_extract(dir.c_str(), (fs::path(tmpRoot) / "petrichor").string().c_str());
            if (extractedDir.empty()) {
                petrichor::plog(PetrichorLogLevel::Error, "mod", "failed to extract '%s'", dir.c_str());
                continue;
            }
            dir = extractedDir;
        } else if (!entry.is_directory()) {
            petrichor::plog(PetrichorLogLevel::Warn, "mod", "unexpected file in mods folder: '%s'. file format should be: '%s'", dir.c_str(), ext.c_str());
            continue;
        }
        
        PetrichorManifest m = {};
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
        if (b->load(dir.c_str(), m)) {
            m.dir = dir;
            s_manifests.push_back(m);
            count++;
        }
    }
    petrichor::plog(PetrichorLogLevel::Info, "mod", "%d mod(s) loaded", count);
}

} // namespace

void petrichor_register_backend(IBackend* b) {
    if (b) s_backends.push_back(b);
}

void petrichor_run(IPetrichorHost& host, const char* format) {
    petrichor::g_host = &host;
    petrichor::luau_backend_register();
    loader_run(host,format);
}

void petrichor_stop(IPetrichorHost& host) {
    for (auto* b : s_backends) b->shutdown();
    s_manifests.clear();
    remove_temps(host);
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

std::vector<PetrichorManifest> petrichor_get_mods() {
    return s_manifests;
}

std::vector<std::string> petrichor_poll_changes() {
    return petrichor::luau_poll_changes();
}
