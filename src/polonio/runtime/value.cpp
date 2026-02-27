#include "polonio/runtime/value.h"

#include <type_traits>
#include <utility>

namespace polonio {

Value::Value() : storage_(std::monostate{}) {}

Value::Value(std::nullptr_t) : storage_(std::monostate{}) {}

Value::Value(bool b) : storage_(b) {}

Value::Value(double d) : storage_(d) {}

Value::Value(int i) : storage_(static_cast<double>(i)) {}

Value::Value(const std::string& s) : storage_(s) {}

Value::Value(std::string&& s) : storage_(std::move(s)) {}

Value::Value(const char* s) : storage_(std::string(s)) {}

Value::Value(const Array& array) : storage_(array) {}

Value::Value(Array&& array) : storage_(std::move(array)) {}

Value::Value(const Object& object) : storage_(object) {}

Value::Value(Object&& object) : storage_(std::move(object)) {}

Value::Value(FunctionValue fn) : storage_(std::move(fn)) {}

Value::Value(BuiltinFunction fn) : storage_(std::move(fn)) {}

std::string Value::type_name() const {
    return std::visit(
        [](const auto& alt) -> std::string {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                return "bool";
            } else if constexpr (std::is_same_v<T, double>) {
                return "number";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "string";
            } else if constexpr (std::is_same_v<T, Array>) {
                return "array";
            } else if constexpr (std::is_same_v<T, Object>) {
                return "object";
            } else if constexpr (std::is_same_v<T, BuiltinFunction>) {
                return "function";
            } else {
                return "function";
            }
        },
        storage_);
}

bool Value::is_truthy() const {
    return std::visit(
        [](const auto& alt) -> bool {
            using T = std::decay_t<decltype(alt)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return false;
            } else if constexpr (std::is_same_v<T, bool>) {
                return alt;
            } else if constexpr (std::is_same_v<T, double>) {
                return alt != 0.0;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return !alt.empty();
            } else {
                return true;
            }
        },
        storage_);
}

bool Value::operator==(const Value& other) const {
    return std::visit(
        [](const auto& lhs, const auto& rhs) -> bool {
            using L = std::decay_t<decltype(lhs)>;
            using R = std::decay_t<decltype(rhs)>;
            if constexpr (!std::is_same_v<L, R>) {
                return false;
            } else if constexpr (std::is_same_v<L, std::monostate>) {
                return true;
            } else {
                return lhs == rhs;
            }
        },
        storage_,
        other.storage_);
}

bool Value::operator!=(const Value& other) const { return !(*this == other); }

const Value::Storage& Value::storage() const { return storage_; }

Value::Storage& Value::storage() { return storage_; }

} // namespace polonio
