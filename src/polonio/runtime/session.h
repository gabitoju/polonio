#pragma once

#include <functional>
#include <string>

#include "polonio/runtime/value.h"

namespace polonio {

struct SessionContext {
    Value::Object data;
    bool dirty = false;
    bool is_cgi = false;
    bool secret_missing = false;
    std::string secret;
};

bool decode_session_cookie(const std::string& cookie_value,
                           const std::string& secret,
                           Value::Object& out);

std::string encode_session_cookie(const Value::Object& data,
                                  const std::string& secret,
                                  const std::function<void(const std::string&)>& on_error);

} // namespace polonio
