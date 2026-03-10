#include "polonio/runtime/db.h"

#include <filesystem>
#include <limits>
#include <string>

#include "polonio/common/error.h"
#include "polonio/runtime/interpreter.h"
#include "polonio/runtime/storage.h"

namespace polonio {

DatabaseConnection::~DatabaseConnection() { close(); }

void DatabaseConnection::close() {
    if (handle_) {
        sqlite3_close(handle_);
        handle_ = nullptr;
    }
}

void DatabaseConnection::connect_relative(const std::string& relative_path,
                                          Interpreter& interp,
                                          const std::string& builtin_name,
                                          const Location& loc) {
    auto resolved = resolve_storage_path(relative_path, interp, builtin_name, loc);
    std::filesystem::path path(resolved);
    auto parent = path.parent_path();
    if (!parent.empty()) {
        if (!std::filesystem::exists(parent) || !std::filesystem::is_directory(parent)) {
            throw PolonioError(ErrorKind::Runtime,
                               builtin_name + ": missing directory",
                               interp.path(),
                               loc);
        }
    } else {
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": missing directory",
                           interp.path(),
                           loc);
    }
    close();
    sqlite3* new_handle = nullptr;
    int rc = sqlite3_open(resolved.c_str(), &new_handle);
    if (rc != SQLITE_OK) {
        std::string message = "unknown error";
        if (new_handle) {
            message = sqlite3_errmsg(new_handle);
            sqlite3_close(new_handle);
        }
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": sqlite open failed: " + message,
                           interp.path(),
                           loc);
    }
    handle_ = new_handle;
}

sqlite3* require_db_handle(Interpreter& interp,
                           const std::string& builtin_name,
                           const Location& loc) {
    (void)builtin_name;
    auto* conn = interp.db_connection();
    if (!conn || !conn->is_open()) {
        throw PolonioError(ErrorKind::Runtime,
                           "database not connected",
                           interp.path(),
                           loc);
    }
    return conn->handle();
}

} // namespace polonio
