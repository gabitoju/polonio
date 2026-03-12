#include "polonio/server/http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "polonio/common/error.h"
#include "polonio/common/source.h"
#include "polonio/runtime/cgi.h"
#include "polonio/runtime/env.h"
#include "polonio/runtime/http_request_utils.h"
#include "polonio/runtime/interpreter.h"
#include "polonio/runtime/session.h"
#include "polonio/runtime/template_renderer.h"

namespace polonio {
namespace {

constexpr std::size_t kMaxHeaderBytes = 64 * 1024;
constexpr std::size_t kMaxBodyBytes = 8 * 1024 * 1024;

[[noreturn]] void throw_system_error(const char* operation) {
    throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string to_lower_ascii(const std::string& input) {
    std::string lowered;
    lowered.reserve(input.size());
    for (char ch : input) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered;
}

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version;
    std::vector<std::pair<std::string, std::string>> headers;
    std::unordered_map<std::string, std::string> header_lookup;
    std::string body;

    std::string header(const std::string& name) const {
        auto it = header_lookup.find(to_lower_ascii(name));
        if (it != header_lookup.end()) {
            return it->second;
        }
        return {};
    }
};

struct RequestInfo {
    std::string path;
    std::string query;
    std::string request_uri;
};

struct ResolvedResource {
    enum class Kind { Template, Static };
    Kind kind = Kind::Static;
    std::filesystem::path path;
};

struct ServerState {
    std::filesystem::path root;
    std::string root_string;
    int port = 0;
};

bool send_all(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        ssize_t sent = ::send(fd, data.data() + offset, data.size() - offset, 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (sent == 0) break;
        offset += static_cast<std::size_t>(sent);
    }
    return offset == data.size();
}

HttpRequest parse_http_request_string(const std::string& raw) {
    HttpRequest request;
    auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw std::runtime_error("invalid HTTP request");
    }
    std::string header_block = raw.substr(0, header_end);
    std::size_t pos = 0;
    bool first_line = true;
    while (pos <= header_block.size()) {
        std::size_t line_end = header_block.find("\r\n", pos);
        std::string line = header_block.substr(
            pos,
            line_end == std::string::npos ? header_block.size() - pos : line_end - pos);
        if (line.empty()) {
            if (line_end == std::string::npos) break;
            pos = line_end + 2;
            continue;
        }
        if (first_line) {
            first_line = false;
            auto first_space = line.find(' ');
            auto second_space = line.find(' ', first_space == std::string::npos ? std::string::npos : first_space + 1);
            if (first_space == std::string::npos || second_space == std::string::npos) {
                throw std::runtime_error("invalid request line");
            }
            request.method = line.substr(0, first_space);
            request.target = line.substr(first_space + 1, second_space - first_space - 1);
            request.version = line.substr(second_space + 1);
        } else {
            auto colon = line.find(':');
            if (colon == std::string::npos) throw std::runtime_error("malformed header line");
            std::string name = line.substr(0, colon);
            std::string value = http::trim_whitespace(line.substr(colon + 1));
            request.headers.emplace_back(name, value);
            request.header_lookup[to_lower_ascii(name)] = value;
        }
        if (line_end == std::string::npos) break;
        pos = line_end + 2;
    }
    request.body = raw.substr(header_end + 4);
    return request;
}

bool read_http_request(int client_fd, HttpRequest& request) {
    std::string buffer;
    buffer.reserve(4096);
    char chunk[4096];
    std::size_t header_end = std::string::npos;
    while (true) {
        ssize_t n = ::recv(client_fd, chunk, sizeof(chunk), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw_system_error("recv");
        }
        if (n == 0) {
            if (buffer.empty()) {
                return false;
            }
            throw std::runtime_error("unexpected EOF");
        }
        buffer.append(chunk, static_cast<std::size_t>(n));
        if (buffer.size() > kMaxHeaderBytes) {
            throw std::runtime_error("request headers too large");
        }
        header_end = buffer.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            break;
        }
    }

    auto length_header_pos = buffer.find("Content-Length:");
    std::size_t content_length = 0;
    std::string content_length_value;
    if (length_header_pos != std::string::npos) {
        auto line_end = buffer.find("\r\n", length_header_pos);
        content_length_value =
            buffer.substr(length_header_pos + std::strlen("Content-Length:"), line_end - (length_header_pos + std::strlen("Content-Length:")));
        content_length_value = http::trim_whitespace(content_length_value);
    }
    if (!content_length_value.empty()) {
        try {
            long long parsed = std::stoll(content_length_value);
            if (parsed < 0) throw std::runtime_error("negative content length");
            content_length = static_cast<std::size_t>(parsed);
        } catch (const std::exception&) {
            throw std::runtime_error("invalid content-length");
        }
        if (content_length > kMaxBodyBytes) {
            throw std::runtime_error("request body too large");
        }
    }

    auto transfer_encoding = buffer.find("Transfer-Encoding:");
    if (transfer_encoding != std::string::npos) {
        throw std::runtime_error("transfer-encoding not supported");
    }

    std::size_t total_needed = header_end + 4 + content_length;
    while (buffer.size() < total_needed) {
        ssize_t n = ::recv(client_fd, chunk, sizeof(chunk), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw_system_error("recv");
        }
        if (n == 0) {
            throw std::runtime_error("unexpected EOF");
        }
        buffer.append(chunk, static_cast<std::size_t>(n));
        if (buffer.size() - header_end - 4 > kMaxBodyBytes) {
            throw std::runtime_error("request body too large");
        }
    }
    std::string complete = buffer.substr(0, header_end + 4 + content_length);
    request = parse_http_request_string(complete);
    return true;
}

std::string reason_phrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default: return "OK";
    }
}

std::string build_http_response(int status,
                                const std::vector<std::pair<std::string, std::string>>& headers,
                                const std::string& body) {
    bool has_connection = false;
    std::ostringstream stream;
    stream << "HTTP/1.1 " << status << ' ' << reason_phrase(status) << "\r\n";
    for (const auto& header : headers) {
        if (iequals(header.first, "Connection")) {
            has_connection = true;
        }
        if (iequals(header.first, "Content-Length")) {
            continue;
        }
        stream << header.first << ": " << header.second << "\r\n";
    }
    if (!has_connection) {
        stream << "Connection: close\r\n";
    }
    stream << "Content-Length: " << body.size() << "\r\n";
    stream << "\r\n";
    stream << body;
    return stream.str();
}

std::string plain_response(int status, const std::string& message) {
    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "text/plain; charset=utf-8"},
    };
    return build_http_response(status, headers, message);
}

std::string header_to_env_key(const std::string& name) {
    std::string key;
    key.reserve(name.size());
    for (char ch : name) {
        if (ch == '-') {
            key.push_back('_');
        } else {
            key.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
    }
    return key;
}

std::string remote_address_string(const sockaddr_in& addr) {
    char buffer[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &addr.sin_addr, buffer, sizeof(buffer))) {
        return std::string(buffer);
    }
    return "127.0.0.1";
}

bool parse_target(const std::string& target, RequestInfo& info) {
    if (target.empty()) {
        return false;
    }
    auto qpos = target.find('?');
    std::string raw_path = (qpos == std::string::npos) ? target : target.substr(0, qpos);
    info.query = (qpos == std::string::npos) ? std::string() : target.substr(qpos + 1);
    if (raw_path.empty()) raw_path = "/";
    if (raw_path.front() != '/') {
        return false;
    }
    std::string decoded = http::url_decode(raw_path);
    if (decoded.empty()) {
        decoded = "/";
    }
    if (decoded.front() != '/' || decoded.find('\0') != std::string::npos) {
        return false;
    }
    info.path = decoded;
    info.request_uri = decoded;
    if (!info.query.empty()) {
        info.request_uri.push_back('?');
        info.request_uri += info.query;
    }
    return true;
}

bool contains_parent_reference(const std::filesystem::path& path) {
    for (const auto& part : path) {
        if (part == "..") return true;
    }
    return false;
}

bool is_within_root(const ServerState& state, const std::filesystem::path& candidate) {
    auto candidate_str = candidate.generic_string();
    const auto& root = state.root_string;
    if (candidate_str.size() < root.size()) return false;
    if (candidate_str.compare(0, root.size(), root) != 0) return false;
    if (candidate_str.size() > root.size() && root.back() != '/' && candidate_str[root.size()] != '/') return false;
    return true;
}

std::optional<std::filesystem::path> find_regular_file(const ServerState& state, const std::filesystem::path& relative) {
    if (relative.empty()) return std::nullopt;
    std::filesystem::path combined = state.root / relative;
    std::error_code ec;
    if (!std::filesystem::exists(combined, ec) || !std::filesystem::is_regular_file(combined, ec)) {
        return std::nullopt;
    }
    auto canonical = std::filesystem::weakly_canonical(combined, ec);
    if (ec) {
        canonical = std::filesystem::absolute(combined);
    }
    if (!is_within_root(state, canonical)) {
        return std::nullopt;
    }
    return canonical;
}

std::optional<ResolvedResource> resolve_resource(const ServerState& state, const std::string& decoded_path) {
    std::string relative = decoded_path;
    while (!relative.empty() && relative.front() == '/') {
        relative.erase(relative.begin());
    }
    std::filesystem::path rel_path = relative.empty() ? std::filesystem::path() : std::filesystem::path(relative);
    rel_path = rel_path.lexically_normal();
    if (contains_parent_reference(rel_path)) {
        return std::nullopt;
    }

    auto ensure_resource = [&](const std::filesystem::path& candidate, ResolvedResource::Kind fallback)
                               -> std::optional<ResolvedResource> {
        auto canonical = find_regular_file(state, candidate);
        if (!canonical) return std::nullopt;
        ResolvedResource resource;
        resource.path = *canonical;
        auto ext = resource.path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".pol") {
            resource.kind = ResolvedResource::Kind::Template;
        } else {
            resource.kind = fallback;
        }
        return resource;
    };

    if (relative.empty()) {
        if (auto resource = ensure_resource("index.pol", ResolvedResource::Kind::Template)) return resource;
        if (auto resource = ensure_resource("index.html", ResolvedResource::Kind::Static)) return resource;
        return std::nullopt;
    }

    if (!rel_path.has_extension()) {
        auto pol_candidate = rel_path;
        pol_candidate += ".pol";
        if (auto resource = ensure_resource(pol_candidate, ResolvedResource::Kind::Template)) return resource;
    }

    std::error_code ec;
    if (!relative.empty()) {
        std::filesystem::path directory_path = state.root / rel_path;
        if (std::filesystem::is_directory(directory_path, ec)) {
            if (auto resource = ensure_resource(rel_path / "index.pol", ResolvedResource::Kind::Template)) return resource;
            if (auto resource = ensure_resource(rel_path / "index.html", ResolvedResource::Kind::Static)) return resource;
        }
    }

    if (auto resource = ensure_resource(rel_path, ResolvedResource::Kind::Static)) {
        return resource;
    }
    return std::nullopt;
}

std::string infer_static_mime_type(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".pdf") return "application/pdf";
    return "application/octet-stream";
}

std::string read_file_contents(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("unable to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

ServerState build_server_state(const ServerConfig& config) {
    ServerState state;
    std::error_code ec;
    state.root = std::filesystem::weakly_canonical(config.root, ec);
    if (ec) {
        state.root = std::filesystem::absolute(config.root);
    }
    state.root_string = state.root.generic_string();
    if (state.root_string.size() > 1 && state.root_string.back() == '/') {
        state.root_string.pop_back();
    }
    if (state.root_string.empty()) {
        state.root_string = "/";
    }
    state.port = config.port;
    return state;
}

void populate_server_superglobals(CGIContext& ctx,
                                  const HttpRequest& request,
                                  const RequestInfo& info,
                                  const sockaddr_in& client,
                                  const ServerState& state) {
    ctx.server = Value::Object();
    ctx.server["REQUEST_METHOD"] = Value(request.method);
    ctx.server["REQUEST_URI"] = Value(info.request_uri);
    ctx.server["QUERY_STRING"] = Value(info.query);
    ctx.server["SERVER_PROTOCOL"] = Value("HTTP/1.1");
    ctx.server["SCRIPT_FILENAME"] = Value(ctx.script_filename);
    ctx.server["SCRIPT_NAME"] = Value(info.path);
    ctx.server["PATH_INFO"] = Value(info.path);
    ctx.server["SERVER_PORT"] = Value(std::to_string(state.port));
    ctx.server["DOCUMENT_ROOT"] = Value(state.root_string);
    ctx.server["REMOTE_ADDR"] = Value(remote_address_string(client));
    auto host_header = request.header("host");
    if (!host_header.empty()) {
        ctx.server["HTTP_HOST"] = Value(host_header);
    }
    if (!ctx.content_type.empty()) {
        ctx.server["CONTENT_TYPE"] = Value(ctx.content_type);
    }
    if (!ctx.content_length.empty()) {
        ctx.server["CONTENT_LENGTH"] = Value(ctx.content_length);
    }
    for (const auto& header : request.headers) {
        std::string key = "HTTP_" + header_to_env_key(header.first);
        ctx.server[key] = Value(header.second);
    }
}

void populate_header_object(CGIContext& ctx, const HttpRequest& request) {
    Value::Object headers;
    for (const auto& header : request.headers) {
        headers[http::normalize_header_key(header.first)] = Value(header.second);
    }
    ctx.headers = std::move(headers);
}

std::string response_from_context(ResponseContext& ctx, const std::string& body) {
    if (!ctx.has_content_type()) {
        ctx.add_header("Content-Type", "text/html; charset=utf-8");
    }
    std::vector<std::pair<std::string, std::string>> headers;
    headers.reserve(ctx.headers.size());
    for (const auto& header : ctx.headers) {
        if (iequals(header.first, "Content-Length")) {
            continue;
        }
        headers.push_back(header);
    }
    return build_http_response(ctx.status_code, headers, body);
}

std::string template_response(const HttpRequest& request,
                              const RequestInfo& info,
                              const ResolvedResource& resource,
                              const sockaddr_in& client,
                              const ServerState& state,
                              std::optional<int> forced_status = std::nullopt) {
    try {
        Source source = Source::from_file(resource.path.string());
        Interpreter interpreter(std::make_shared<Env>(), resource.path.string());
        ResponseContext response;
        if (forced_status) {
            response.set_status(*forced_status);
        }
        interpreter.set_response_context(&response);
        CGIContext ctx;
        ctx.script_filename = resource.path.string();
        ctx.request_method = request.method;
        ctx.content_type = request.header("content-type");
        ctx.content_length = request.header("content-length");
        ctx.body = request.body;
        ctx.get = http::parse_query_string(info.query);
        ctx.post = Value::Object();
        ctx.files = Value::Object();
        ctx.cookie = http::parse_cookie_header(request.header("cookie"));
        populate_header_object(ctx, request);
        populate_server_superglobals(ctx, request, info, client, state);

        interpreter.set_cgi_context(&ctx);
        SessionContext session;
        session.is_cgi = true;
        const char* secret_env = std::getenv("POLONIO_SESSION_SECRET");
        if (secret_env) {
            session.secret = secret_env;
        }
        session.secret_missing = session.secret.empty();
        if (!session.secret_missing) {
            auto it = ctx.cookie.find("polonio_session");
            if (it != ctx.cookie.end() && std::holds_alternative<std::string>(it->second.storage())) {
                std::string cookie_value = std::get<std::string>(it->second.storage());
                Value::Object loaded;
                if (decode_session_cookie(cookie_value, session.secret, loaded)) {
                    session.data = std::move(loaded);
                }
            }
        }
        interpreter.set_session_context(&session);
        process_request_body(ctx, interpreter);
        auto env = interpreter.env();
        env->set_local("_GET", Value(ctx.get));
        env->set_local("_POST", Value(ctx.post));
        env->set_local("_FILES", Value(ctx.files));
        env->set_local("_COOKIE", Value(ctx.cookie));
        env->set_local("_SERVER", Value(ctx.server));
        std::string rendered = render_template_with_interpreter(source, interpreter);
        if (session.is_cgi && session.dirty && !session.secret_missing) {
            try {
                std::string cookie_value =
                    encode_session_cookie(session.data, session.secret, [](const std::string&) {});
                response.add_header("Set-Cookie", "polonio_session=" + cookie_value + "; Path=/; HttpOnly");
            } catch (...) {
            }
        }
        if (forced_status) {
            response.set_status(*forced_status);
        }
        std::string body = interpreter.response_finalized() ? interpreter.finalized_body() : rendered;
        return response_from_context(response, body);
    } catch (const PolonioError& err) {
        return plain_response(500, err.format());
    } catch (const std::exception& ex) {
        return plain_response(500, ex.what());
    }
}

std::string static_response(const ResolvedResource& resource) {
    try {
        std::string body = read_file_contents(resource.path);
        std::vector<std::pair<std::string, std::string>> headers = {
            {"Content-Type", infer_static_mime_type(resource.path)},
        };
        return build_http_response(200, headers, body);
    } catch (const std::exception& ex) {
        return plain_response(500, ex.what());
    }
}

std::string not_found_response(const HttpRequest& request,
                               const RequestInfo& info,
                               const sockaddr_in& client,
                               const ServerState& state) {
    auto custom = find_regular_file(state, "404.pol");
    if (custom) {
        ResolvedResource resource;
        resource.kind = ResolvedResource::Kind::Template;
        resource.path = *custom;
        return template_response(request, info, resource, client, state, 404);
    }
    return plain_response(404, "Not Found");
}

std::string dispatch_request(const ServerState& state, const HttpRequest& request, const sockaddr_in& client) {
    struct ScopedStorageRoot {
        bool applied = false;
        explicit ScopedStorageRoot(const std::filesystem::path& root) {
            const char* existing = std::getenv("POLONIO_STORAGE_PATH");
            if (existing && existing[0] != '\0') {
                return;
            }
            std::string value = root.generic_string();
            ::setenv("POLONIO_STORAGE_PATH", value.c_str(), 1);
            applied = true;
        }
        ~ScopedStorageRoot() {
            if (applied) {
                ::unsetenv("POLONIO_STORAGE_PATH");
            }
        }
    } storage_guard(state.root);

    if (request.version != "HTTP/1.1") {
        return plain_response(400, "Bad Request");
    }
    RequestInfo info;
    if (!parse_target(request.target, info)) {
        return plain_response(400, "Bad Request");
    }
    std::string method_lower = to_lower_ascii(request.method);
    if (method_lower != "get" && method_lower != "post") {
        std::vector<std::pair<std::string, std::string>> headers = {
            {"Allow", "GET, POST"},
            {"Content-Type", "text/plain; charset=utf-8"},
        };
        return build_http_response(405, headers, "Method Not Allowed");
    }
    auto resource = resolve_resource(state, info.path);
    if (!resource) {
        return not_found_response(request, info, client, state);
    }
    if (resource->kind == ResolvedResource::Kind::Template) {
        return template_response(request, info, *resource, client, state);
    }
    return static_response(*resource);
}

void handle_client(int client_fd, const sockaddr_in& client, const ServerState& state) {
    try {
        HttpRequest request;
        if (!read_http_request(client_fd, request)) {
            return;
        }
        std::string response = dispatch_request(state, request, client);
        send_all(client_fd, response);
    } catch (const std::exception& ex) {
        std::string response = plain_response(500, ex.what());
        send_all(client_fd, response);
    }
}

} // namespace

void run_http_server(const ServerConfig& config) {
    ServerState state = build_server_state(config);
    std::cout << "Serving " << state.root_string << " at http://127.0.0.1:" << state.port << std::endl;

    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        throw_system_error("socket");
    }

    int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(state.port));
    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        int err = errno;
        ::close(server_fd);
        errno = err;
        throw_system_error("bind");
    }

    if (::listen(server_fd, 16) < 0) {
        int err = errno;
        ::close(server_fd);
        errno = err;
        throw_system_error("listen");
    }

    while (true) {
        sockaddr_in client{};
        socklen_t client_len = sizeof(client);
        int client_fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            int err = errno;
            ::close(server_fd);
            errno = err;
            throw_system_error("accept");
        }
        handle_client(client_fd, client, state);
        ::close(client_fd);
    }
}

std::string simulate_http_request(const ServerConfig& config,
                                  const std::string& raw_request,
                                  const std::string& client_address) {
    ServerState state = build_server_state(config);
    HttpRequest request = parse_http_request_string(raw_request);
    sockaddr_in client{};
    client.sin_family = AF_INET;
    if (::inet_pton(AF_INET, client_address.c_str(), &client.sin_addr) != 1) {
        client.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    client.sin_port = 0;
    return dispatch_request(state, request, client);
}

} // namespace polonio
