#pragma once

#include <string>
#include <utility>

#include "polonio/runtime/value.h"

namespace polonio {

struct CGIContext {
    Value::Object get;
    Value::Object post;
    Value::Object files;
    Value::Object cookie;
    Value::Object server;
    Value::Object headers;
    std::string body;
    std::string script_filename;
    std::string request_method;
    std::string content_type;
    std::string content_length;
};

bool is_cgi_mode();
CGIContext build_cgi_context();
class Interpreter;
void process_request_body(CGIContext& ctx, Interpreter& interpreter);

} // namespace polonio
