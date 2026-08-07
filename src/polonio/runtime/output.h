#pragma once

#include <functional>
#include <string>

#include "polonio/runtime/value.h"

namespace polonio {

class OutputBuffer {
public:
    using Sink = std::function<void(const std::string&)>;

    void write(const Value& value);
    void write_text(const std::string& text);
    void set_sink(Sink sink, bool capture_output) {
        sink_ = std::move(sink);
        capture_output_ = capture_output;
    }
    const std::string& str() const { return buffer_; }
    void clear() { buffer_.clear(); }

    static std::string value_to_string(const Value& value);

private:
    std::string buffer_;
    Sink sink_;
    bool capture_output_ = true;
};

} // namespace polonio
