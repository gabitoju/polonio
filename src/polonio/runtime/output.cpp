#include "polonio/runtime/output.h"

#include <sstream>
#include <type_traits>

namespace polonio {

namespace {

std::string format_number(double value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

} // namespace

void OutputBuffer::write(const Value& value) { buffer_ += value_to_string(value); }

std::string OutputBuffer::value_to_string(const Value& value) {
    return std::visit(
        [](const auto& alt) -> std::string {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "";
            } else if constexpr (std::is_same_v<T, bool>) {
                return alt ? "true" : "false";
            } else if constexpr (std::is_same_v<T, double>) {
                return format_number(alt);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return alt;
            } else if constexpr (std::is_same_v<T, Value::Array>) {
                return "[array]";
            } else if constexpr (std::is_same_v<T, Value::Object>) {
                return "[object]";
            } else {
                return "[function]";
            }
        },
        value.storage());
}

} // namespace polonio
