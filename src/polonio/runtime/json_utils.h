#pragma once

#include <functional>
#include <string>

#include "polonio/runtime/value.h"

namespace polonio {

using JsonErrorFn = std::function<void(const std::string&)>;

Value parse_json_string(const std::string& text, const JsonErrorFn& on_error);
std::string serialize_json_value(const Value& value, const JsonErrorFn& on_error);
void ensure_json_serializable(const Value& value, const JsonErrorFn& on_error);

} // namespace polonio
