/// @internal
/// \cond

/*
 * Backend interface
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
    virtual std::vector<std::string> poll_changes()                = 0;
    virtual ~IBackend() = default;
};

void petrichor_register_backend(IBackend* b);

/// \endcond