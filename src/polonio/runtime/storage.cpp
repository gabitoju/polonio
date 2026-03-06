#include "polonio/runtime/storage.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "polonio/common/error.h"
#include "polonio/runtime/interpreter.h"
#include "polonio/common/location.h"

namespace polonio {

namespace {

std::string get_storage_root_internal() {
    const char* root = std::getenv("POLONIO_STORAGE_PATH");
    if (!root || std::string(root).empty()) {
        return std::string();
    }
    return root;
}

} // namespace

std::string storage_root(Interpreter& interp,
                         const std::string& builtin_name,
                         const Location& loc) {
    std::string root = get_storage_root_internal();
    if (root.empty()) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": missing storage root",
                           interp.path(),
                           loc);
    }
    return root;
}

std::string resolve_storage_path(const std::string& relative,
                                 Interpreter& interp,
                                 const std::string& builtin_name,
                                 const Location& loc) {
    std::string root = storage_root(interp, builtin_name, loc);
    if (relative.empty()) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": empty path",
                           interp.path(),
                           loc);
    }
    std::filesystem::path rel_path(relative);
    if (rel_path.is_absolute()) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": absolute path not allowed",
                           interp.path(),
                           loc);
    }
    auto normalized = rel_path.lexically_normal();
    for (const auto& part : normalized) {
        if (part == "..") {
            throw PolonioError(ErrorKind::Runtime,
                               builtin_name + ": path traversal",
                               interp.path(),
                               loc);
        }
    }
    std::filesystem::path base(root);
    std::filesystem::path combined = base / normalized;
    std::error_code ec;
    auto canonical_root = std::filesystem::weakly_canonical(base, ec);
    if (ec) canonical_root = base;
    auto canonical_combined = std::filesystem::weakly_canonical(combined, ec);
    if (ec) {
        canonical_combined = combined.lexically_normal();
        if (canonical_combined.has_parent_path()) {
            auto parent = canonical_combined.parent_path();
            auto canonical_parent = std::filesystem::weakly_canonical(parent, ec);
            if (!ec) {
                canonical_combined = canonical_parent / canonical_combined.filename();
            }
        }
    }
    auto root_str = canonical_root.generic_string();
    auto target_str = canonical_combined.generic_string();
    if (target_str.size() < root_str.size() ||
        target_str.compare(0, root_str.size(), root_str) != 0 ||
        (target_str.size() > root_str.size() && target_str[root_str.size()] != '/')) {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": path traversal",
                           interp.path(),
                           loc);
    }
    return target_str;
}

} // namespace polonio
