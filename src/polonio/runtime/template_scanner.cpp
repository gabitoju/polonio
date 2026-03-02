#include "polonio/runtime/template_scanner.h"

#include <string>
#include <vector>

#include "polonio/common/error.h"
#include "polonio/common/location.h"
#include "polonio/common/source.h"

namespace polonio {

std::vector<TemplateSegment> scan_template(const Source& source) {
    const std::string& input = source.content();
    std::vector<TemplateSegment> segments;
    std::string current;
    bool in_code = false;
    Location loc = Location::start();
    Location segment_loc = loc;
    std::size_t i = 0;
    auto flush_segment = [&](TemplateSegment::Kind kind) {
        if (!current.empty()) {
            segments.push_back(TemplateSegment{kind, current, segment_loc});
            current.clear();
        }
        segment_loc = loc;
    };

    while (i < input.size()) {
        if (!in_code && input[i] == '<' && i + 1 < input.size() && input[i + 1] == '%') {
            flush_segment(TemplateSegment::Kind::Text);
            i += 2;
            loc = advance(loc, '<');
            loc = advance(loc, '%');
            segment_loc = loc;
            in_code = true;
            continue;
        }
        if (in_code && input[i] == '%' && i + 1 < input.size() && input[i + 1] == '>') {
            flush_segment(TemplateSegment::Kind::Code);
            i += 2;
            loc = advance(loc, '%');
            loc = advance(loc, '>');
            segment_loc = loc;
            in_code = false;
            continue;
        }
        current.push_back(input[i]);
        loc = advance(loc, input[i]);
        i += 1;
    }

    if (in_code) {
        throw PolonioError(ErrorKind::Parse, "unterminated template block", source.path(), segment_loc);
    }
    flush_segment(TemplateSegment::Kind::Text);
    return segments;
}

} // namespace polonio
