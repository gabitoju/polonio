#include "polonio/runtime/storage_ops.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>

#include "polonio/common/error.h"
#include "polonio/runtime/interpreter.h"
#include "polonio/runtime/storage.h"

namespace polonio {

namespace {

std::string make_temp_path(const std::filesystem::path& dir) {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    for (int i = 0; i < 16; ++i) {
        auto candidate = dir / (".tmp." + std::to_string(dist(rng)));
        if (!std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }
    return (dir / ".tmp").string();
}

void ensure_parent_exists(const std::filesystem::path& path,
                          const std::string& builtin_name,
                          Interpreter& interp,
                          const Location& loc) {
    auto parent = path.parent_path();
    if (parent.empty() || (std::filesystem::exists(parent) && std::filesystem::is_directory(parent))) {
        return;
    }
    throw PolonioError(ErrorKind::Runtime,
                       builtin_name + ": missing directory",
                       interp.path(),
                       loc);
}

} // namespace

std::string storage_file_read(const std::string& relative,
                              Interpreter& interp,
                              const std::string& builtin_name,
                              const Location& loc) {
    auto resolved = resolve_storage_path(relative, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    if (!std::filesystem::exists(path)) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": file not found",
                           interp.path(),
                           loc);
    }
    if (!std::filesystem::is_regular_file(path)) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": not a file",
                           interp.path(),
                           loc);
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": unable to read file",
                           interp.path(),
                           loc);
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content;
}

void storage_file_write(const std::string& relative,
                        const std::string& content,
                        Interpreter& interp,
                        const std::string& builtin_name,
                        const Location& loc) {
    auto resolved = resolve_storage_path(relative, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    ensure_parent_exists(path, builtin_name, interp, loc);
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": target is a directory",
                           interp.path(),
                           loc);
    }
    auto parent = path.parent_path();
    auto temp_path = make_temp_path(parent);
    {
        std::ofstream tmp(temp_path, std::ios::binary);
        if (!tmp) {
            throw PolonioError(ErrorKind::Runtime,
                               builtin_name + ": unable to write temp file",
                               interp.path(),
                               loc);
        }
        tmp << content;
    }
    std::error_code ec;
    std::filesystem::rename(temp_path, path, ec);
    if (ec) {
        std::filesystem::remove(temp_path);
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": unable to replace file",
                           interp.path(),
                           loc);
    }
}

void storage_file_append(const std::string& relative,
                         const std::string& content,
                         Interpreter& interp,
                         const std::string& builtin_name,
                         const Location& loc) {
    auto resolved = resolve_storage_path(relative, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    ensure_parent_exists(path, builtin_name, interp, loc);
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": target is a directory",
                           interp.path(),
                           loc);
    }
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": unable to append to file",
                           interp.path(),
                           loc);
    }
    file << content;
}

bool storage_file_exists(const std::string& relative,
                         Interpreter& interp,
                         const std::string& builtin_name,
                         const Location& loc) {
    auto resolved = resolve_storage_path(relative, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    if (!std::filesystem::exists(path)) {
        return false;
    }
    return std::filesystem::is_regular_file(path);
}

bool storage_file_delete(const std::string& relative,
                         Interpreter& interp,
                         const std::string& builtin_name,
                         const Location& loc) {
    auto resolved = resolve_storage_path(relative, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    if (!std::filesystem::exists(path)) {
        return false;
    }
    if (std::filesystem::is_directory(path)) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": path is a directory",
                           interp.path(),
                           loc);
    }
    std::error_code ec;
    bool removed = std::filesystem::remove(path, ec);
    if (ec) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": unable to delete file",
                           interp.path(),
                           loc);
    }
    return removed;
}

std::uintmax_t storage_file_size(const std::string& relative,
                                 Interpreter& interp,
                                 const std::string& builtin_name,
                                 const Location& loc) {
    auto resolved = resolve_storage_path(relative, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    if (!std::filesystem::exists(path)) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": file not found",
                           interp.path(),
                           loc);
    }
    if (!std::filesystem::is_regular_file(path)) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": not a file",
                           interp.path(),
                           loc);
    }
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": unable to read file size",
                           interp.path(),
                           loc);
    }
    return size;
}

std::int64_t storage_file_modified(const std::string& relative,
                                   Interpreter& interp,
                                   const std::string& builtin_name,
                                   const Location& loc) {
    auto resolved = resolve_storage_path(relative, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    if (!std::filesystem::exists(path)) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": file not found",
                           interp.path(),
                           loc);
    }
    if (!std::filesystem::is_regular_file(path)) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": not a file",
                           interp.path(),
                           loc);
    }
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": unable to read file time",
                           interp.path(),
                           loc);
    }
    auto diff = ftime - std::filesystem::file_time_type::clock::now();
    auto system_time = std::chrono::system_clock::now() +
                       std::chrono::duration_cast<std::chrono::system_clock::duration>(diff);
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(system_time.time_since_epoch()).count();
    return seconds;
}

bool storage_dir_create(const std::string& relative,
                        Interpreter& interp,
                        const std::string& builtin_name,
                        const Location& loc) {
    auto resolved = resolve_storage_path(relative, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    if (std::filesystem::exists(path)) {
        if (!std::filesystem::is_directory(path)) {
            throw PolonioError(ErrorKind::Runtime,
                               builtin_name + ": path exists and is not a directory",
                               interp.path(),
                               loc);
        }
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": unable to create directory",
                           interp.path(),
                           loc);
    }
    return true;
}

bool storage_dir_exists(const std::string& relative,
                        Interpreter& interp,
                        const std::string& builtin_name,
                        const Location& loc) {
    auto resolved = resolve_storage_path(relative, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    return std::filesystem::exists(path) && std::filesystem::is_directory(path);
}

std::vector<std::string> storage_dir_list(const std::string& relative,
                                          Interpreter& interp,
                                          const std::string& builtin_name,
                                          const Location& loc) {
    auto resolved = resolve_storage_path(relative, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": not a directory",
                           interp.path(),
                           loc);
    }
    std::vector<std::string> entries;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        entries.push_back(entry.path().filename().generic_string());
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

} // namespace polonio
