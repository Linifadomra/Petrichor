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

static std::string unwrapSingleDir(const fs::path& dir) {
    fs::directory_iterator it(dir);
    fs::directory_iterator end;

    if (it == end) return dir.string();
    fs::path first = it->path();
    ++it;
    if (it != end) return dir.string();
    if (fs::is_directory(first)) return first.string();
    return dir.string();
}

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

    bool ok = true;
    const mz_uint numFiles = mz_zip_reader_get_num_files(&zip);

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