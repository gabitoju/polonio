#pragma once

#include <string>

namespace polonio {

std::string base64url_encode(const std::string& input);
bool base64url_decode(const std::string& input, std::string& output);
std::string hmac_sha256(const std::string& key, const std::string& data);

} // namespace polonio
