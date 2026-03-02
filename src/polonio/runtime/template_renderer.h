#pragma once

#include <string>

namespace polonio {

class Source;

class Interpreter;

std::string render_template(const Source& source);
std::string render_template_with_interpreter(const Source& source, Interpreter& interpreter);

} // namespace polonio
