#pragma once

#include <cstddef>

namespace polonio {

struct Location {
    std::size_t offset;
    int line;
    int column;

    static Location start() { return Location{0, 1, 1}; }
};

struct Span {
    Location start;
    Location end;
};

} // namespace polonio
