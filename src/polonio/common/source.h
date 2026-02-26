#pragma once

#include <cstddef>
#include <string>

namespace polonio {

class Source {
public:
    Source(std::string path, std::string content);

    static Source from_file(const std::string& path);

    const std::string& path() const noexcept { return path_; }
    const std::string& content() const noexcept { return content_; }
    std::size_t size() const noexcept { return content_.size(); }

private:
    std::string path_;
    std::string content_;
};

} // namespace polonio
