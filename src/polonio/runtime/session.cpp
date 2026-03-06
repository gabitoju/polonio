#include "polonio/runtime/session.h"

#include <stdexcept>
#include <string>
#include <vector>

#include "polonio/runtime/crypto.h"
#include "polonio/runtime/json_utils.h"

namespace polonio {

namespace {

std::string hex_encode(const std::string& data) {
    static const char* hex_digits = "0123456789abcdef";
    std::string out(data.size() * 2, '\0');
    for (std::size_t i = 0; i < data.size(); ++i) {
        unsigned char byte = static_cast<unsigned char>(data[i]);
        out[i * 2] = hex_digits[(byte >> 4) & 0xF];
        out[i * 2 + 1] = hex_digits[byte & 0xF];
    }
    return out;
}

bool hex_decode(const std::string& input, std::string& output) {
    if (input.size() % 2 != 0) return false;
    output.resize(input.size() / 2);
    auto hex_value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < output.size(); ++i) {
        int hi = hex_value(input[i * 2]);
        int lo = hex_value(input[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        output[i] = static_cast<char>((hi << 4) | lo);
    }
    return true;
}

} // namespace

bool decode_session_cookie(const std::string& cookie_value,
                           const std::string& secret,
                           Value::Object& out) {
    auto dot = cookie_value.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string payload_b64 = cookie_value.substr(0, dot);
    std::string sig_hex = cookie_value.substr(dot + 1);
    std::string payload;
    if (!base64url_decode(payload_b64, payload)) {
        return false;
    }
    std::string signature_bytes;
    if (!hex_decode(sig_hex, signature_bytes)) {
        return false;
    }
    std::string expected = hmac_sha256(secret, payload);
    if (!constant_time_equals(signature_bytes, expected)) {
        return false;
    }
    try {
        Value parsed = parse_json_string(payload, [](const std::string&) {
            throw std::runtime_error("invalid json");
        });
        if (!std::holds_alternative<Value::ObjectPtr>(parsed.storage())) {
            return false;
        }
        auto obj = std::get<Value::ObjectPtr>(parsed.storage());
        if (!obj) return false;
        out = *obj;
        return true;
    } catch (...) {
        return false;
    }
}

std::string encode_session_cookie(const Value::Object& data,
                                  const std::string& secret,
                                  const std::function<void(const std::string&)>& on_error) {
    Value wrapper(data);
    std::string payload = serialize_json_value(wrapper, on_error);
    std::string signature = hmac_sha256(secret, payload);
    std::string value = base64url_encode(payload);
    value.push_back('.');
    value += hex_encode(signature);
    return value;
}

} // namespace polonio
