#pragma once

#include <filesystem>

namespace polonio {

struct ServerConfig {
    std::filesystem::path root;
    int port = 8080;
};

void run_http_server(const ServerConfig& config);

} // namespace polonio
