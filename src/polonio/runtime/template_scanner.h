#pragma once

#include <string>
#include <vector>

#include "polonio/common/location.h"

namespace polonio {

class Source;

struct TemplateSegment {
    enum class Kind { Text, Code };
    Kind kind;
    std::string content;
    Location location;
};

std::vector<TemplateSegment> scan_template(const Source& source);

} // namespace polonio
