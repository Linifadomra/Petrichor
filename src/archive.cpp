/*
 * miniz archive extraction wrapper
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

#include "archive.hpp"
#include "internal.hpp"

#include "miniz.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static std::string stemOf(const char* archivePath) {
    return fs::path(archivePath).stem().string();
}

static bool isMetadataOrHidden(const fs::path& path) {
    const std::string name = path.filename().string();
    if (name.empty()) return false;
    if (name[0] == '.') return true;
    if (name == "__MACOSX") return true; // macOS zip archives
    return false;
}

static std::string unwrapSingleDir(const fs::path& dir) {
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) return dir.string();

    fs::directory_iterator end;
    fs::path singleDir;
    int count = 0;

    for (; it != end; ++it) {
        if (isMetadataOrHidden(it->path())) {
            continue;
        }
        singleDir = it->path();
        count++;
    }

    if (count == 1 && fs::is_directory(singleDir, ec) && !ec) {
        return singleDir.string();
    }
    return dir.string();
}

static constexpr mz_uint   kMaxEntries       = 4096;
static constexpr mz_uint64 kMaxExtractedBytes = 512ull * 1024 * 1024; // 512 MB

std::string archive_extract(const char* archivePath, const char* tempRoot) {
    fs::path destDir = fs::path(tempRoot) / stemOf(archivePath);
    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (ec) {
        petrichor::plog(PetrichorLogLevel::Error, "mod",
                        "archive: could not create temp dir '%s': %s",
                        destDir.string().c_str(), ec.message().c_str());
        return {};
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, archivePath, 0)) {
        petrichor::plog(PetrichorLogLevel::Error, "mod",
                        "archive: failed to open '%s'", archivePath);
        return {};
    }

    const mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    if (numFiles > kMaxEntries) {
        petrichor::plog(PetrichorLogLevel::Error, "mod",
                        "archive: '%s' has %u entries, limit is %u", archivePath, numFiles, kMaxEntries);
        mz_zip_reader_end(&zip);
        return {};
    }

    bool ok = true;
    mz_uint64 totalExtracted = 0;

    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            petrichor::plog(PetrichorLogLevel::Warn, "mod",
                            "archive: could not stat entry %u in '%s', skipping", i, archivePath);
            continue;
        }

        fs::path rel(stat.m_filename);
        if (rel.is_absolute() || rel.string().find("..") != std::string::npos) {
            petrichor::plog(PetrichorLogLevel::Warn, "mod",
                            "archive: skipping suspicious path '%s'", stat.m_filename);
            continue;
        }

        fs::path outPath = destDir / rel;

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            fs::create_directories(outPath, ec);
            if (ec) {
                petrichor::plog(PetrichorLogLevel::Error, "mod",
                                "archive: could not create dir '%s': %s",
                                outPath.string().c_str(), ec.message().c_str());
                ok = false;
                break;
            }
            continue;
        }

        fs::create_directories(outPath.parent_path(), ec);
        if (ec) {
            petrichor::plog(PetrichorLogLevel::Error, "mod",
                            "archive: could not create parent for '%s': %s",
                            outPath.string().c_str(), ec.message().c_str());
            ok = false;
            break;
        }

        totalExtracted += stat.m_uncomp_size;
        if (totalExtracted > kMaxExtractedBytes) {
            petrichor::plog(PetrichorLogLevel::Error, "mod",
                            "archive: '%s' exceeds extraction limit of %llu bytes", archivePath, kMaxExtractedBytes);
            ok = false;
            break;
        }

        if (!mz_zip_reader_extract_to_file(&zip, i, outPath.string().c_str(), 0)) {
            petrichor::plog(PetrichorLogLevel::Error, "mod",
                            "archive: failed to extract '%s' from '%s'",
                            stat.m_filename, archivePath);
            ok = false;
            break;
        }
    }

    mz_zip_reader_end(&zip);
    if (!ok) return {};
    return unwrapSingleDir(destDir);
}
