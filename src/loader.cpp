/*
 * Top level Petrichor mod loading & backend deferrance.
 * Copyright (C) 2026 Linifadomra Org.
 *
 * This file is part of Petrichor.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

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
#include "petrichor/petrichor_version.hpp"

petrichor::SemVer petrichor::g_host_version = petrichor::SemVer(0,0,0,false);

namespace {

std::vector<IBackend*> s_backends;
std::vector<PetrichorManifest> s_manifests;
static const petrichor::SemVer PETRICHOR_VERSION = petrichor::SemVer(PETRICHOR_VERSION_STRING);

void store_game_version(IPetrichorHost& host) {
    if (strcmp(host.version(), "unversioned") == 0) {
        petrichor::plog(PetrichorLogLevel::Warn, 
            "%s has no version. Automatic versioning will be unavailable. Ask project developers to fix the issue.", 
            host.project_name());
        petrichor::g_host_version = petrichor::SemVer(0, 0, 0, false);
        return;
    }

    petrichor::g_host_version = petrichor::SemVer(host.version());
    
    if (!petrichor::g_host_version.valid) petrichor::plog(PetrichorLogLevel::Warn, "%s parsed as 0.0.0. Automatic versioning will be unavailable. Ask project developers to fix the issue.", host.project_name());
}

bool isHiddenEntryName(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return !name.empty() && name[0] == '.';
}

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
    copyJsonString(doc, "version", m.version, sizeof(m.version)); // Package version
    copyJsonString(doc, "hostVersion", m.hostVersion,     sizeof(m.hostVersion)); // Host project version
    copyJsonString(doc, "engineVersion", m.engineVersion,     sizeof(m.engineVersion)); // Petrichor API version
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

std::vector<std::string> normalizeExts(const std::vector<std::string>& formats) {
    std::vector<std::string> exts;
    exts.reserve(formats.size());
    for (const auto& f : formats) {
        if (f.empty()) continue;
        std::string e = lower(f);
        if (e[0] != '.') e = "." + e;
        exts.push_back(std::move(e));
    }
    return exts;
}

bool resolveEntry(IPetrichorHost& host, const std::filesystem::directory_entry& entry,
                   const std::vector<std::string>& exts, const std::string& extsDisplay,
                   std::string& outDir, PetrichorManifest& outManifest) {
    namespace fs = std::filesystem;
    if (isHiddenEntryName(entry.path())) return false;
    std::string dir = entry.path().string();

    auto matchesFormat = [&exts](const std::string& extension) {
        std::string e = lower(extension);
        return std::find(exts.begin(), exts.end(), e) != exts.end();
    };

    if (!exts.empty() && matchesFormat(entry.path().extension().string())) {
        const char* tmpRoot = host.temp_dir();
        std::string extractedDir = archive_extract(dir.c_str(), (fs::path(tmpRoot) / "petrichor").string().c_str());
        if (extractedDir.empty()) {
            petrichor::plog(PetrichorLogLevel::Error, "mod", "failed to extract '%s'", dir.c_str());
            return false;
        }
        dir = extractedDir;
    } else if (!entry.is_directory()) {
        petrichor::plog(PetrichorLogLevel::Warn, "mod", "unexpected file in mods folder: '%s'. file format should be one of: '%s'", dir.c_str(), extsDisplay.c_str());
        return false;
    }

    PetrichorManifest m = {};
    if (!readManifest(dir.c_str(), m)) return false;
    outDir = dir;
    outManifest = m;
    return true;
}

bool discoverEntry(IPetrichorHost& host, const std::filesystem::directory_entry& entry,
                    const std::vector<std::string>& exts, const std::string& extsDisplay,
                    PetrichorManifest& outManifest) {
    std::string dir;
    PetrichorManifest m;
    if (!resolveEntry(host, entry, exts, extsDisplay, dir, m)) return false;
    if (PETRICHOR_VERSION.valid && m.engineVersion[0]) {
        auto prVersion = petrichor::SemVer::parse(m.engineVersion);
        if (prVersion > PETRICHOR_VERSION) {
            petrichor::plog(PetrichorLogLevel::Error, "mod",
                "%s requires Petrichor %s, running %s. Skipping...",
                m.id, m.engineVersion, PETRICHOR_VERSION_STRING);
            return false;
        } else if (prVersion.major < PETRICHOR_VERSION.major) {
            int majorBehind = PETRICHOR_VERSION.major - prVersion.major;
            petrichor::plog(PetrichorLogLevel::Warn, "mod",
                "%s was built against Petrichor %s (%d major version(s) behind, running %s). May be unstable.",
                m.id, m.engineVersion, majorBehind, PETRICHOR_VERSION_STRING);
        } else if (prVersion.minor < PETRICHOR_VERSION.minor) {
            int minorBehind = PETRICHOR_VERSION.minor - prVersion.minor;
            petrichor::plog(PetrichorLogLevel::Warn, "mod",
                "%s was built against Petrichor %s (%d minor version(s) behind, running %s).",
                m.id, m.engineVersion, minorBehind, PETRICHOR_VERSION_STRING);
        }
    }

    if (petrichor::g_host_version.valid && m.hostVersion[0]) {
        auto hVersion = petrichor::SemVer::parse(m.hostVersion);
        if (hVersion > petrichor::g_host_version) {
            petrichor::plog(PetrichorLogLevel::Error, "mod",
                "%s requires %s %s, running %s. Skipping...",
                m.id, host.project_name(), m.hostVersion, host.version());
            return false;
        } else if (hVersion.major < petrichor::g_host_version.major) {
            int majorBehind = petrichor::g_host_version.major - hVersion.major;
            petrichor::plog(PetrichorLogLevel::Warn, "mod",
                "%s was built against %s %s (%d major version(s) behind, running %s). May be unstable.",
                m.id, host.project_name(), m.hostVersion, majorBehind, host.version());
        } else if (hVersion.minor < petrichor::g_host_version.minor) {
            int minorBehind = petrichor::g_host_version.minor - hVersion.minor;
            petrichor::plog(PetrichorLogLevel::Warn, "mod",
                "%s was built against %s %s (%d minor version(s) behind, running %s).",
                m.id, host.project_name(), m.hostVersion, minorBehind, host.version());
        }
    }

    IBackend* b = backendFor(m.type);
    if (!b) {
        petrichor::plog(PetrichorLogLevel::Error, "mod", "%s: no backend for type '%s'", m.id, m.type);
        return false;
    }
    if (!b->load(dir.c_str(), m)) return false;
    m.dir = dir;
    outManifest = m;
    return true;
}

void loader_run(IPetrichorHost& host, std::vector<std::string> formats) {
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

    const std::vector<std::string> exts = normalizeExts(formats);
    std::string extsDisplay;
    for (size_t i = 0; i < exts.size(); ++i) {
        if (i) extsDisplay += ", ";
        extsDisplay += exts[i];
    }

    int count = 0;
    for (const auto& entry : fs::directory_iterator(modsDir, ec)) {
        if (ec) break;
        PetrichorManifest m;
        if (discoverEntry(host, entry, exts, extsDisplay, m)) {
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

void petrichor_run(IPetrichorHost& host, std::vector<std::string> formats) {
    petrichor::g_host = &host;
    petrichor::luau_backend_register();
    store_game_version(host);
    loader_run(host,formats);
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

void petrichor_rescan_dir(IPetrichorHost& host, std::vector<std::string> formats) {
    const char* modsDir = host.mods_dir();
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!modsDir || !fs::exists(modsDir, ec) || !fs::is_directory(modsDir, ec)) {
        petrichor::plog(PetrichorLogLevel::Warn, "mod", "no mods directory");
        return;
    }

    const std::vector<std::string> exts = normalizeExts(formats);
    std::string extsDisplay;
    for (size_t i = 0; i < exts.size(); ++i) {
        if (i) extsDisplay += ", ";
        extsDisplay += exts[i];
    }

    std::vector<std::string> presentIds;
    std::vector<std::filesystem::directory_entry> newEntries;
    for (const auto& entry : fs::directory_iterator(modsDir, ec)) {
        if (ec) break;
        std::string dir;
        PetrichorManifest m;
        if (!resolveEntry(host, entry, exts, extsDisplay, dir, m)) continue;
        presentIds.push_back(m.id);
        bool alreadyKnown = std::any_of(s_manifests.begin(), s_manifests.end(),
            [&](const PetrichorManifest& existing) { return std::strcmp(existing.id, m.id) == 0; });
        if (!alreadyKnown) newEntries.push_back(entry);
    }

    int removed = 0;
    std::vector<PetrichorManifest> next;
    next.reserve(s_manifests.size());
    for (const auto& old : s_manifests) {
        bool stillPresent = std::any_of(presentIds.begin(), presentIds.end(),
            [&](const std::string& id) { return id == old.id; });
        if (stillPresent) {
            next.push_back(old);
        } else {
            for (auto* b : s_backends) b->unload(old.id);
            petrichor::plog(PetrichorLogLevel::Info, "mod", "%s removed from disk, unloaded", old.id);
            removed++;
        }
    }

    int added = 0;
    for (const auto& entry : newEntries) {
        PetrichorManifest m;
        if (discoverEntry(host, entry, exts, extsDisplay, m)) {
            next.push_back(m);
            added++;
        }
    }

    s_manifests = std::move(next);
    if (removed || added)
        petrichor::plog(PetrichorLogLevel::Info, "mod", "rescan: %d added, %d removed", added, removed);
}
