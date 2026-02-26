#pragma once

#include <string>

#include "polonio/runtime/value.h"

namespace polonio {

class OutputBuffer {
public:
    void write(const Value& value);
    const std::string& str() const { return buffer_; }
    void clear() { buffer_.clear(); }

    static std::string value_to_string(const Value& value);

private:
    std::string buffer_;
};

} // namespace polonio
