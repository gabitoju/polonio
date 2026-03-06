#pragma once

#include <string>
#include <vector>

namespace polonio {

class Interpreter;
struct Location;

std::string storage_file_read(const std::string& relative,
                              Interpreter& interp,
                              const std::string& builtin_name,
                              const Location& loc);
void storage_file_write(const std::string& relative,
                        const std::string& content,
                        Interpreter& interp,
                        const std::string& builtin_name,
                        const Location& loc);
bool storage_file_exists(const std::string& relative,
                         Interpreter& interp,
                         const std::string& builtin_name,
                         const Location& loc);
bool storage_dir_create(const std::string& relative,
                        Interpreter& interp,
                        const std::string& builtin_name,
                        const Location& loc);
std::vector<std::string> storage_dir_list(const std::string& relative,
                                          Interpreter& interp,
                                          const std::string& builtin_name,
                                          const Location& loc);

} // namespace polonio
