#pragma once

#include <string>
#include <vector>

namespace polonio {

class Interpreter;
struct Location;

std::string resolve_storage_path(const std::string& relative,
                                 Interpreter& interp,
                                 const std::string& builtin_name,
                                 const Location& loc);
std::string storage_root(Interpreter& interp,
                         const std::string& builtin_name,
                         const Location& loc);

} // namespace polonio
