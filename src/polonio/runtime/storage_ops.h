#pragma once

#include <cstdint>
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
void storage_file_append(const std::string& relative,
                         const std::string& content,
                         Interpreter& interp,
                         const std::string& builtin_name,
                         const Location& loc);
bool storage_file_exists(const std::string& relative,
                         Interpreter& interp,
                         const std::string& builtin_name,
                         const Location& loc);
bool storage_file_delete(const std::string& relative,
                         Interpreter& interp,
                         const std::string& builtin_name,
                         const Location& loc);
std::uintmax_t storage_file_size(const std::string& relative,
                                 Interpreter& interp,
                                 const std::string& builtin_name,
                                 const Location& loc);
std::int64_t storage_file_modified(const std::string& relative,
                                   Interpreter& interp,
                                   const std::string& builtin_name,
                                   const Location& loc);
bool storage_dir_create(const std::string& relative,
                        Interpreter& interp,
                        const std::string& builtin_name,
                        const Location& loc);
bool storage_dir_exists(const std::string& relative,
                        Interpreter& interp,
                        const std::string& builtin_name,
                        const Location& loc);
std::vector<std::string> storage_dir_list(const std::string& relative,
                                          Interpreter& interp,
                                          const std::string& builtin_name,
                                          const Location& loc);

} // namespace polonio
