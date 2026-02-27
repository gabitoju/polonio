#pragma once

#include <string>

#include "polonio/runtime/value.h"

namespace polonio {

class OutputBuffer {
public:
    void write(const Value& value);
    void write_text(const std::string& text) { buffer_ += text; }
    const std::string& str() const { return buffer_; }
    void clear() { buffer_.clear(); }

    static std::string value_to_string(const Value& value);

private:
    std::string buffer_;
};

} // namespace polonio
