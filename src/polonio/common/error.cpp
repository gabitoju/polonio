#include "polonio/common/error.h"

#include <sstream>
#include <utility>

namespace polonio {

PolonioError::PolonioError(ErrorKind kind,
                           std::string message,
                           std::string path,
                           Location location)
    : std::runtime_error(message.c_str()),
      kind_(kind),
      message_(std::move(message)),
      path_(std::move(path)),
      location_(location) {}

std::string PolonioError::format() const {
    std::ostringstream out;
    if (!path_.empty()) {
        out << path_ << ':';
    }
    out << location_.line << ':' << location_.column << ": " << message_;
    return out.str();
}

} // namespace polonio
