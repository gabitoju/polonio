#include "polonio/runtime/output.h"

#include <sstream>
#include <locale>
#include <cmath>
#include <iomanip>
#include <type_traits>

namespace polonio {

namespace {

std::string format_number(double value) {
    if (!std::isfinite(value)) return "[number]";
    if (value == 0.0) return "0";
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::setprecision(6) << value;
    return oss.str();
}

} // namespace

void OutputBuffer::write(const Value& value) { write_text(value_to_string(value)); }

void OutputBuffer::write_text(const std::string& text) {
    if (sink_) {
        sink_(text);
    }
    if (capture_output_) {
        buffer_ += text;
    }
}

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
            } else if constexpr (std::is_same_v<T, Value::ObjectPtr> ||
                                 std::is_same_v<T, Value::ReadOnlyObjectPtr>) {
                return "[object]";
            } else {
                return "[function]";
            }
        },
        value.storage());
}

} // namespace polonio
