#pragma once

#include <cstddef>
#include <string_view>

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

inline Location advance(Location loc, char c) {
    loc.offset += 1;
    if (c == '\n') {
        loc.line += 1;
        loc.column = 1;
    } else {
        loc.column += 1;
    }
    return loc;
}

inline Location advance(Location loc, std::string_view text) {
    for (char c : text) {
        loc = advance(loc, c);
    }
    return loc;
}

} // namespace polonio
