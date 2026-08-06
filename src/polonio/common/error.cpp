#include "polonio/common/error.h"

#include <sstream>
#include <utility>

namespace polonio {

namespace {
const char* category_name(ErrorCategory category) {
    switch (category) {
    case ErrorCategory::Source: return "SourceError";
    case ErrorCategory::Lex: return "LexError";
    case ErrorCategory::Parse: return "ParseError";
    case ErrorCategory::Runtime: return "RuntimeError";
    case ErrorCategory::Capability: return "CapabilityError";
    case ErrorCategory::Resource: return "ResourceError";
    case ErrorCategory::Internal: return "InternalError";
    }
    return "InternalError";
}
}

PolonioError::PolonioError(ErrorCategory category,
                           std::string message,
                           std::string path)
    : std::runtime_error(message.c_str()),
      category_(category),
      message_(std::move(message)),
      path_(std::move(path)),
      location_(Location::start()) {}

PolonioError::PolonioError(ErrorCategory category,
                           std::string message,
                           std::string path,
                           Location location,
                           ErrorDetails details)
    : std::runtime_error(message.c_str()), category_(category),
      message_(std::move(message)), path_(std::move(path)), location_(location),
      has_location_(true), details_(std::move(details)) {}

std::string PolonioError::format() const {
    std::ostringstream out;
    if (has_location_ && !path_.empty()) {
        out << path_ << ':';
    }
    if (has_location_) {
        out << location_.line << ':' << location_.column << ": ";
    }
    out << category_name(category_) << ": " << message_;
    for (const auto& include : details_.include_chain) {
        out << "\nincluded from: " << include;
    }
    return out.str();
}

void PolonioError::add_include_frame(std::string frame) {
    for (const auto& existing : details_.include_chain) {
        if (existing == frame) return;
    }
    details_.include_chain.push_back(std::move(frame));
}

} // namespace polonio
