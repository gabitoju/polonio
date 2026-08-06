#pragma once

#include <stdexcept>
#include <optional>
#include <string>
#include <vector>

#include "polonio/common/location.h"

namespace polonio {

enum class ErrorCategory {
    Source,
    Lex,
    Parse,
    Runtime,
    Capability,
    Resource,
    Internal,
    // Compatibility spelling for pre-RFC-0004 callers.
    IO = Source,
};

using ErrorKind = ErrorCategory;

struct ErrorDetails {
    std::string operation;
    std::string function_name;
    std::optional<std::size_t> argument_index;
    std::string capability;
    std::string resource;
    std::vector<std::string> include_chain;
    std::string host_detail;
};

class PolonioError : public std::runtime_error {
public:
    PolonioError(ErrorCategory category,
                 std::string message,
                 std::string path = std::string());
    PolonioError(ErrorCategory category,
                 std::string message,
                 std::string path,
                 Location location,
                 ErrorDetails details = {});

    ErrorCategory category() const noexcept { return category_; }
    ErrorKind kind() const noexcept { return category_; }
    const std::string& path() const noexcept { return path_; }
    const Location& location() const noexcept { return location_; }
    bool has_location() const noexcept { return has_location_; }
    const std::string& message() const noexcept { return message_; }
    const ErrorDetails& details() const noexcept { return details_; }
    void add_include_frame(std::string frame);

    std::string format() const;

private:
    ErrorCategory category_;
    std::string message_;
    std::string path_;
    Location location_;
    bool has_location_ = false;
    ErrorDetails details_;
};

} // namespace polonio
