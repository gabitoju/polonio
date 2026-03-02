#pragma once

#include <string>
#include <utility>

#include "polonio/runtime/value.h"

namespace polonio {

struct CGIContext {
    Value::Object get;
    Value::Object post;
    Value::Object cookie;
    Value::Object server;
    std::string script_filename;
};

bool is_cgi_mode();
CGIContext build_cgi_context();

} // namespace polonio
