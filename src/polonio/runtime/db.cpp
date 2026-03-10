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
    transaction_active_ = false;
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
    transaction_active_ = false;
}

namespace {

void sqlite_exec_or_throw(sqlite3* handle,
                          const std::string& sql,
                          const std::string& builtin_name,
                          Interpreter& interp,
                          const Location& loc) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string message;
        if (errmsg) {
            message = errmsg;
            sqlite3_free(errmsg);
        } else {
            message = sqlite3_errmsg(handle);
        }
        throw PolonioError(ErrorKind::Runtime,
                           builtin_name + ": sqlite error: " + message,
                           interp.path(),
                           loc);
    }
}

} // namespace

void DatabaseConnection::begin_transaction(const std::string& builtin_name,
                                           Interpreter& interp,
                                           const Location& loc) {
    if (!handle_) {
        throw PolonioError(ErrorKind::Runtime,
                           "database not connected",
                           interp.path(),
                           loc);
    }
    if (transaction_active_) {
        throw PolonioError(ErrorKind::Runtime,
                           "transaction already active",
                           interp.path(),
                           loc);
    }
    sqlite_exec_or_throw(handle_, "BEGIN", builtin_name, interp, loc);
    transaction_active_ = true;
}

void DatabaseConnection::commit_transaction(const std::string& builtin_name,
                                            Interpreter& interp,
                                            const Location& loc) {
    if (!handle_) {
        throw PolonioError(ErrorKind::Runtime,
                           "database not connected",
                           interp.path(),
                           loc);
    }
    if (!transaction_active_) {
        throw PolonioError(ErrorKind::Runtime,
                           "no active transaction",
                           interp.path(),
                           loc);
    }
    sqlite_exec_or_throw(handle_, "COMMIT", builtin_name, interp, loc);
    transaction_active_ = false;
}

void DatabaseConnection::rollback_transaction(const std::string& builtin_name,
                                              Interpreter& interp,
                                              const Location& loc) {
    if (!handle_) {
        throw PolonioError(ErrorKind::Runtime,
                           "database not connected",
                           interp.path(),
                           loc);
    }
    if (!transaction_active_) {
        throw PolonioError(ErrorKind::Runtime,
                           "no active transaction",
                           interp.path(),
                           loc);
    }
    sqlite_exec_or_throw(handle_, "ROLLBACK", builtin_name, interp, loc);
    transaction_active_ = false;
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
