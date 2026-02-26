#include "polonio/common/source.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace polonio {

Source::Source(std::string path, std::string content)
    : path_(std::move(path)), content_(std::move(content)) {}

Source Source::from_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("failed to open source file: " + path);
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        throw std::runtime_error("failed to read source file: " + path);
    }

    return Source(path, buffer.str());
}

} // namespace polonio
