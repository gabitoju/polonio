#include "polonio/runtime/http_request_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace polonio::http {

namespace {

using EntryMap = std::unordered_map<std::string, std::vector<std::string>>;

EntryMap parse_urlencoded_entries(const std::string& data) {
    EntryMap entries;
    std::size_t start = 0;
    while (start <= data.size()) {
        std::size_t amp = data.find('&', start);
        std::string pair = (amp == std::string::npos) ? data.substr(start) : data.substr(start, amp - start);
        std::size_t eq = pair.find('=');
        std::string name_enc = (eq == std::string::npos) ? pair : pair.substr(0, eq);
        std::string value_enc = (eq == std::string::npos) ? std::string() : pair.substr(eq + 1);
        std::string name = url_decode(name_enc);
        std::string value = url_decode(value_enc);
        entries[name].push_back(value);
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return entries;
}

Value::Object entries_to_object(const EntryMap& entries) {
    Value::Object obj;
    for (const auto& entry : entries) {
        if (entry.second.empty()) {
            obj[entry.first] = Value();
            continue;
        }
        if (entry.second.size() == 1) {
            obj[entry.first] = Value(entry.second.front());
        } else {
            Value::Array arr;
            for (const auto& val : entry.second) {
                arr.emplace_back(val);
            }
            obj[entry.first] = Value(std::move(arr));
        }
    }
    return obj;
}

} // namespace

std::string url_decode(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < input.size() && std::isxdigit(static_cast<unsigned char>(input[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(input[i + 2]))) {
            auto hex = input.substr(i + 1, 2);
            char decoded = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            out.push_back(decoded);
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

Value::Object parse_query_string(const std::string& qs) { return entries_to_object(parse_urlencoded_entries(qs)); }

Value::Object parse_cookie_header(const std::string& header) {
    Value::Object obj;
    std::size_t start = 0;
    while (start < header.size()) {
        std::size_t semi = header.find(';', start);
        std::string pair = (semi == std::string::npos) ? header.substr(start) : header.substr(start, semi - start);
        std::size_t eq = pair.find('=');
        std::string name = (eq == std::string::npos) ? pair : pair.substr(0, eq);
        std::string value = (eq == std::string::npos) ? std::string() : pair.substr(eq + 1);
        name = trim_whitespace(name);
        value = trim_whitespace(value);
        if (!name.empty()) {
            obj[name] = Value(url_decode(value));
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return obj;
}

Value::Object parse_post_body(const std::string& body) {
    if (body.empty()) return Value::Object();
    return entries_to_object(parse_urlencoded_entries(body));
}

void append_form_value(Value::Object& target, const std::string& name, const Value& value) {
    auto it = target.find(name);
    if (it == target.end()) {
        target[name] = value;
        return;
    }
    auto& existing = it->second;
    auto& storage = existing.storage();
    if (std::holds_alternative<Value::ArrayPtr>(storage)) {
        auto arr = std::get<Value::ArrayPtr>(storage);
        if (!arr) {
            arr = std::make_shared<Value::Array>();
            storage = arr;
        }
        arr->push_back(value);
    } else {
        Value::Array arr;
        arr.push_back(existing);
        arr.push_back(value);
        existing = Value(std::move(arr));
    }
}

std::string trim_whitespace(const std::string& input) {
    std::size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }
    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }
    return input.substr(start, end - start);
}

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string normalize_header_key(const std::string& name) {
    std::string normalized;
    normalized.reserve(name.size());
    for (char ch : name) {
        if (ch == '_') {
            normalized.push_back('-');
        } else {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return normalized;
}

} // namespace polonio::http
