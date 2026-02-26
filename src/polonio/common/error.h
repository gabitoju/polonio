#pragma once

#include <stdexcept>
#include <string>

#include "polonio/common/location.h"

namespace polonio {

enum class ErrorKind {
    IO,
    Lex,
    Parse,
    Runtime,
};

class PolonioError : public std::runtime_error {
public:
    PolonioError(ErrorKind kind,
                 std::string message,
                 std::string path = std::string(),
                 Location location = Location::start());

    ErrorKind kind() const noexcept { return kind_; }
    const std::string& path() const noexcept { return path_; }
    const Location& location() const noexcept { return location_; }
    const std::string& message() const noexcept { return message_; }

    std::string format() const;

private:
    ErrorKind kind_;
    std::string message_;
    std::string path_;
    Location location_;
};

} // namespace polonio
