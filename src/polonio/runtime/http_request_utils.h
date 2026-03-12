#pragma once

#include <string>

#include "polonio/runtime/value.h"

namespace polonio::http {

std::string url_decode(const std::string& input);
Value::Object parse_query_string(const std::string& qs);
Value::Object parse_cookie_header(const std::string& header);
Value::Object parse_post_body(const std::string& body);
void append_form_value(Value::Object& target, const std::string& name, const Value& value);
std::string trim_whitespace(const std::string& input);
std::string to_lower_copy(std::string value);
std::string normalize_header_key(const std::string& name);

} // namespace polonio::http
