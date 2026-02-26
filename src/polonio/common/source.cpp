#include "polonio/common/source.h"

#include "polonio/common/error.h"
#include "polonio/common/location.h"

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
        throw PolonioError(ErrorKind::IO, "failed to open source file", path, Location::start());
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        throw PolonioError(ErrorKind::IO, "failed to read source file", path, Location::start());
    }

    return Source(path, buffer.str());
}

} // namespace polonio
