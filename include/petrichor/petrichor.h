/*
 * Petrichor public API
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
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#define PETRICHOR_API_VERSION 3

/**
 * @brief Manifest describing a Petrichor package.
 *
 * Stores metadata loaded from a package manifest, including its identity,
 * version, entry point, author information, compatibility requirements,
 * filesystem location, and dependency conflict list.
 */
struct PetrichorManifest {
    /** Unique package identifier. */
    char id[64];

    /** Human-readable package name. */
    char name[128];

    /** Package kind: "code", "asset", or "mixed". */
    char kind[16];

    /** Package type or category. */
    char type[32];

    /** Relative path to the package entry point. */
    char entry[128];

    /** Package author. */
    char author[128];

    /** Package version string. */
    char version[32];

    /** Package description. */
    std::string desc;

    /** List of incompatible package IDs. */
    std::vector<std::string> conflicts;

    /** Absolute path to the package's root directory. */
    std::string dir;

    /** Target Petrichor API version. */
    int apiVersion;
};

struct PetrichorEvents {
    void (*pre_frame)(void);
    void (*post_frame)(void);
    void (*scene_load)(const char* stage, int32_t point);
};

/**
 * @brief Petrichor logging verbosity level
 */
enum class PetrichorLogLevel : uint8_t { Info = 0, Warn, Error, Debug };

/**
 * @brief Petrichor consumer-level interface.
 *
 * Implemented by the host application and passed to Augment during
 * initialization. Provides logging, memory allocation, symbol
 * resolution, event registration, and access to host-specific
 * services required by modules.
 */
struct IPetrichorHost {
    /**
     * @brief Writes a log message through the host.
     * @param level Log severity.
     * @param tag Source or subsystem name.
     * @param msg Null-terminated message.
     */
    virtual void log(PetrichorLogLevel level, const char* tag, const char* msg) const = 0;

    /** @brief Returns the root directory containing installed packages. */
    virtual const char* mods_dir() const = 0;

    /** @brief Returns the host's temporary working directory. */
    virtual const char* temp_dir() const = 0;

    /**
     * @brief Allocates memory owned by the host.
     * @param bytes Number of bytes to allocate.
     * @return Pointer to the allocated memory, or nullptr on failure.
     */
    virtual void* alloc(uint32_t bytes) = 0;

    /**
     * @brief Resolves a host-exported symbol by name.
     * @param sym Null-terminated symbol name.
     * @return Pointer to the resolved symbol, or nullptr if unavailable.
     */
    virtual void* resolve(const char* sym) const = 0;

    /**
     * @brief Invokes a host-exported function dynamically.
     * @param sym Symbol name to invoke.
     * @param args Array of argument pointers.
     * @param n Number of arguments.
     * @param ret_out Optional output location for the return value.
     * @return Host-defined status code.
     */
    virtual int call(const char* sym, void** args, uint32_t n, void* ret_out) = 0;

    /**
     * @brief Registers event callbacks with the host.
     * @param ev Event callback table.
     */
    virtual void subscribe_events(const PetrichorEvents* ev) = 0;

    /**
     * @brief Returns an optional pointer to the native game API.
     *
     * Hosts that do not expose a game API should return nullptr.
     */
    virtual void* game_api() const { return nullptr; }

    /**
     * @brief Returns the directory used for persistent package storage.
     *
     * Hosts that do not provide persistent storage should return nullptr.
     */
    virtual const char* store_dir() const { return nullptr; }

    /** @brief Virtual destructor. */
    virtual ~IPetrichorHost() = default;
};

/**
 * @brief Starts the Petrichor runtime.
 *
 * Discovers, loads, and initializes packages using the supplied host
 * interface.
 *
 * @param host Host implementation providing runtime services.
 * @param format Package manifest format identifier. Defaults to "prm".
 */
void petrichor_run(IPetrichorHost& host, const char* format = "prm");

/**
 * @brief Advances the Petrichor runtime by one frame.
 *
 * Invokes per-frame callbacks for loaded packages.
 *
 * @param host Host implementation.
 * @param delta Elapsed time since the previous tick, in seconds.
 */
void petrichor_tick(IPetrichorHost& host, float delta);

/**
 * @brief Stops the Petrichor runtime.
 *
 * Shuts down loaded packages and releases runtime resources.
 *
 * @param host Host implementation.
 */
void petrichor_stop(IPetrichorHost& host);

/**
 * @brief Unloads a package by its identifier.
 *
 * @param id Unique package identifier.
 */
void petrichor_unload_mod(const char* id);

/**
 * @brief Reloads a package by its identifier.
 *
 * Unloads the package if necessary and loads the latest version from disk.
 *
 * @param id Unique package identifier.
 */
void petrichor_reload_mod(const char* id);

/**
 * @brief Retrieves packages that have changed on disk.
 *
 * Intended for hot-reload workflows.
 *
 * @return List of changed package identifiers.
 */
std::vector<std::string> petrichor_poll_changes();

/**
 * @brief Returns metadata for all discovered packages.
 *
 * @return Collection of package manifests.
 */
std::vector<PetrichorManifest> petrichor_get_mods();

#ifdef __cplusplus
struct lua_State;
namespace petrichor {

    /**
     * @brief Factory function used to register a Luau module.
     *
     * The function is expected to push the module value onto the Lua stack
     * and return the number of values pushed.
     */
    using ModuleFactory = int(*)(lua_State*);

    /**
     * @brief Registers a native Luau module.
     *
     * Registered modules become available to scripts through the runtime's
     * module loader.
     *
     * @param name Module name exposed to Luau.
     * @param factory Module factory callback.
     */
    void luau_register_module(const char* name, ModuleFactory factory);

    /**
     * @brief Returns the active Luau state.
     *
     * @return Pointer to the runtime's Lua state, or nullptr if the runtime
     *         is not initialized.
     */
    lua_State* lua_state();

}
#endif
