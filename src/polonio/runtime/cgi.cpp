#include "polonio/runtime/cgi.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "polonio/common/error.h"

extern char** environ;

namespace polonio {

namespace {

std::string get_env(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

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
            obj[entry.first] = Value(arr);
        }
    }
    return obj;
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
        auto trim = [](std::string s) {
            std::size_t first = s.find_first_not_of(" \t");
            std::size_t last = s.find_last_not_of(" \t");
            if (first == std::string::npos) return std::string();
            return s.substr(first, last - first + 1);
        };
        name = trim(name);
        value = trim(value);
        if (!name.empty()) {
            obj[name] = Value(url_decode(value));
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return obj;
}

Value::Object build_server_object() {
    static const char* keys[] = {"REQUEST_METHOD", "QUERY_STRING", "CONTENT_TYPE", "CONTENT_LENGTH",
                                 "SCRIPT_NAME",   "SCRIPT_FILENAME", "PATH_INFO",    "REMOTE_ADDR",
                                 "HTTP_HOST",     "HTTP_USER_AGENT", "HTTP_ACCEPT"};
    Value::Object server;
    for (const char* key : keys) {
        std::string value = get_env(key);
        if (!value.empty()) {
            server[key] = Value(value);
        }
    }
    for (char** env = environ; env && *env; ++env) {
        std::string entry(*env);
        std::size_t eq = entry.find('=');
        if (eq == std::string::npos) continue;
        std::string name = entry.substr(0, eq);
        if (name.rfind("HTTP_", 0) == 0) {
            std::string val = entry.substr(eq + 1);
            server[name] = Value(val);
        }
    }
    return server;
}

Value::Object parse_post_body(const std::string& body) {
    if (body.empty()) return Value::Object();
    return entries_to_object(parse_urlencoded_entries(body));
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

Value::Object build_headers_object() {
    Value::Object headers;
    auto add_header = [&](const std::string& key, const std::string& value) {
        if (!value.empty()) {
            headers[key] = Value(value);
        }
    };
    add_header("content-type", get_env("CONTENT_TYPE"));
    add_header("content-length", get_env("CONTENT_LENGTH"));
    for (char** env = environ; env && *env; ++env) {
        std::string entry(*env);
        std::size_t eq = entry.find('=');
        if (eq == std::string::npos) continue;
        std::string name = entry.substr(0, eq);
        if (name.rfind("HTTP_", 0) == 0) {
            std::string raw = name.substr(5);
            add_header(normalize_header_key(raw), entry.substr(eq + 1));
        }
    }
    return headers;
}

std::string read_request_body(const std::string& content_length) {
    if (content_length.empty()) {
        return std::string();
    }
    std::size_t len = static_cast<std::size_t>(std::strtoul(content_length.c_str(), nullptr, 10));
    if (len == 0) {
        return std::string();
    }
    std::string body(len, '\0');
    std::cin.read(&body[0], len);
    body.resize(static_cast<std::size_t>(std::cin.gcount()));
    return body;
}

} // namespace

bool is_cgi_mode() { return std::getenv("GATEWAY_INTERFACE") != nullptr; }

CGIContext build_cgi_context() {
    CGIContext ctx;
    std::string script = get_env("SCRIPT_FILENAME");
    if (script.empty()) {
        script = get_env("PATH_TRANSLATED");
    }
    if (script.empty()) {
        throw PolonioError(ErrorKind::Runtime, "missing CGI script path");
    }
    ctx.script_filename = script;
    ctx.server = build_server_object();
    ctx.headers = build_headers_object();
    ctx.get = parse_query_string(get_env("QUERY_STRING"));
    ctx.cookie = parse_cookie_header(get_env("HTTP_COOKIE"));
    ctx.post = Value::Object();
    ctx.body.clear();

    std::string method = get_env("REQUEST_METHOD");
    std::string content_type = get_env("CONTENT_TYPE");
    std::string content_length = get_env("CONTENT_LENGTH");
    if (!content_length.empty()) {
        ctx.body = read_request_body(content_length);
    }

    if (!method.empty() && content_type.find("application/x-www-form-urlencoded") == 0 && !content_length.empty()) {
        ctx.post = parse_post_body(ctx.body);
    }

    return ctx;
}

} // namespace polonio
