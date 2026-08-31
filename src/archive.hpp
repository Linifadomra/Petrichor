/*
 * miniz archive extraction wrapper
 * Copyright (C) 2026 Linifadomra Org.
 *
 * Licensed under the MIT license.
 * See LICENSE for details.
 */

#pragma once
#include <string>

std::string archive_extract(const char* archivePath, const char* tempRoot);
