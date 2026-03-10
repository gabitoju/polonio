#pragma once

#include <string>

#include <sqlite3.h>

namespace polonio {

class Interpreter;
struct Location;

class DatabaseConnection {
public:
    DatabaseConnection() = default;
    ~DatabaseConnection();

    void connect_relative(const std::string& relative_path,
                          Interpreter& interp,
                          const std::string& builtin_name,
                          const Location& loc);
    void close();
    bool is_open() const { return handle_ != nullptr; }
    sqlite3* handle() const { return handle_; }

private:
    sqlite3* handle_ = nullptr;
};

sqlite3* require_db_handle(Interpreter& interp,
                           const std::string& builtin_name,
                           const Location& loc);

} // namespace polonio

