#pragma once

#include <filesystem>
#include <string>

namespace polonio {

struct ServerConfig {
    std::filesystem::path root;
    int port = 8080;
};

void run_http_server(const ServerConfig& config);
std::string simulate_http_request(const ServerConfig& config,
                                  const std::string& raw_request,
                                  const std::string& client_address = "127.0.0.1");

} // namespace polonio
