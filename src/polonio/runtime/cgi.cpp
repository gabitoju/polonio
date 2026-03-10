#include "polonio/runtime/cgi.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "polonio/common/error.h"
#include "polonio/runtime/crypto.h"
#include "polonio/runtime/interpreter.h"
#include "polonio/runtime/storage.h"

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

std::string trim(const std::string& input) {
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

std::string random_hex_string(std::size_t nbytes, Interpreter& interpreter) {
    std::string bytes;
    if (!secure_random_bytes(bytes, nbytes)) {
        throw PolonioError(ErrorKind::Runtime,
                           "multipart upload: secure RNG failure",
                           interpreter.path(),
                           Location::start());
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(nbytes * 2);
    for (unsigned char ch : bytes) {
        out.push_back(hex[ch >> 4]);
        out.push_back(hex[ch & 0x0F]);
    }
    return out;
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
    ctx.files = Value::Object();
    ctx.body.clear();

    ctx.request_method = get_env("REQUEST_METHOD");
    ctx.content_type = get_env("CONTENT_TYPE");
    ctx.content_length = get_env("CONTENT_LENGTH");
    if (!ctx.content_length.empty()) {
        ctx.body = read_request_body(ctx.content_length);
    }

    return ctx;
}

void process_request_body(CGIContext& ctx, Interpreter& interpreter) {
    if (ctx.post.empty()) {
        ctx.post = Value::Object();
    }
    ctx.files = Value::Object();
    if (ctx.request_method.empty()) {
        return;
    }
    std::string method = to_lower_copy(ctx.request_method);
    if (method != "post") {
        return;
    }
    std::string content_type = ctx.content_type;
    if (content_type.empty()) {
        return;
    }
    std::string lowered = to_lower_copy(content_type);
    if (lowered.rfind("application/x-www-form-urlencoded", 0) == 0) {
        ctx.post = parse_post_body(ctx.body);
        return;
    }
    if (lowered.rfind("multipart/form-data", 0) != 0) {
        return;
    }
    auto runtime_error = [&](const std::string& message) {
        throw PolonioError(ErrorKind::Runtime, message, interpreter.path(), Location::start());
    };
    auto boundary_pos = content_type.find("boundary=");
    if (boundary_pos == std::string::npos) {
        runtime_error("invalid multipart body");
    }
    std::string boundary = content_type.substr(boundary_pos + 9);
    auto semi = boundary.find(';');
    if (semi != std::string::npos) {
        boundary = boundary.substr(0, semi);
    }
    boundary = trim(boundary);
    if (!boundary.empty() && boundary.front() == '"' && boundary.back() == '"') {
        boundary = boundary.substr(1, boundary.size() - 2);
    }
    if (boundary.empty()) {
        runtime_error("invalid multipart body");
    }
    if (ctx.body.empty()) {
        return;
    }
    std::string marker = "--" + boundary;
    const std::string& body = ctx.body;
    if (body.compare(0, marker.size(), marker) != 0) {
        runtime_error("invalid multipart body");
    }
    std::string root = storage_root(interpreter, "multipart upload", Location::start());
    std::filesystem::path root_path(root);
    std::filesystem::path uploads_rel = std::filesystem::path("tmp") / "uploads";
    std::filesystem::path uploads_dir = root_path / uploads_rel;
    std::error_code ec;
    std::filesystem::create_directories(uploads_dir, ec);
    if (ec) {
        runtime_error("unable to create upload directory");
    }
    std::size_t pos = marker.size();
    if (body.compare(pos, 2, "--") == 0) {
        return;
    }
    if (body.compare(pos, 2, "\r\n") != 0) {
        runtime_error("invalid multipart body");
    }
    pos += 2;
    while (true) {
        std::size_t headers_end = body.find("\r\n\r\n", pos);
        if (headers_end == std::string::npos) {
            runtime_error("invalid multipart body");
        }
        std::string header_block = body.substr(pos, headers_end - pos);
        pos = headers_end + 4;
        std::size_t delim = body.find("\r\n" + marker, pos);
        if (delim == std::string::npos) {
            runtime_error("invalid multipart body");
        }
        std::string data = body.substr(pos, delim - pos);
        pos = delim + 2 + marker.size();
        bool final = false;
        if (pos <= body.size() && body.compare(pos, 2, "--") == 0) {
            pos += 2;
            final = true;
            if (pos <= body.size() && body.compare(pos, 2, "\r\n") == 0) {
                pos += 2;
            }
        } else if (pos <= body.size() && body.compare(pos, 2, "\r\n") == 0) {
            pos += 2;
        } else if (pos == body.size()) {
            final = true;
        } else {
            runtime_error("invalid multipart body");
        }

        std::string field_name;
        std::string filename;
        std::string part_type;

        std::size_t line_start = 0;
        while (line_start < header_block.size()) {
            std::size_t line_end = header_block.find("\r\n", line_start);
            std::string line = header_block.substr(
                line_start,
                (line_end == std::string::npos ? header_block.size() : line_end) - line_start);
            if (line_end == std::string::npos) {
                line_start = header_block.size();
            } else {
                line_start = line_end + 2;
            }
            if (line.empty()) continue;
            auto colon = line.find(':');
            if (colon == std::string::npos) {
                runtime_error("invalid multipart body");
            }
            std::string key = to_lower_copy(trim(line.substr(0, colon)));
            std::string value = trim(line.substr(colon + 1));
            if (key == "content-disposition") {
                std::stringstream ss(value);
                std::string token;
                bool first_token = true;
                while (std::getline(ss, token, ';')) {
                    token = trim(token);
                    if (token.empty()) continue;
                    if (first_token) {
                        first_token = false;
                        continue;
                    }
                    auto eq = token.find('=');
                    if (eq == std::string::npos) continue;
                    std::string param_key = to_lower_copy(trim(token.substr(0, eq)));
                    std::string param_value = trim(token.substr(eq + 1));
                    if (!param_value.empty() && param_value.front() == '"' && param_value.back() == '"') {
                        param_value = param_value.substr(1, param_value.size() - 2);
                    }
                    if (param_key == "name") {
                        field_name = param_value;
                    } else if (param_key == "filename") {
                        filename = param_value;
                    }
                }
            } else if (key == "content-type") {
                part_type = value;
            }
        }

        if (field_name.empty()) {
            runtime_error("invalid multipart body");
        }

        if (filename.empty()) {
            append_form_value(ctx.post, field_name, Value(data));
        } else {
            std::string extension;
            auto dot = filename.find_last_of('.');
            if (dot != std::string::npos) {
                extension = filename.substr(dot);
            }
            std::string random_name = random_hex_string(16, interpreter);
            std::filesystem::path relative_file = uploads_rel / (random_name + extension);
            std::filesystem::path absolute_file = root_path / relative_file;
            std::ofstream out(absolute_file, std::ios::binary);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!out) {
                runtime_error("unable to write upload");
            }
            Value::Object file_entry;
            file_entry["name"] = Value(filename);
            file_entry["type"] = Value(part_type.empty() ? std::string("application/octet-stream") : part_type);
            file_entry["size"] = Value(static_cast<double>(data.size()));
            file_entry["tmp_path"] = Value(relative_file.generic_string());
            append_form_value(ctx.files, field_name, Value(std::move(file_entry)));
        }

        if (final) {
            break;
        }
    }
}

} // namespace polonio
