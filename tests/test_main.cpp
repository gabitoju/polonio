#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_INCLUDE_CISO646
#define DOCTEST_CONFIG_USE_STD_HEADERS
#include "third_party/doctest/doctest.h"

#include "polonio/common/source.h"
#include "polonio/common/location.h"
#include "polonio/common/error.h"
#include "polonio/lexer/lexer.h"
#include "polonio/parser/parser.h"
#include "polonio/runtime/value.h"
#include "polonio/runtime/env.h"
#include "polonio/runtime/interpreter.h"
#include "polonio/runtime/template_scanner.h"

#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

struct CommandResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

std::string shell_quote(const std::string& input) {
    std::string quoted = "'";
    for (char ch : input) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

std::string create_temp_file(const std::string& prefix) {
    std::string pattern = "/tmp/" + prefix + "XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    int fd = mkstemp(buffer.data());
    REQUIRE(fd != -1);
    close(fd);
    return std::string(buffer.data());
}

std::string read_file(const std::string& path) {
    std::ifstream stream(path);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

std::string create_temp_file_with_content(const std::string& prefix, const std::string& content) {
    std::string path = create_temp_file(prefix);
    std::ofstream stream(path, std::ios::binary);
    stream << content;
    stream.close();
    return path;
}

std::string create_temp_directory(const std::string& prefix) {
    std::string pattern = "/tmp/" + prefix + "XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    char* result = mkdtemp(buffer.data());
    REQUIRE(result != nullptr);
    return std::string(result);
}

CommandResult run_polonio_with_env(const std::vector<std::string>& args,
                                   const std::vector<std::pair<std::string, std::string>>& env,
                                   const std::string& stdin_content);

CommandResult run_polonio(const std::vector<std::string>& args,
                          const std::vector<std::pair<std::string, std::string>>& env = {},
                          const std::string& stdin_content = "") {
    return run_polonio_with_env(args, env, stdin_content);
}

CommandResult run_polonio_with_env(const std::vector<std::string>& args,
                                   const std::vector<std::pair<std::string, std::string>>& env,
                                   const std::string& stdin_content) {
    auto binary = (std::filesystem::current_path() / "build/polonio").string();
    REQUIRE(std::filesystem::exists(binary));

    const std::string stdout_path = create_temp_file("polonio_stdout");
    const std::string stderr_path = create_temp_file("polonio_stderr");
    std::string stdin_path;
    if (!stdin_content.empty()) {
        stdin_path = create_temp_file("polonio_stdin");
        std::ofstream stdin_file(stdin_path, std::ios::binary);
        stdin_file << stdin_content;
    }

    std::ostringstream cmd;
    if (!env.empty()) {
        cmd << "env";
        for (const auto& kv : env) {
            cmd << " " << shell_quote(kv.first + "=" + kv.second);
        }
        cmd << " ";
    }
    cmd << shell_quote(binary);
    for (const auto& arg : args) {
        cmd << " " << shell_quote(arg);
    }
    cmd << " > " << shell_quote(stdout_path) << " 2> " << shell_quote(stderr_path);
    if (!stdin_content.empty()) {
        cmd << " < " << shell_quote(stdin_path);
    } else {
        cmd << " < /dev/null";
    }

    int status = std::system(cmd.str().c_str());
    CommandResult result{};
    if (status == -1) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = status;
    }

    result.stdout_output = read_file(stdout_path);
    result.stderr_output = read_file(stderr_path);

    std::remove(stdout_path.c_str());
    std::remove(stderr_path.c_str());
    if (!stdin_path.empty()) {
        std::remove(stdin_path.c_str());
    }
    return result;
}

int reserve_tcp_port() {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(sock >= 0);
    int opt = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    int rc = ::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0) {
        INFO("reserve_tcp_port bind failed: " << std::strerror(errno));
    }
    REQUIRE(rc == 0);
    socklen_t len = sizeof(addr);
    rc = ::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len);
    REQUIRE(rc == 0);
    int port = ntohs(addr.sin_port);
    ::close(sock);
    return port;
}

int connect_with_retry(int port) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(sock >= 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            return sock;
        }
        ::close(sock);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    FAIL("unable to connect to test server");
    return -1;
}

std::string perform_http_get(int port) {
    int sock = connect_with_retry(port);
    const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    ssize_t sent = ::send(sock, request.data(), request.size(), 0);
    REQUIRE(sent == static_cast<ssize_t>(request.size()));
    std::string response;
    char buffer[1024];
    while (true) {
        ssize_t n = ::recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        response.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(sock);
    return response;
}

struct ChildProcessGuard {
    pid_t pid = -1;
    explicit ChildProcessGuard(pid_t p) : pid(p) {}
    ~ChildProcessGuard() {
        if (pid > 0) {
            ::kill(pid, SIGTERM);
            ::waitpid(pid, nullptr, 0);
        }
    }
    void release() { pid = -1; }
};

std::string run_serve_request(const std::string& root, int port) {
    auto binary = (std::filesystem::current_path() / "build/polonio").string();
    REQUIRE(std::filesystem::exists(binary));
    pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        std::string port_str = std::to_string(port);
        execl(binary.c_str(), binary.c_str(), "serve", "--port", port_str.c_str(), "--root", root.c_str(), (char*)nullptr);
        _exit(127);
    }
    ChildProcessGuard guard(pid);
    std::string response = perform_http_get(port);
    int status = 0;
    ::waitpid(pid, &status, 0);
    guard.release();
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    return response;
}

} // namespace

TEST_CASE("CLI: run executes interpreter output") {
    auto path = create_temp_file_with_content("polonio_cli_run", "var x = 1\necho x");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "1");
    CHECK(result.stderr_output.empty());
    std::filesystem::remove(path);
}

TEST_CASE("CLI: run reports runtime errors") {
    auto path = create_temp_file_with_content("polonio_cli_rt", "echo y");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("undefined variable") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CLI: run reports parse errors") {
    auto path = create_temp_file_with_content("polonio_cli_parse", "var");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find(path) != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CLI: --dump-ast prints expression tree") {
    auto result = run_polonio({"--dump-ast", "1 + 2 * 3"});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "(+ num(1) (* num(2) num(3)))\n");
    CHECK(result.stderr_output.empty());
}

TEST_CASE("CLI: --dump-ast validates arguments") {
    auto result = run_polonio({"--dump-ast"});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("requires an expression") != std::string::npos);
}

TEST_CASE("CLI: shorthand file invocation executes program") {
    auto path = create_temp_file_with_content("polonio_cli_sh", "echo 42");
    auto result = run_polonio({path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "42");
    std::filesystem::remove(path);
}

TEST_CASE("CLI: shorthand on missing file reports IO error") {
    auto path = (std::filesystem::temp_directory_path() / "polonio_missing_cli_file").string();
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
    auto result = run_polonio({path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("failed to open source file") != std::string::npos);
}

TEST_CASE("CLI: template rendering mixes text and code") {
    const char* tpl = "<h1>Hello</h1>\n<% echo 1 + 2 %>";
    auto path = create_temp_file_with_content("polonio_template_mix", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("<h1>Hello</h1>") != std::string::npos);
    CHECK(result.stdout_output.find("3") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CLI: inline echo renders expressions") {
    const char* tpl = "<p>2 + 3 = <% echo 2 + 3 %></p>";
    auto path = create_temp_file_with_content("polonio_inline_basic", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("2 + 3 = 5") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("Template scanner splits text and code segments") {
    polonio::Source src("inline.pol", "Hello <% echo 1 %> world!");
    auto segments = polonio::scan_template(src);
    REQUIRE(segments.size() == 3);
    CHECK(segments[0].kind == polonio::TemplateSegment::Kind::Text);
    CHECK(segments[0].content == "Hello ");
    CHECK(segments[1].kind == polonio::TemplateSegment::Kind::Code);
    CHECK(segments[1].content == " echo 1 ");
    CHECK(segments[2].kind == polonio::TemplateSegment::Kind::Text);
    CHECK(segments[2].content == " world!");
}

TEST_CASE("Template scanner errors on unterminated code blocks") {
    polonio::Source src("broken.pol", "<% echo 1");
    CHECK_THROWS_AS(polonio::scan_template(src), polonio::PolonioError);
}

TEST_CASE("Template renderer strips HTML comments in text segments") {
    const char* tpl = R"(
<% var name = "Ada" %>
<div>Hello /* remove me */$name</div>
)";
    auto path = create_temp_file_with_content("polonio_html_comments", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("<div>Hello Ada</div>") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("Template renderer errors on unterminated HTML comment") {
    const char* tpl = "<% echo 1 %> Hello /* oops";
    auto path = create_temp_file_with_content("polonio_html_comment_err", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("unterminated HTML comment") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("Template if blocks span HTML segments") {
    const char* tpl = R"(
<ul>
<% if true %>
<li>Visible</li>
<% else %>
<li>Hidden</li>
<% end %>
</ul>
)";
    auto path = create_temp_file_with_content("polonio_if_span", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("<li>Visible</li>") != std::string::npos);
    CHECK(result.stdout_output.find("Hidden") == std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("Template for loops span HTML segments") {
    const char* tpl = R"(
<ul>
<% for item in ["one", "two"] %>
<li>$item</li>
<% end %>
</ul>
)";
    auto path = create_temp_file_with_content("polonio_for_span", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("<li>one</li>") != std::string::npos);
    CHECK(result.stdout_output.find("<li>two</li>") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CLI: inline echo uses shared variables") {
    const char* tpl = R"(
<% var name = "Juan" %>
Hello <% echo name %>!
)";
    auto path = create_temp_file_with_content("polonio_inline_vars", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("Hello Juan!") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CLI: multiple inline echo blocks") {
    const char* tpl = R"(
<% var x = 2 %>
<% echo x %>-<% echo x * 2 %>
)";
    auto path = create_temp_file_with_content("polonio_inline_multi", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("2-4") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CLI: non-echo code blocks still execute") {
    const char* tpl = R"(
<% var x = 3 %>
<% x = x + 1 %>
<% echo x %>
)";
    auto path = create_temp_file_with_content("polonio_inline_non", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("4") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CLI: inline echo parse errors report location") {
    const char* tpl = "<% echo 1 + %>";
    auto path = create_temp_file_with_content("polonio_inline_error", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find(path) != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("Template include inserts partial output") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_include";
    std::filesystem::create_directories(dir);
    auto main_path = (dir / "main.pol").string();
    auto partial_path = (dir / "partial.pol").string();
    {
        std::ofstream f(main_path);
        f << "<h1>Main</h1>\n<% include \"partial.pol\" %>";
    }
    {
        std::ofstream f(partial_path);
        f << "<p>Partial</p>";
    }
    auto result = run_polonio({"run", main_path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("<p>Partial</p>") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Includes share interpreter state") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_include_state";
    std::filesystem::create_directories(dir / "sub");
    auto main_path = (dir / "main.pol").string();
    auto child_path = (dir / "hello.pol").string();
    {
        std::ofstream f(main_path);
        f << "<% var name = \"World\" %>\n<% include \"hello.pol\" %>";
    }
    {
        std::ofstream f(child_path);
        f << "Hello $name";
    }
    auto result = run_polonio({"run", main_path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("Hello World") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Nested includes resolve relative paths") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_include_nested";
    std::filesystem::create_directories(dir / "a");
    std::filesystem::create_directories(dir / "b");
    auto main_path = (dir / "main.pol").string();
    auto one_path = (dir / "a/one.pol").string();
    auto two_path = (dir / "b/two.pol").string();
    {
        std::ofstream f(main_path);
        f << "<% include \"a/one.pol\" %>";
    }
    {
        std::ofstream f(one_path);
        f << "<% include \"../b/two.pol\" %>";
    }
    {
        std::ofstream f(two_path);
        f << "OK";
    }
    auto result = run_polonio({"run", main_path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("OK") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Include cycles detected") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_include_cycle";
    std::filesystem::create_directories(dir);
    auto a_path = (dir / "a.pol").string();
    auto b_path = (dir / "b.pol").string();
    {
        std::ofstream f(a_path);
        f << "<% include \"b.pol\" %>";
    }
    {
        std::ofstream f(b_path);
        f << "<% include \"a.pol\" %>";
    }
    auto result = run_polonio({"run", a_path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("include") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Missing include file reports error") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_include_missing";
    std::filesystem::create_directories(dir);
    auto main_path = (dir / "main.pol").string();
    {
        std::ofstream f(main_path);
        f << "<% include \"nope.pol\" %>";
    }
    auto result = run_polonio({"run", main_path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("nope.pol") != std::string::npos);
    std::filesystem::remove_all(dir);
}

namespace {

CommandResult run_polonio_cgi(const std::vector<std::pair<std::string, std::string>>& env,
                              const std::string& stdin_body = std::string()) {
    return run_polonio_with_env({}, env, stdin_body);
}

struct CGIOutput {
    std::string headers;
    std::string body;
};

CGIOutput parse_cgi_output(const std::string& output) {
    std::string delimiter = "\r\n\r\n";
    auto pos = output.find(delimiter);
    if (pos == std::string::npos) {
        return {output, std::string()};
    }
    return {output.substr(0, pos), output.substr(pos + delimiter.size())};
}

std::string extract_cgi_body(const std::string& output) {
    const std::string header = "Content-Type: text/html\r\n\r\n";
    if (output.rfind(header, 0) == 0) {
        return output.substr(header.size());
    }
    return output;
}

std::string find_session_cookie_line(const std::string& headers) {
    const std::string prefix = "Set-Cookie: polonio_session=";
    auto pos = headers.find(prefix);
    if (pos == std::string::npos) {
        return std::string();
    }
    auto end = headers.find("\r\n", pos);
    std::string line = end == std::string::npos ? headers.substr(pos) : headers.substr(pos, end - pos);
    return line;
}

std::string extract_session_cookie_value(const std::string& headers) {
    std::string line = find_session_cookie_line(headers);
    if (line.empty()) {
        return std::string();
    }
    auto start = line.find('=');
    if (start == std::string::npos) return std::string();
    std::string value = line.substr(start + 1);
    auto semi = value.find(';');
    if (semi != std::string::npos) {
        value = value.substr(0, semi);
    }
    return value;
}

bool is_url_safe(const std::string& token) {
    for (char ch : token) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_') {
            continue;
        }
        return false;
    }
    return true;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::filesystem::path> list_files(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> paths;
    if (std::filesystem::exists(dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                paths.push_back(entry.path());
            }
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("CLI: template blocks share interpreter state") {
    const char* tpl = "<% var x = 1 %>\n<p>\n<% echo x %>";
    auto path = create_temp_file_with_content("polonio_template_state", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("<p>") != std::string::npos);
    CHECK(result.stdout_output.find("1") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CLI: unterminated template block reports error") {
    const char* tpl = "Hello <% echo 1";
    auto path = create_temp_file_with_content("polonio_template_error", tpl);
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("unterminated template block") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CGI mode populates _GET") {
    auto path = create_temp_file_with_content("polonio_cgi_get",
                                              "<% echo _GET[\"a\"] %><% echo _GET[\"b\"][0] %>"
                                              "<% echo _GET[\"b\"][1] %><% echo _GET[\"flag\"] %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"QUERY_STRING", "a=1&b=two&b=three&flag"},
    };
    auto result = run_polonio_cgi(env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.rfind("Content-Type: text/html\r\n\r\n", 0) == 0);
    CHECK(extract_cgi_body(result.stdout_output) == "1twothree");
    std::filesystem::remove(path);
}

TEST_CASE("CGI mode parses cookies") {
    auto path = create_temp_file_with_content("polonio_cgi_cookie",
                                              "<% echo _COOKIE[\"y\"] %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"HTTP_COOKIE", "x=1; y=two"},
    };
    auto result = run_polonio_cgi(env);
    CHECK(result.exit_code == 0);
    CHECK(extract_cgi_body(result.stdout_output) == "two");
    std::filesystem::remove(path);
}

TEST_CASE("CGI mode parses POST form data") {
    auto path = create_temp_file_with_content("polonio_cgi_post",
                                              "<% echo _POST[\"m\"][0] %><% echo _POST[\"m\"][1] %>");
    std::string body = "m=hi&m=there";
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"REQUEST_METHOD", "POST"},
        {"CONTENT_TYPE", "application/x-www-form-urlencoded"},
        {"CONTENT_LENGTH", std::to_string(body.size())},
    };
    auto result = run_polonio_cgi(env, body);
    CHECK(result.exit_code == 0);
    CHECK(extract_cgi_body(result.stdout_output) == "hithere");
    std::filesystem::remove(path);
}

TEST_CASE("CGI mode populates _SERVER") {
    auto path = create_temp_file_with_content("polonio_cgi_server",
                                              "<% echo _SERVER[\"REQUEST_METHOD\"] %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"REQUEST_METHOD", "POST"},
    };
    auto result = run_polonio_cgi(env);
    CHECK(result.exit_code == 0);
    CHECK(extract_cgi_body(result.stdout_output) == "POST");
    std::filesystem::remove(path);
}

TEST_CASE("CGI mode supports status() and header() builtins") {
    auto path = create_temp_file_with_content(
        "polonio_cgi_headers",
        "<% status(404) %><% header(\"X-Test\", \"yes\") %><h1>Missing</h1>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    auto result = run_polonio_cgi(env);
    CHECK(result.exit_code == 0);
    auto parsed = parse_cgi_output(result.stdout_output);
    CHECK(parsed.headers.find("Status: 404") != std::string::npos);
    CHECK(parsed.headers.find("X-Test: yes") != std::string::npos);
    CHECK(parsed.headers.find("Content-Type: text/html") != std::string::npos);
    CHECK(parsed.body.find("<h1>Missing</h1>") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CGI mode allows raw header lines") {
    auto path = create_temp_file_with_content("polonio_cgi_header_raw",
                                              "<% header(\"X-A: 1\") %>ok");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    auto result = run_polonio_cgi(env);
    auto parsed = parse_cgi_output(result.stdout_output);
    CHECK(parsed.headers.find("X-A: 1") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CGI mode allows Content-Type override") {
    auto path = create_temp_file_with_content("polonio_cgi_ct",
                                              "<% header(\"Content-Type\", \"text/plain\") %>ok");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    auto result = run_polonio_cgi(env);
    auto parsed = parse_cgi_output(result.stdout_output);
    CHECK(parsed.headers.find("Content-Type: text/plain") != std::string::npos);
    CHECK(parsed.headers.find("text/html") == std::string::npos);
    CHECK(parsed.body == "ok");
    std::filesystem::remove(path);
}

TEST_CASE("CGI http helpers control status and headers") {
    auto path = create_temp_file_with_content(
        "polonio_cgi_http_helpers",
        "<% http_status(201) %><% http_header(\"X-Test\", \"ok\") %><% http_content_type(\"text/plain\") %>ready");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    auto result = run_polonio_cgi(env);
    auto parsed = parse_cgi_output(result.stdout_output);
    CHECK(parsed.headers.find("Status: 201") != std::string::npos);
    CHECK(parsed.headers.find("X-Test: ok") != std::string::npos);
    CHECK(parsed.headers.find("Content-Type: text/plain") != std::string::npos);
    CHECK(parsed.body == "ready");
    std::filesystem::remove(path);
}

TEST_CASE("serve stub handles single request") {
    auto root = create_temp_directory("polonio_serve_root");
    int port = reserve_tcp_port();
    auto response = run_serve_request(root, port);
    CHECK(response.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(response.find("\r\n\r\nOK") != std::string::npos);
    std::filesystem::remove_all(root);
}

TEST_CASE("CGI redirect builtin sets Location header") {
    auto path = create_temp_file_with_content("polonio_cgi_redirect", "<% redirect(\"/login\") %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    auto result = run_polonio_cgi(env);
    auto parsed = parse_cgi_output(result.stdout_output);
    CHECK(parsed.headers.find("Status: 302") != std::string::npos);
    CHECK(parsed.headers.find("Location: /login") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("redirect builtin allows custom status") {
    auto path = create_temp_file_with_content("polonio_cgi_redirect_custom", "<% redirect(\"/home\", 301) %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    auto result = run_polonio_cgi(env);
    auto parsed = parse_cgi_output(result.stdout_output);
    CHECK(parsed.headers.find("Status: 301") != std::string::npos);
    CHECK(parsed.headers.find("Location: /home") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("request_body returns raw CGI body") {
    auto path = create_temp_file_with_content("polonio_request_body", "<% echo request_body() %>");
    std::string body = "a=1&b=2";
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"REQUEST_METHOD", "POST"},
        {"CONTENT_LENGTH", std::to_string(body.size())},
    };
    auto result = run_polonio_cgi(env, body);
    CHECK(result.exit_code == 0);
    CHECK(extract_cgi_body(result.stdout_output) == body);
    std::filesystem::remove(path);
}

TEST_CASE("request_header is case-insensitive and null on miss") {
    auto path = create_temp_file_with_content("polonio_request_header",
                                              "<% echo request_header(\"X-Test\") %>|"
                                              "<% echo request_header(\"content-type\") %>|"
                                              "<% if request_header(\"missing\") == null %>none<% end %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"HTTP_X_TEST", "abc"},
        {"CONTENT_TYPE", "text/plain"},
    };
    auto result = run_polonio_cgi(env);
    CHECK(result.exit_code == 0);
    CHECK(extract_cgi_body(result.stdout_output) == "abc|text/plain|none");
    std::filesystem::remove(path);
}

TEST_CASE("request_headers returns normalized map of headers") {
    auto path = create_temp_file_with_content("polonio_request_headers",
                                              "<% var h = request_headers(); %>"
                                              "<% echo h[\"x-test\"] %>|"
                                              "<% echo h[\"content-type\"] %>|"
                                              "<% echo h[\"x-custom\"] %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"CONTENT_TYPE", "text/plain"},
        {"CONTENT_LENGTH", "5"},
        {"HTTP_X_TEST", "abc"},
        {"HTTP_X_CUSTOM", "42"},
    };
    auto result = run_polonio_cgi(env, "hello");
    CHECK(result.exit_code == 0);
    CHECK(extract_cgi_body(result.stdout_output) == "abc|text/plain|42");
    std::filesystem::remove(path);
}

TEST_CASE("cookies builtin exposes parsed cookie jar") {
    auto path = create_temp_file_with_content("polonio_cookies_builtin",
                                              "<% var c = cookies(); %>"
                                              "<% echo c[\"a\"] %>|<% echo c[\"b\"] %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"HTTP_COOKIE", "a=1; b=two"},
    };
    auto result = run_polonio_cgi(env);
    CHECK(result.exit_code == 0);
    CHECK(extract_cgi_body(result.stdout_output) == "1|two");
    std::filesystem::remove(path);
}

TEST_CASE("request_json parses JSON bodies") {
    auto path = create_temp_file_with_content(
        "polonio_request_json",
        "<% var data = request_json(); %>"
        "<% echo data[\"a\"] %>|"
        "<% echo data[\"b\"][0] %>|"
        "<% if data[\"b\"][1] == null %>null<% end %>|"
        "<% echo data[\"b\"][2] %>");
    std::string body = "{\"a\":1,\"b\":[true,null,\"x\"]}";
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"REQUEST_METHOD", "POST"},
        {"CONTENT_TYPE", "application/json"},
        {"CONTENT_LENGTH", std::to_string(body.size())},
    };
    auto result = run_polonio_cgi(env, body);
    CHECK(result.exit_code == 0);
    CHECK(extract_cgi_body(result.stdout_output) == "1|true|null|x");
    std::filesystem::remove(path);
}

TEST_CASE("request_json returns null on empty body") {
    auto path = create_temp_file_with_content("polonio_request_json_empty",
                                              "<% var data = request_json(); %>"
                                              "<% if data == null %>empty<% end %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    auto result = run_polonio_cgi(env);
    CHECK(result.exit_code == 0);
    CHECK(extract_cgi_body(result.stdout_output) == "empty");
    std::filesystem::remove(path);
}

TEST_CASE("request_json reports invalid payloads") {
    auto path = create_temp_file_with_content("polonio_request_json_invalid",
                                              "<% request_json(); %>");
    std::string body = "{\"a\":";
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"REQUEST_METHOD", "POST"},
        {"CONTENT_TYPE", "application/json"},
        {"CONTENT_LENGTH", std::to_string(body.size())},
    };
    auto result = run_polonio_cgi(env, body);
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("invalid json") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("sessions work in non-CGI mode") {
    auto path = create_temp_file_with_content("polonio_session_inmemory",
                                              "<% session_set(\"a\", 1) %>"
                                              "<% echo session_get(\"a\") %>"
                                              "<% session_unset(\"a\") %>"
                                              "<% if session_get(\"a\") == null %>null<% end %>"
                                              "<% session_set(\"b\", \"x\") %>"
                                              "<% session_clear() %>"
                                              "<% if session_get(\"b\") == null %>clear<% end %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "1nullclear");
    std::filesystem::remove(path);
}

TEST_CASE("CGI sessions emit cookie when dirty") {
    auto path = create_temp_file_with_content("polonio_session_header",
                                              "<% session_set(\"user\", \"juan\") %>ok");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"POLONIO_SESSION_SECRET", "secret-key"},
    };
    auto result = run_polonio_cgi(env);
    auto parsed = parse_cgi_output(result.stdout_output);
    CHECK(parsed.body == "ok");
    CHECK(find_session_cookie_line(parsed.headers).find("Set-Cookie: polonio_session=") != std::string::npos);
    auto second = parsed.headers.find("Set-Cookie: polonio_session=",
                                      parsed.headers.find("Set-Cookie: polonio_session=") + 1);
    CHECK(second == std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CGI sessions reuse cookie data") {
    auto path = create_temp_file_with_content("polonio_session_reuse_set",
                                              "<% session_set(\"user\", \"ana\") %>ready");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"POLONIO_SESSION_SECRET", "secret-key"},
    };
    auto first = run_polonio_cgi(env);
    auto first_headers = parse_cgi_output(first.stdout_output).headers;
    std::string cookie_value = extract_session_cookie_value(first_headers);
    REQUIRE(!cookie_value.empty());
    auto path_get = create_temp_file_with_content("polonio_session_reuse_get",
                                                  "<% echo session_get(\"user\") %>");
    std::vector<std::pair<std::string, std::string>> env2 = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path_get},
        {"POLONIO_SESSION_SECRET", "secret-key"},
        {"HTTP_COOKIE", "polonio_session=" + cookie_value},
    };
    auto result = run_polonio_cgi(env2);
    CHECK(extract_cgi_body(result.stdout_output) == "ana");
    std::filesystem::remove(path);
    std::filesystem::remove(path_get);
}

TEST_CASE("invalid session cookie is ignored") {
    auto path = create_temp_file_with_content("polonio_session_invalid_cookie",
                                              "<% var val = session_get(\"user\"); %>"
                                              "<% if val == null %>none<% else %>bad<% end %>");
    std::string bad_cookie = "abc.def";
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"POLONIO_SESSION_SECRET", "secret-key"},
        {"HTTP_COOKIE", "polonio_session=" + bad_cookie},
    };
    auto result = run_polonio_cgi(env);
    CHECK(extract_cgi_body(result.stdout_output) == "none");
    std::filesystem::remove(path);
}

TEST_CASE("missing session secret errors when session used") {
    auto path = create_temp_file_with_content("polonio_session_missing_secret",
                                              "<% session_get(\"user\") %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    auto result = run_polonio_cgi(env);
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("missing session secret") != std::string::npos);
    std::filesystem::remove(path);

    auto ok_path = create_temp_file_with_content("polonio_session_missing_secret_ok",
                                                 "<% echo \"fine\" %>");
    std::vector<std::pair<std::string, std::string>> env_ok = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", ok_path},
    };
    auto ok_result = run_polonio_cgi(env_ok);
    CHECK(extract_cgi_body(ok_result.stdout_output) == "fine");
    std::filesystem::remove(ok_path);
}

TEST_CASE("session_set rejects non-serializable values") {
    auto path = create_temp_file_with_content("polonio_session_non_serializable",
                                              "<% session_set(\"f\", type) %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("session value not serializable") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("random_token produces url-safe unique strings") {
    auto path = create_temp_file_with_content("polonio_random_token",
                                              "<% var a = random_token(16) %>"
                                              "<% var b = random_token(16) %>"
                                              "<% echo a %>\n<% echo b %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    auto pos = result.stdout_output.find('\n');
    REQUIRE(pos != std::string::npos);
    std::string first = result.stdout_output.substr(0, pos);
    std::string second = result.stdout_output.substr(pos + 1);
    CHECK_FALSE(first.empty());
    CHECK_FALSE(second.empty());
    CHECK(first != second);
    CHECK(is_url_safe(first));
    CHECK(is_url_safe(second));
    std::filesystem::remove(path);
}

TEST_CASE("random_token enforces bounds and types") {
    auto path = create_temp_file_with_content("polonio_random_token_bounds",
                                              "<% random_token(0) %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("random_token") != std::string::npos);
    std::filesystem::remove(path);

    auto path2 = create_temp_file_with_content("polonio_random_token_bounds2",
                                               "<% random_token(2000) %>");
    auto result2 = run_polonio({"run", path2});
    CHECK(result2.exit_code != 0);
    CHECK(result2.stderr_output.find("random_token") != std::string::npos);
    std::filesystem::remove(path2);

    auto path3 = create_temp_file_with_content("polonio_random_token_bounds3",
                                               "<% random_token(\"oops\") %>");
    auto result3 = run_polonio({"run", path3});
    CHECK(result3.exit_code != 0);
    CHECK(result3.stderr_output.find("random_token") != std::string::npos);
    std::filesystem::remove(path3);
}

TEST_CASE("csrf_token returns stable value within session") {
    auto path = create_temp_file_with_content("polonio_csrf_token",
                                              "<% var a = csrf_token() %>"
                                              "<% var b = csrf_token() %>"
                                              "<% if a == b %>true<% else %>false<% end %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "true");
    std::filesystem::remove(path);
}

TEST_CASE("csrf_verify validates tokens") {
    auto path = create_temp_file_with_content("polonio_csrf_verify",
                                              "<% var t = csrf_token() %>"
                                              "<% if csrf_verify(t) %>T<% else %>F<% end %>"
                                              "<% if csrf_verify(\"wrong\") %>T<% else %>F<% end %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "TF");
    std::filesystem::remove(path);
}

TEST_CASE("csrf_token persists across CGI requests via session cookie") {
    auto path = create_temp_file_with_content("polonio_csrf_cgi",
                                              "<% echo csrf_token() %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"POLONIO_SESSION_SECRET", "csrf-secret"},
    };
    auto first = run_polonio_cgi(env);
    auto parsed = parse_cgi_output(first.stdout_output);
    std::string token = parsed.body;
    std::string cookie_value = extract_session_cookie_value(parsed.headers);
    REQUIRE(!cookie_value.empty());

    std::vector<std::pair<std::string, std::string>> env2 = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
        {"POLONIO_SESSION_SECRET", "csrf-secret"},
        {"HTTP_COOKIE", "polonio_session=" + cookie_value},
    };
    auto second = run_polonio_cgi(env2);
    CHECK(extract_cgi_body(second.stdout_output) == token);
    std::filesystem::remove(path);
}

TEST_CASE("csrf builtins handle missing secret in CGI mode") {
    auto path = create_temp_file_with_content("polonio_csrf_missing_secret",
                                              "<% csrf_token() %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    auto result = run_polonio_cgi(env);
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("missing session secret") != std::string::npos);
    std::filesystem::remove(path);

    auto path_verify = create_temp_file_with_content("polonio_csrf_missing_secret_verify",
                                                     "<% if csrf_verify(\"x\") %>yes<% else %>no<% end %>");
    std::vector<std::pair<std::string, std::string>> env_verify = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path_verify},
    };
    auto verify_result = run_polonio_cgi(env_verify);
    CHECK(extract_cgi_body(verify_result.stdout_output) == "no");
    std::filesystem::remove(path_verify);
}

TEST_CASE("hash_password + verify_password roundtrip") {
    auto path = create_temp_file_with_content("polonio_hash_password",
                                              "<% var h = hash_password(\"secret\") %>"
                                              "<% echo h %>\n"
                                              "<% if verify_password(\"secret\", h) %>true<% else %>false<% end %>"
                                              "<% if verify_password(\"wrong\", h) %>true<% else %>false<% end %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    auto lines = result.stdout_output;
    auto newline = lines.find('\n');
    REQUIRE(newline != std::string::npos);
    std::string hash = lines.substr(0, newline);
    std::string rest = lines.substr(newline + 1);
    CHECK(hash.find("pbkdf2_sha256$") != std::string::npos);
    CHECK(rest == "truefalse");
    std::filesystem::remove(path);
}

TEST_CASE("hash_password generates different hashes for same input") {
    auto path = create_temp_file_with_content("polonio_hash_password_uniqueness",
                                              "<% var a = hash_password(\"same\") %>"
                                              "<% var b = hash_password(\"same\") %>"
                                              "<% if a == b %>same<% else %>diff<% end %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "diff");
    std::filesystem::remove(path);
}

TEST_CASE("password hashing type checks and malformed hashes") {
    auto path = create_temp_file_with_content("polonio_hash_password_type", "<% hash_password(123) %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("hash_password") != std::string::npos);
    std::filesystem::remove(path);

    auto path2 = create_temp_file_with_content("polonio_verify_password_type1",
                                               "<% verify_password(123, \"x\") %>");
    auto result2 = run_polonio({"run", path2});
    CHECK(result2.exit_code != 0);
    CHECK(result2.stderr_output.find("verify_password") != std::string::npos);
    std::filesystem::remove(path2);

    auto path3 = create_temp_file_with_content("polonio_verify_password_type2",
                                               "<% verify_password(\"x\", 123) %>");
    auto result3 = run_polonio({"run", path3});
    CHECK(result3.exit_code != 0);
    CHECK(result3.stderr_output.find("verify_password") != std::string::npos);
    std::filesystem::remove(path3);

    auto path4 = create_temp_file_with_content("polonio_verify_password_malformed",
                                               "<% if verify_password(\"x\", \"nope\") %>t<% else %>f<% end %>");
    auto result4 = run_polonio({"run", path4});
    CHECK(result4.exit_code == 0);
    CHECK(result4.stdout_output == "f");
    std::filesystem::remove(path4);

    auto path5 = create_temp_file_with_content("polonio_verify_password_iteration_bounds",
                                               "<% echo verify_password(\"x\", \"pbkdf2_sha256$0$a$b\") %>"
                                               "<% echo verify_password(\"x\", \"pbkdf2_sha256$1000000000$a$b\") %>");
    auto result5 = run_polonio({"run", path5});
    CHECK(result5.exit_code == 0);
    CHECK(result5.stdout_output == "falsefalse");
    std::filesystem::remove(path5);
}

std::vector<std::pair<std::string, std::string>> storage_env(const std::string& path) {
    return {{"POLONIO_STORAGE_PATH", path}};
}

CommandResult run_cgi_template(const std::string& name,
                               const std::string& contents,
                               const std::vector<std::pair<std::string, std::string>>& extra_env = {},
                               const std::string& stdin_body = std::string()) {
    auto path = create_temp_file_with_content(name, contents);
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    env.insert(env.end(), extra_env.begin(), extra_env.end());
    auto result = run_polonio_cgi(env, stdin_body);
    std::filesystem::remove(path);
    return result;
}

struct MultipartPart {
    std::string name;
    bool is_file = false;
    std::string filename;
    std::string content_type;
    std::string data;
};

std::string build_multipart_body(const std::string& boundary, const std::vector<MultipartPart>& parts) {
    std::string body;
    for (const auto& part : parts) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + part.name + "\"";
        if (part.is_file) {
            body += "; filename=\"" + part.filename + "\"";
        }
        body += "\r\n";
        if (part.is_file) {
            if (!part.content_type.empty()) {
                body += "Content-Type: " + part.content_type + "\r\n";
            } else {
                body += "Content-Type: application/octet-stream\r\n";
            }
        }
        body += "\r\n";
        body += part.data;
        body += "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    return body;
}

CommandResult run_multipart_template(const std::string& name,
                                     const std::string& contents,
                                     const std::vector<MultipartPart>& parts,
                                     const std::filesystem::path& storage_root,
                                     const std::vector<std::pair<std::string, std::string>>& extra_env = {}) {
    std::string boundary = "----PolonioBoundaryTest";
    auto body = build_multipart_body(boundary, parts);
    std::vector<std::pair<std::string, std::string>> env = {
        {"REQUEST_METHOD", "POST"},
        {"CONTENT_TYPE", std::string("multipart/form-data; boundary=") + boundary},
        {"CONTENT_LENGTH", std::to_string(body.size())},
        {"POLONIO_STORAGE_PATH", storage_root.string()},
    };
    env.insert(env.end(), extra_env.begin(), extra_env.end());
    return run_cgi_template(name, contents, env, body);
}

std::string response_body(const std::string& response) {
    auto pos = response.find("\r\n\r\n");
    if (pos == std::string::npos) {
        return {};
    }
    return response.substr(pos + 4);
}

TEST_CASE("storage file write/read and listing") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_program",
        "<% dir_create(\"notes\") %>"
        "<% file_write(\"notes/a.txt\", \"hello\") %>"
        "<% echo file_read(\"notes/a.txt\") %>"
        "<% echo file_exists(\"notes/a.txt\") %>"
        "<% echo file_exists(\"notes/missing.txt\") %>"
        "<% file_write(\"notes/b.txt\", \"B\") %>"
        "<% var entries = dir_list(\"notes\") %>"
        "<% echo join(entries, \",\") %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "hellotruefalsea.txt,b.txt");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("storage write requires parent dir") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_missing_parent";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_missing_program",
        "<% file_write(\"missing/a.txt\", \"x\") %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("missing directory") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("storage path traversal and absolute paths rejected") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_traversal";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto traversal_program = create_temp_file_with_content(
        "polonio_storage_traversal_program",
        "<% file_read(\"../secret.txt\") %>");
    auto result = run_polonio({"run", traversal_program}, env);
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("path traversal") != std::string::npos);
    std::filesystem::remove(traversal_program);

    auto absolute_program = create_temp_file_with_content(
        "polonio_storage_absolute_program",
        "<% file_read(\"/tmp/x\") %>");
    auto result_abs = run_polonio({"run", absolute_program}, env);
    CHECK(result_abs.exit_code != 0);
    CHECK(result_abs.stderr_output.find("absolute path") != std::string::npos);
    std::filesystem::remove(absolute_program);

    auto dir_create_program = create_temp_file_with_content(
        "polonio_storage_dir_traversal",
        "<% dir_create(\"../oops\") %>");
    auto dir_create_result = run_polonio({"run", dir_create_program}, env);
    CHECK(dir_create_result.exit_code != 0);
    CHECK(dir_create_result.stderr_output.find("path traversal") != std::string::npos);
    std::filesystem::remove(dir_create_program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("storage builtins require storage root") {
    auto program = create_temp_file_with_content(
        "polonio_storage_missing_root",
        "<% file_exists(\"x\") %>");
    auto result = run_polonio({"run", program});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("missing storage root") != std::string::npos);
    std::filesystem::remove(program);
}

TEST_CASE("file_exists for directory returns false") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_dir_exists";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_dir_program",
        "<% dir_create(\"notes\") %>"
        "<% if file_exists(\"notes\") %>true<% else %>false<% end %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "false");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("file_append appends to existing file") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_append_existing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_append_existing_program",
        "<% dir_create(\"logs\") %>"
        "<% file_write(\"logs/app.log\", \"hello\") %>"
        "<% file_append(\"logs/app.log\", \" world\") %>"
        "<% echo file_read(\"logs/app.log\") %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "hello world");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("file_append creates file if missing") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_append_new";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_append_new_program",
        "<% dir_create(\"logs\") %>"
        "<% file_append(\"logs/new.log\", \"first\") %>"
        "<% echo file_read(\"logs/new.log\") %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "first");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("file_delete removes existing file") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_delete_existing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_delete_existing_program",
        "<% dir_create(\"tmp\") %>"
        "<% file_write(\"tmp/a.txt\", \"x\") %>"
        "<% echo file_delete(\"tmp/a.txt\") %>"
        "<% echo file_exists(\"tmp/a.txt\") %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "truefalse");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("file_delete missing returns false") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_delete_missing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_delete_missing_program",
        "<% echo file_delete(\"missing.txt\") %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "false");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("file_size returns byte count") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_file_size";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_file_size_program",
        "<% dir_create(\"tmp\") %>"
        "<% file_write(\"tmp/a.txt\", \"hello\") %>"
        "<% echo file_size(\"tmp/a.txt\") %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "5");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("file_modified returns epoch seconds") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_file_modified";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_file_modified_program",
        "<% dir_create(\"tmp\") %>"
        "<% file_write(\"tmp/a.txt\", \"x\") %>"
        "<% if file_modified(\"tmp/a.txt\") > 0 %>true<% else %>false<% end %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "true");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("dir_exists checks directories") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_dir_exists_builtin";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_dir_exists_builtin_program",
        "<% dir_create(\"notes\") %>"
        "<% echo dir_exists(\"notes\") %>"
        "<% echo dir_exists(\"missing\") %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "truefalse");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("file_size rejects directory inputs") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_storage_file_size_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_storage_file_size_dir_program",
        "<% dir_create(\"notes\") %>"
        "<% file_size(\"notes\") %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("not a file") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_connect insert and query") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_basic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_basic_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_exec(\"create table if not exists users (id integer primary key autoincrement, name text, active integer)\") %>"
        "<% db_exec(\"insert into users(name, active) values(?, ?)\", [\"Juan\", true]) %>"
        "<% var rows = db_query(\"select id, name, active from users\") %>"
        "<% echo count(rows) %>"
        "<% echo rows[0][\"name\"] %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "1Juan");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_last_insert_id returns value") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_last_id";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_last_id_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_exec(\"create table if not exists t (id integer primary key autoincrement, name text)\") %>"
        "<% db_exec(\"insert into t(name) values(?)\", [\"A\"]) %>"
        "<% echo db_last_insert_id() > 0 %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "true");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_close requires connection and allows reconnect") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_close";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_close_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/a.db\") %>"
        "<% db_close() %>"
        "<% db_connect(\"data/b.db\") %>"
        "<% db_exec(\"create table if not exists t (id integer)\") %>"
        "<% echo true %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "true");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_close without connect errors") {
    auto program = create_temp_file_with_content(
        "polonio_sqlite_close_error",
        "<% db_close() %>");
    auto result = run_polonio({"run", program});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("database not connected") != std::string::npos);
    std::filesystem::remove(program);
}

TEST_CASE("db parameter binding types roundtrip") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_params";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_params_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_exec(\"create table if not exists t (a text, b integer, c real, d text)\") %>"
        "<% db_exec(\"insert into t(a,b,c,d) values(?,?,?,?)\", [\"x\", true, 3.5, null]) %>"
        "<% var rows = db_query(\"select a,b,c,d from t\") %>"
        "<% echo rows[0][\"a\"] %>"
        "<% echo rows[0][\"b\"] == 1 %>"
        "<% echo rows[0][\"c\"] == 3.5 %>"
        "<% echo rows[0][\"d\"] == null %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "xtruetruetrue");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_exec rejects unsupported parameter type") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_param_error";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_param_error_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_exec(\"create table if not exists t (a text)\") %>"
        "<% db_exec(\"insert into t(a) values(?)\", [[1,2,3]]) %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("unsupported parameter type") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_connect enforces path sandbox and storage root") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_paths";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());

    auto traversal_prog = create_temp_file_with_content(
        "polonio_sqlite_traversal",
        "<% db_connect(\"../escape.db\") %>");
    auto traversal = run_polonio({"run", traversal_prog}, env);
    CHECK(traversal.exit_code != 0);
    CHECK(traversal.stderr_output.find("path traversal") != std::string::npos);
    std::filesystem::remove(traversal_prog);

    auto absolute_prog = create_temp_file_with_content(
        "polonio_sqlite_absolute",
        "<% db_connect(\"/tmp/x.db\") %>");
    auto absolute = run_polonio({"run", absolute_prog}, env);
    CHECK(absolute.exit_code != 0);
    CHECK(absolute.stderr_output.find("absolute path") != std::string::npos);
    std::filesystem::remove(absolute_prog);

    auto missing_root_prog = create_temp_file_with_content(
        "polonio_sqlite_missing_root",
        "<% db_connect(\"data/app.db\") %>");
    auto missing_root = run_polonio({"run", missing_root_prog});
    CHECK(missing_root.exit_code != 0);
    CHECK(missing_root.stderr_output.find("missing storage root") != std::string::npos);
    std::filesystem::remove(missing_root_prog);

    std::filesystem::remove_all(dir);
}

TEST_CASE("db_connect requires parent directory") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_parent";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_parent_program",
        "<% db_connect(\"missing/app.db\") %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("missing directory") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_begin commit persists changes") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_tx_commit";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_tx_commit_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_exec(\"create table if not exists t (id integer primary key autoincrement, name text)\") %>"
        "<% db_begin() %>"
        "<% db_exec(\"insert into t(name) values(?)\", [\"A\"]) %>"
        "<% db_commit() %>"
        "<% var rows = db_query(\"select name from t\") %>"
        "<% echo count(rows) %>"
        "<% echo rows[0][\"name\"] %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "1A");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_rollback discards transaction") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_tx_rollback";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_tx_rollback_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_exec(\"create table if not exists t (id integer primary key autoincrement, name text)\") %>"
        "<% db_begin() %>"
        "<% db_exec(\"insert into t(name) values(?)\", [\"A\"]) %>"
        "<% db_rollback() %>"
        "<% var rows = db_query(\"select name from t\") %>"
        "<% echo count(rows) %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "0");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_begin rejects nested transactions") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_tx_nested";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_tx_nested_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_begin() %>"
        "<% db_begin() %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("transaction already active") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_commit requires active transaction") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_tx_commit_error";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_tx_commit_error_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_commit() %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("no active transaction") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_rollback requires active transaction") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_tx_rollback_error";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_tx_rollback_error_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_rollback() %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("no active transaction") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db_close resets transaction state") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_sqlite_tx_close";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto env = storage_env(dir.string());
    auto program = create_temp_file_with_content(
        "polonio_sqlite_tx_close_program",
        "<% dir_create(\"data\") %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_begin() %>"
        "<% db_close() %>"
        "<% db_connect(\"data/app.db\") %>"
        "<% db_begin() %>"
        "<% db_rollback() %>"
        "<% echo true %>");
    auto result = run_polonio({"run", program}, env);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "true");
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("db transaction builtins require connection") {
    auto prog_begin = create_temp_file_with_content(
        "polonio_sqlite_tx_no_conn_begin",
        "<% db_begin() %>");
    auto prog_commit = create_temp_file_with_content(
        "polonio_sqlite_tx_no_conn_commit",
        "<% db_commit() %>");
    auto prog_rollback = create_temp_file_with_content(
        "polonio_sqlite_tx_no_conn_rollback",
        "<% db_rollback() %>");
    auto result_begin = run_polonio({"run", prog_begin});
    auto result_commit = run_polonio({"run", prog_commit});
    auto result_rollback = run_polonio({"run", prog_rollback});
    CHECK(result_begin.exit_code != 0);
    CHECK(result_begin.stderr_output.find("database not connected") != std::string::npos);
    CHECK(result_commit.exit_code != 0);
    CHECK(result_commit.stderr_output.find("database not connected") != std::string::npos);
    CHECK(result_rollback.exit_code != 0);
    CHECK(result_rollback.stderr_output.find("database not connected") != std::string::npos);
    std::filesystem::remove(prog_begin);
    std::filesystem::remove(prog_commit);
    std::filesystem::remove(prog_rollback);
}

TEST_CASE("send_file serves basic text file via CGI") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_file_text";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "files");
    {
        std::ofstream file(dir / "files/hello.txt", std::ios::binary);
        file << "hello";
    }
    auto result = run_cgi_template(
        "polonio_send_file_basic",
        "<% send_file(\"files/hello.txt\") %>",
        storage_env(dir.string()));
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("Content-Type: text/plain; charset=utf-8") != std::string::npos);
    CHECK(response_body(result.stdout_output) == "hello");
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_file supports custom content type") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_file_ct";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "files");
    {
        std::ofstream file(dir / "files/hello.txt", std::ios::binary);
        file << "hello";
    }
    auto result = run_cgi_template(
        "polonio_send_file_ct_program",
        "<% send_file(\"files/hello.txt\", {\"content_type\": \"application/x-test\"}) %>",
        storage_env(dir.string()));
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("Content-Type: application/x-test") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_file supports download name attachment") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_file_download";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "files");
    {
        std::ofstream file(dir / "files/data.txt", std::ios::binary);
        file << "data";
    }
    auto result = run_cgi_template(
        "polonio_send_file_download_program",
        "<% send_file(\"files/data.txt\", {\"download_name\": \"download.txt\"}) %>",
        storage_env(dir.string()));
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("Content-Disposition: attachment; filename=\"download.txt\"") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_file supports inline download disposition") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_file_inline";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "files");
    {
        std::ofstream file(dir / "files/page.html", std::ios::binary);
        file << "<p>hi</p>";
    }
    auto result = run_cgi_template(
        "polonio_send_file_inline_program",
        "<% send_file(\"files/page.html\", {\"download_name\": \"view.html\", \"inline\": true}) %>",
        storage_env(dir.string()));
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("Content-Disposition: inline; filename=\"view.html\"") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_file finalizes response") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_file_final";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "files");
    {
        std::ofstream file(dir / "files/hello.txt", std::ios::binary);
        file << "hello";
    }
    auto result = run_cgi_template(
        "polonio_send_file_final_program",
        "<% send_file(\"files/hello.txt\") %>after",
        storage_env(dir.string()));
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("response already finalized") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_file missing file errors") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_file_missing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "files");
    auto result = run_cgi_template(
        "polonio_send_file_missing_program",
        "<% send_file(\"files/missing.txt\") %>",
        storage_env(dir.string()));
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("file not found") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_file rejects directories") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_file_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "files" / "dir");
    auto result = run_cgi_template(
        "polonio_send_file_dir_program",
        "<% send_file(\"files/dir\") %>",
        storage_env(dir.string()));
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("not a file") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_file enforces sandbox paths") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_file_sandbox";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "files");
    auto traversal = run_cgi_template(
        "polonio_send_file_traversal_program",
        "<% send_file(\"../escape.txt\") %>",
        storage_env(dir.string()));
    CHECK(traversal.exit_code != 0);
    CHECK(traversal.stdout_output.find("path traversal") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_file rejects absolute paths") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_file_abs";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto result = run_cgi_template(
        "polonio_send_file_abs_program",
        "<% send_file(\"/tmp/secret.txt\") %>",
        storage_env(dir.string()));
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("absolute path") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_file requires storage root") {
    auto program = "<% send_file(\"files/hello.txt\") %>";
    auto result = run_cgi_template("polonio_send_file_no_root", program);
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("missing storage root") != std::string::npos);
}

TEST_CASE("send_file handles binary data") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_file_binary";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "files");
    std::string data("\x89PNG\r\n\x1a\n\x00\x00\x00\x00", 12);
    {
        std::ofstream file(dir / "files/pixel.png", std::ios::binary);
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    auto result = run_cgi_template(
        "polonio_send_file_binary_program",
        "<% send_file(\"files/pixel.png\") %>",
        storage_env(dir.string()));
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("Content-Type: image/png") != std::string::npos);
    CHECK(response_body(result.stdout_output) == data);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_mail writes file in outbox") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_mail_basic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto program = create_temp_file_with_content(
        "polonio_send_mail_basic_program",
        "<% echo send_mail(\"ada@example.com\", \"Hello\", \"Body\") %>");
    auto result = run_polonio({"run", program}, storage_env(dir.string()));
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "true");
    auto outbox = dir / "mail" / "outbox";
    auto files = list_files(outbox);
    REQUIRE(files.size() == 1);
    auto contents = read_text_file(files[0]);
    CHECK(contents.find("To: ada@example.com") != std::string::npos);
    CHECK(contents.find("From: noreply@polonio.local") != std::string::npos);
    CHECK(contents.find("Subject: Hello") != std::string::npos);
    CHECK(contents.find("Content-Type: text/plain; charset=utf-8") != std::string::npos);
    CHECK(contents.find("X-Polonio-Mailer: file-mode") != std::string::npos);
    CHECK(contents.find("\n\nBody") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_mail supports custom from and content type") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_mail_custom";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto program = create_temp_file_with_content(
        "polonio_send_mail_custom_program",
        "<% echo send_mail(\"ada@example.com\", \"Hello\", \"<b>Body</b>\", {"
        "\"from\": \"app@example.com\","
        "\"reply_to\": \"support@example.com\","
        "\"content_type\": \"text/html; charset=utf-8\""
        "}) %>");
    auto result = run_polonio({"run", program}, storage_env(dir.string()));
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "true");
    auto files = list_files(dir / "mail" / "outbox");
    REQUIRE(files.size() == 1);
    auto contents = read_text_file(files[0]);
    CHECK(contents.find("From: app@example.com") != std::string::npos);
    CHECK(contents.find("Reply-To: support@example.com") != std::string::npos);
    CHECK(contents.find("Content-Type: text/html; charset=utf-8") != std::string::npos);
    CHECK(contents.find("<b>Body</b>") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_mail writes custom headers") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_mail_headers";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto program = create_temp_file_with_content(
        "polonio_send_mail_headers_program",
        "<% echo send_mail(\"ada@example.com\", \"Hi\", \"Body\", {"
        "\"headers\": {\"X-App\": \"Scraps\", \"X-Env\": \"test\"}"
        "}) %>");
    auto result = run_polonio({"run", program}, storage_env(dir.string()));
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "true");
    auto files = list_files(dir / "mail" / "outbox");
    REQUIRE(files.size() == 1);
    auto contents = read_text_file(files[0]);
    CHECK(contents.find("X-App: Scraps") != std::string::npos);
    CHECK(contents.find("X-Env: test") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_mail rejects reserved headers") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_mail_reserved";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto program = create_temp_file_with_content(
        "polonio_send_mail_reserved_program",
        "<% send_mail(\"ada@example.com\", \"Hi\", \"Body\", {"
        "\"headers\": {\"Subject\": \"Nope\"}"
        "}) %>");
    auto result = run_polonio({"run", program}, storage_env(dir.string()));
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("reserved header") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_mail rejects invalid header values") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_mail_invalid_header";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto program = create_temp_file_with_content(
        "polonio_send_mail_invalid_header_program",
        "<% send_mail(\"ada@example.com\", \"Hi\", \"Body\", {"
        "\"headers\": {\"X-Test\": \"bad\\nvalue\"}"
        "}) %>");
    auto result = run_polonio({"run", program}, storage_env(dir.string()));
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("invalid header") != std::string::npos);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_mail requires storage root") {
    auto program = create_temp_file_with_content(
        "polonio_send_mail_no_root",
        "<% send_mail(\"ada@example.com\", \"Hi\", \"Body\") %>");
    auto result = run_polonio({"run", program});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("missing storage root") != std::string::npos);
    std::filesystem::remove(program);
}

TEST_CASE("send_mail validates arguments") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_mail_args";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto program = create_temp_file_with_content(
        "polonio_send_mail_args_program",
        "<% send_mail(\"\", \"Hi\", \"Body\") %>");
    auto result = run_polonio({"run", program}, storage_env(dir.string()));
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("send_mail") != std::string::npos);
    std::filesystem::remove(program);

    auto program2 = create_temp_file_with_content(
        "polonio_send_mail_args_program2",
        "<% send_mail(\"ada@example.com\", \"Hi\") %>");
    auto result2 = run_polonio({"run", program2}, storage_env(dir.string()));
    CHECK(result2.exit_code != 0);
    CHECK(result2.stderr_output.find("expected") != std::string::npos);
    std::filesystem::remove(program2);
    std::filesystem::remove_all(dir);
}

TEST_CASE("send_mail creates multiple files") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_send_mail_multi";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto program = create_temp_file_with_content(
        "polonio_send_mail_multi_program",
        "<% send_mail(\"ada@example.com\", \"Hi\", \"One\") %>"
        "<% send_mail(\"bob@example.com\", \"Hi\", \"Two\") %>");
    auto result = run_polonio({"run", program}, storage_env(dir.string()));
    CHECK(result.exit_code == 0);
    auto files = list_files(dir / "mail" / "outbox");
    CHECK(files.size() == 2);
    std::filesystem::remove(program);
    std::filesystem::remove_all(dir);
}

TEST_CASE("multipart upload exposes file metadata") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_multipart_basic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::vector<MultipartPart> parts = {
        {"upload", true, "a.txt", "text/plain", "hello"},
    };
    auto tpl = R"(<%
var f = _FILES["upload"]
echo f["name"]
echo "\n"
echo f["type"]
echo "\n"
echo f["size"] > 0
echo "\n"
echo file_read(f["tmp_path"])
%>)";
    auto result = run_multipart_template("polonio_multipart_basic_program", tpl, parts, dir);
    CHECK(result.exit_code == 0);
    auto lines = split_lines(parse_cgi_output(result.stdout_output).body);
    REQUIRE(lines.size() >= 4);
    CHECK(lines[0] == "a.txt");
    CHECK(lines[1] == "text/plain");
    CHECK(lines[2] == "true");
    CHECK(lines[3] == "hello");
    std::filesystem::remove_all(dir);
}

TEST_CASE("multipart text field populates _POST") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_multipart_text";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::vector<MultipartPart> parts = {
        {"title", false, "", "", "Hello"},
    };
    auto result = run_multipart_template("polonio_multipart_text_program", "<% echo _POST[\"title\"] %>", parts, dir);
    CHECK(result.exit_code == 0);
    CHECK(parse_cgi_output(result.stdout_output).body == "Hello");
    std::filesystem::remove_all(dir);
}

TEST_CASE("multipart mixed fields populate _POST and _FILES") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_multipart_mixed";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::vector<MultipartPart> parts = {
        {"title", false, "", "", "Hello"},
        {"upload", true, "a.txt", "text/plain", "world"},
    };
    auto tpl = R"(<%
echo _POST["title"]
echo _FILES["upload"]["name"]
%>)";
    auto result = run_multipart_template("polonio_multipart_mixed_program", tpl, parts, dir);
    CHECK(result.exit_code == 0);
    CHECK(parse_cgi_output(result.stdout_output).body == "Helloa.txt");
    std::filesystem::remove_all(dir);
}

TEST_CASE("multipart repeated file fields become array") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_multipart_repeat";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::vector<MultipartPart> parts = {
        {"upload", true, "a.txt", "text/plain", "A"},
        {"upload", true, "b.txt", "text/plain", "B"},
    };
    auto tpl = R"(<%
var files = _FILES["upload"]
echo count(files)
echo "\n"
echo files[0]["name"]
echo "\n"
echo files[1]["name"]
%>)";
    auto result = run_multipart_template("polonio_multipart_repeat_program", tpl, parts, dir);
    CHECK(result.exit_code == 0);
    auto lines = split_lines(parse_cgi_output(result.stdout_output).body);
    REQUIRE(lines.size() >= 3);
    CHECK(lines[0] == "2");
    CHECK(lines[1] == "a.txt");
    CHECK(lines[2] == "b.txt");
    std::filesystem::remove_all(dir);
}

TEST_CASE("upload_save moves uploaded file") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_upload_save";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::vector<MultipartPart> parts = {
        {"upload", true, "a.txt", "text/plain", "hello"},
    };
    auto tpl = R"(<%
var f = _FILES["upload"]
dir_create("uploads")
upload_save(f, "uploads/final.txt")
echo file_exists("uploads/final.txt")
echo "\n"
echo file_read("uploads/final.txt")
%>)";
    auto result = run_multipart_template("polonio_upload_save_program", tpl, parts, dir);
    CHECK(result.exit_code == 0);
    auto lines = split_lines(parse_cgi_output(result.stdout_output).body);
    REQUIRE(lines.size() >= 2);
    CHECK(lines[0] == "true");
    CHECK(lines[1] == "hello");
    std::filesystem::remove_all(dir);
}

TEST_CASE("multipart upload requires storage root") {
    auto part = MultipartPart{"upload", true, "a.txt", "text/plain", "hello"};
    auto boundary = "----PolonioBoundaryTest";
    auto body = build_multipart_body(boundary, {part});
    std::vector<std::pair<std::string, std::string>> env = {
        {"REQUEST_METHOD", "POST"},
        {"CONTENT_TYPE", std::string("multipart/form-data; boundary=") + boundary},
        {"CONTENT_LENGTH", std::to_string(body.size())},
    };
    auto result = run_cgi_template("polonio_multipart_no_root", "<% echo \"noop\" %>", env, body);
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("missing storage root") != std::string::npos);
}

TEST_CASE("upload_save rejects invalid destination path") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_upload_save_escape";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::vector<MultipartPart> parts = {
        {"upload", true, "a.txt", "text/plain", "hello"},
    };
    auto tpl = R"(<%
var f = _FILES["upload"]
upload_save(f, "../escape.txt")
%>)";
    auto result = run_multipart_template("polonio_upload_save_escape_program", tpl, parts, dir);
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("path traversal") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("multipart empty file upload recorded") {
    auto dir = std::filesystem::temp_directory_path() / "polonio_multipart_empty";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::vector<MultipartPart> parts = {
        {"upload", true, "empty.bin", "application/octet-stream", ""},
    };
    auto tpl = R"(<%
var f = _FILES["upload"]
echo f["size"] == 0
echo "\n"
echo file_exists(f["tmp_path"])
%>)";
    auto result = run_multipart_template("polonio_multipart_empty_program", tpl, parts, dir);
    CHECK(result.exit_code == 0);
    auto lines = split_lines(parse_cgi_output(result.stdout_output).body);
    REQUIRE(lines.size() >= 2);
    CHECK(lines[0] == "true");
    CHECK(lines[1] == "true");
    std::filesystem::remove_all(dir);
}

TEST_CASE("CGI header builtin validates syntax") {
    auto path = create_temp_file_with_content("polonio_cgi_header_bad",
                                              "<% header(\"bad header\") %>");
    std::vector<std::pair<std::string, std::string>> env = {
        {"GATEWAY_INTERFACE", "CGI/1.1"},
        {"SCRIPT_FILENAME", path},
    };
    auto result = run_polonio_cgi(env);
    CHECK(result.exit_code != 0);
    CHECK(result.stdout_output.find("Status: 500") != std::string::npos);
    CHECK(result.stdout_output.find("invalid header") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("htmlspecialchars escapes string") {
    auto path = create_temp_file_with_content("polonio_escape",
                                              "<% echo htmlspecialchars(\"<a&b>\\\"'\") %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "&lt;a&amp;b&gt;&quot;&#39;");
    std::filesystem::remove(path);
}

TEST_CASE("html_escape escapes standard characters") {
    auto path = create_temp_file_with_content("polonio_html_escape_basic",
                                              "<% echo html_escape(\"<>&\\\"'\") %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "&lt;&gt;&amp;&quot;&#39;");
    std::filesystem::remove(path);
}

TEST_CASE("html_escape leaves plain text unchanged") {
    auto path = create_temp_file_with_content("polonio_html_escape_plain",
                                              "<% echo html_escape(\"hello\") %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "hello");
    std::filesystem::remove(path);
}

TEST_CASE("html_escape handles mixed and non-string values") {
    auto path = create_temp_file_with_content("polonio_html_escape_mixed",
                                              "<% echo html_escape(\"Tom & Jerry <3\") %>"
                                              "<% echo html_escape(42) %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "Tom &amp; Jerry &lt;342");
    std::filesystem::remove(path);
}

TEST_CASE("html_escape validates arity") {
    auto path = create_temp_file_with_content("polonio_html_escape_arity",
                                              "<% html_escape(\"a\", \"b\") %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("html_escape") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("debug writes to stderr without affecting stdout") {
    auto path = create_temp_file_with_content("polonio_debug_hi",
                                              "<% debug(\"hi\") %><% echo \"OK\" %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "OK");
    CHECK(result.stderr_output.find("\"hi\"") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("debug describes arrays and objects") {
    auto path = create_temp_file_with_content("polonio_debug_structs",
                                              "<% debug([1,2]) %><% debug({\"a\": 1}) %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.empty());
    CHECK(result.stderr_output.find("array(len=2)") != std::string::npos);
    CHECK(result.stderr_output.find("object(len=1)") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("debug validates arity") {
    auto path = create_temp_file_with_content("polonio_debug_arity",
                                              "<% debug() %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("debug") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("header/status error outside CGI mode") {
    auto path = create_temp_file_with_content("polonio_noncgi_header",
                                              "<% header(\"X\", \"1\") %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("CGI") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("redirect errors outside CGI mode") {
    auto path = create_temp_file_with_content("polonio_redirect_noncgi", "<% redirect(\"/login\") %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("CGI") != std::string::npos);
    std::filesystem::remove(path);
}

TEST_CASE("CLI: version command") {
    auto result = run_polonio({"version"});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "0.1.0\n");
    CHECK(result.stderr_output.empty());
}

TEST_CASE("CLI: help command shows usage text") {
    auto result = run_polonio({"help"});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("Usage:") != std::string::npos);
    CHECK(result.stdout_output.find("polonio run") != std::string::npos);
}

// run command tests updated after interpreter wiring

TEST_CASE("CLI: run without file shows usage") {
    auto result = run_polonio({"run"});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("Usage:") != std::string::npos);
}

TEST_CASE("CLI: run with extra args errors") {
    auto result = run_polonio({"run", "a.pol", "b.pol"});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("Usage:") != std::string::npos);
}

TEST_CASE("CLI: flag-like arg is treated as unknown command") {
    auto result = run_polonio({"--help"});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("Unknown command") != std::string::npos);
}

TEST_CASE("Source::from_file loads entire file contents") {
    const std::string input = "hello world\nsecond line\r\n";
    auto path = create_temp_file_with_content("polonio_source", input);
    auto src = polonio::Source::from_file(path);
    CHECK(src.path() == path);
    CHECK(src.content() == input);
    CHECK(src.size() == input.size());
    std::filesystem::remove(path);
}

TEST_CASE("Source::from_file throws when file is missing") {
    auto missing = (std::filesystem::temp_directory_path() / "polonio_missing_source_file").string();
    if (std::filesystem::exists(missing)) {
        std::filesystem::remove(missing);
    }

    CHECK_THROWS_AS(polonio::Source::from_file(missing), polonio::PolonioError);

    try {
        polonio::Source::from_file(missing);
        FAIL_CHECK("expected PolonioError");
    } catch (const polonio::PolonioError& err) {
        CHECK(err.kind() == polonio::ErrorKind::IO);
        auto formatted = err.format();
        CHECK(formatted.find(missing) != std::string::npos);
        CHECK(formatted.find(":1:1:") != std::string::npos);
    }
}

TEST_CASE("urlencode builtin escapes reserved characters") {
    auto path = create_temp_file_with_content("polonio_urlencode",
                                              "<% echo urlencode(\"hello world &/!\") %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "hello+world+%26%2F%21");
    std::filesystem::remove(path);
}

TEST_CASE("urldecode builtin decodes sequences and validates input") {
    auto path = create_temp_file_with_content("polonio_urldecode", "<% echo urldecode(\"a+%26+b\") %>");
    auto result = run_polonio({"run", path});
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "a & b");
    std::filesystem::remove(path);

    auto bad_path = create_temp_file_with_content("polonio_urldecode_bad", "<% echo urldecode(\"%ZZ\") %>");
    auto bad = run_polonio({"run", bad_path});
    CHECK(bad.exit_code != 0);
    CHECK(bad.stderr_output.find("urldecode") != std::string::npos);
    std::filesystem::remove(bad_path);
}

TEST_CASE("PolonioError format includes path and location") {
    polonio::PolonioError err(
        polonio::ErrorKind::Parse,
        "unexpected token",
        "example.pol",
        polonio::Location{5, 2, 3}
    );
    CHECK(err.format() == std::string("example.pol:2:3: unexpected token"));
}

TEST_CASE("Location start is beginning of file") {
    polonio::Location loc = polonio::Location::start();
    CHECK(loc.offset == 0);
    CHECK(loc.line == 1);
    CHECK(loc.column == 1);
}

TEST_CASE("Location advance across simple text") {
    polonio::Location loc = polonio::Location::start();
    loc = polonio::advance(loc, "abc");
    CHECK(loc.offset == 3);
    CHECK(loc.line == 1);
    CHECK(loc.column == 4);
}

TEST_CASE("Location advance handles newline transitions") {
    polonio::Location loc = polonio::Location::start();
    loc = polonio::advance(loc, "a\nb");
    CHECK(loc.offset == 3);
    CHECK(loc.line == 2);
    CHECK(loc.column == 2);
}

TEST_CASE("Location advance handles multiple newlines") {
    polonio::Location loc = polonio::Location::start();
    loc = polonio::advance(loc, "line1\n\nline2");
    CHECK(loc.offset == 12);
    CHECK(loc.line == 3);
    CHECK(loc.column == 6);
}

namespace {

std::vector<polonio::TokenKind> kinds(const std::vector<polonio::Token>& tokens) {
    std::vector<polonio::TokenKind> result;
    result.reserve(tokens.size());
    for (const auto& token : tokens) {
        result.push_back(token.kind);
    }
    return result;
}

std::string parse_expr(const std::string& input) {
    polonio::Lexer lexer(input);
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens);
    auto expr = parser.parse_expression();
    return expr->dump();
}

} // namespace

TEST_CASE("Lexer recognizes keywords and identifiers") {
    polonio::Lexer lexer("var name function foo echo true false null and or not end");
    auto tokens = lexer.scan_all();
    auto kinds_only = kinds(tokens);
    std::vector<polonio::TokenKind> expected = {
        polonio::TokenKind::Var,
        polonio::TokenKind::Identifier,
        polonio::TokenKind::Function,
        polonio::TokenKind::Identifier,
        polonio::TokenKind::Echo,
        polonio::TokenKind::True,
        polonio::TokenKind::False,
        polonio::TokenKind::Null,
        polonio::TokenKind::And,
        polonio::TokenKind::Or,
        polonio::TokenKind::Not,
        polonio::TokenKind::End,
        polonio::TokenKind::EndOfFile,
    };
    CHECK(kinds_only == expected);
    CHECK(tokens[1].lexeme == "name");
    CHECK(tokens[3].lexeme == "foo");
}

TEST_CASE("Lexer parses numbers") {
    polonio::Lexer lexer("0 42 3.14 10.0");
    auto tokens = lexer.scan_all();
    CHECK(tokens[0].lexeme == "0");
    CHECK(tokens[1].lexeme == "42");
    CHECK(tokens[2].lexeme == "3.14");
    CHECK(tokens[3].lexeme == "10.0");
}

TEST_CASE("Lexer parses string literals") {
    polonio::Lexer lexer("\"hi\" 'hi' \"a\\n\\t\\\\\\\"\"");
    auto tokens = lexer.scan_all();
    CHECK(tokens[0].lexeme == "\"hi\"");
    CHECK(tokens[1].lexeme == "'hi'");
    CHECK(tokens[2].lexeme == "\"a\\n\\t\\\\\\\"\"");
}

TEST_CASE("Lexer tracks location across lines") {
    polonio::Lexer lexer("var x\nvar y");
    auto tokens = lexer.scan_all();
    CHECK(tokens[0].span.start.line == 1);
    CHECK(tokens[2].span.start.line == 2);
    CHECK(tokens[2].span.start.column == 1);
}

TEST_CASE("Lexer handles punctuation tokens") {
    polonio::Lexer lexer("()[]{} ,:;");
    auto tokens = lexer.scan_all();
    std::vector<polonio::TokenKind> expected = {
        polonio::TokenKind::LeftParen,
        polonio::TokenKind::RightParen,
        polonio::TokenKind::LeftBracket,
        polonio::TokenKind::RightBracket,
        polonio::TokenKind::LeftBrace,
        polonio::TokenKind::RightBrace,
        polonio::TokenKind::Comma,
        polonio::TokenKind::Colon,
        polonio::TokenKind::Semicolon,
        polonio::TokenKind::EndOfFile,
    };
    CHECK(kinds(tokens) == expected);
    CHECK(tokens[0].lexeme == "(");
    CHECK(tokens[5].lexeme == "}");
    CHECK(tokens[6].lexeme == ",");
}

TEST_CASE("Lexer handles basic operators") {
    polonio::Lexer lexer("= + - * / % < >");
    auto tokens = lexer.scan_all();
    std::vector<polonio::TokenKind> expected = {
        polonio::TokenKind::Equal,
        polonio::TokenKind::Plus,
        polonio::TokenKind::Minus,
        polonio::TokenKind::Star,
        polonio::TokenKind::Slash,
        polonio::TokenKind::Percent,
        polonio::TokenKind::Less,
        polonio::TokenKind::Greater,
        polonio::TokenKind::EndOfFile,
    };
    CHECK(kinds(tokens) == expected);
}

TEST_CASE("Lexer handles multi-character operators with longest match") {
    polonio::Lexer lexer("== != <= >= += -= *= /= %= .. ..=");
    auto tokens = lexer.scan_all();
    std::vector<polonio::TokenKind> expected = {
        polonio::TokenKind::EqualEqual,
        polonio::TokenKind::NotEqual,
        polonio::TokenKind::LessEqual,
        polonio::TokenKind::GreaterEqual,
        polonio::TokenKind::PlusEqual,
        polonio::TokenKind::MinusEqual,
        polonio::TokenKind::StarEqual,
        polonio::TokenKind::SlashEqual,
        polonio::TokenKind::PercentEqual,
        polonio::TokenKind::DotDot,
        polonio::TokenKind::DotDotEqual,
        polonio::TokenKind::EndOfFile,
    };
    CHECK(kinds(tokens) == expected);
    CHECK(tokens[9].lexeme == "..");
    CHECK(tokens[10].lexeme == "..=");
}

TEST_CASE("Lexer tokenizes mixed snippet") {
    polonio::Lexer lexer("var x = 10 + 20 .. \"!\"");
    auto tokens = lexer.scan_all();
    std::vector<polonio::TokenKind> expected = {
        polonio::TokenKind::Var,
        polonio::TokenKind::Identifier,
        polonio::TokenKind::Equal,
        polonio::TokenKind::Number,
        polonio::TokenKind::Plus,
        polonio::TokenKind::Number,
        polonio::TokenKind::DotDot,
        polonio::TokenKind::String,
        polonio::TokenKind::EndOfFile,
    };
    CHECK(kinds(tokens) == expected);
    CHECK(tokens[1].lexeme == "x");
    CHECK(tokens[7].lexeme == "\"!\"");
}

TEST_CASE("Lexer rejects single dot") {
    polonio::Lexer lexer(".");
    bool threw = false;
    try {
        lexer.scan_all();
    } catch (const polonio::PolonioError& err) {
        threw = true;
        CHECK(err.kind() == polonio::ErrorKind::Lex);
        CHECK(err.location().line == 1);
        CHECK(err.location().column == 1);
    }
    CHECK(threw);
}

TEST_CASE("Lexer skips block comments between tokens") {
    polonio::Lexer lexer("var /*comment*/ x");
    auto tokens = lexer.scan_all();
    std::vector<polonio::TokenKind> expected = {
        polonio::TokenKind::Var,
        polonio::TokenKind::Identifier,
        polonio::TokenKind::EndOfFile,
    };
    CHECK(kinds(tokens) == expected);
    CHECK(tokens[1].lexeme == "x");
}

TEST_CASE("Lexer skips multiline comments and tracks location") {
    const char* input = "var x\n/* a\nb\nc */\nvar y";
    polonio::Lexer lexer(input);
    auto tokens = lexer.scan_all();
    CHECK(tokens[0].span.start.line == 1);
    CHECK(tokens[2].span.start.line == 5);
    CHECK(tokens[2].span.start.column == 1);
}

TEST_CASE("Lexer errors on unterminated block comment") {
    polonio::Lexer lexer("var x /* oops");
    bool threw = false;
    try {
        lexer.scan_all();
    } catch (const polonio::PolonioError& err) {
        threw = true;
        CHECK(err.kind() == polonio::ErrorKind::Lex);
        CHECK(err.location().line == 1);
        CHECK(err.location().column == 7);
    }
    CHECK(threw);
}

TEST_CASE("Parser respects arithmetic precedence") {
    CHECK(parse_expr("1 + 2 * 3") == "(+ num(1) (* num(2) num(3)))");
    CHECK(parse_expr("(1 + 2) * 3") == "(* (+ num(1) num(2)) num(3))");
}

TEST_CASE("Parser handles concat precedence") {
    CHECK(parse_expr("1 .. 2 + 3") == "(.. num(1) (+ num(2) num(3)))");
}

TEST_CASE("Parser handles comparison and equality") {
    CHECK(parse_expr("1 < 2 == true") == "(== (< num(1) num(2)) bool(true))");
}

TEST_CASE("Parser handles logical operators") {
    CHECK(parse_expr("not true or false") == "(or (not bool(true)) bool(false))");
    CHECK(parse_expr("true and false or true") == "(or (and bool(true) bool(false)) bool(true))");
}

TEST_CASE("Parser errors on incomplete expression") {
    polonio::Lexer lexer("1 +");
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens);
    CHECK_THROWS_AS(parser.parse_expression(), polonio::PolonioError);
}

TEST_CASE("Parser errors on stray closing paren") {
    polonio::Lexer lexer(")");
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens);
    bool threw = false;
    try {
        parser.parse_expression();
    } catch (const polonio::PolonioError& err) {
        threw = true;
        CHECK(err.kind() == polonio::ErrorKind::Parse);
        CHECK(err.location().line == 1);
        CHECK(err.location().column == 1);
    }
    CHECK(threw);
}

TEST_CASE("Parser parses array literals") {
    CHECK(parse_expr("[1, 2, 3]") == "array(num(1), num(2), num(3))");
    CHECK(parse_expr("[1, [2, 3], 4]") == "array(num(1), array(num(2), num(3)), num(4))");
}

TEST_CASE("Parser parses object literals") {
    CHECK(parse_expr("{\"name\": \"Juan\", \"age\": 42}") ==
          "object(\"name\": str(\"Juan\"), \"age\": num(42))");
}

TEST_CASE("Parser handles nested array/object combinations") {
    CHECK(parse_expr("[{\"name\": \"Juan\"}, 42]") ==
          "array(object(\"name\": str(\"Juan\")), num(42))");
}

TEST_CASE("Parser errors on unterminated array") {
    polonio::Lexer lexer("[1, 2");
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens);
    CHECK_THROWS_AS(parser.parse_expression(), polonio::PolonioError);
}

TEST_CASE("Parser errors on invalid object syntax") {
    {
        polonio::Lexer lexer("{\"a\" 1}");
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_expression(), polonio::PolonioError);
    }
    {
        polonio::Lexer lexer("{a: 1}");
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_expression(), polonio::PolonioError);
    }
}

TEST_CASE("Parser handles function calls") {
    CHECK(parse_expr("f(1, 2)") == "call(ident(f), num(1), num(2))");
    CHECK(parse_expr("f(1)(2)") == "call(call(ident(f), num(1)), num(2))");
}

TEST_CASE("Parser handles indexing") {
    CHECK(parse_expr("arr[0]") == "index(ident(arr), num(0))");
    CHECK(parse_expr("arr[0][1]") == "index(index(ident(arr), num(0)), num(1))");
}

TEST_CASE("Parser handles mixed call and index") {
    CHECK(parse_expr("f(x)[0]") == "index(call(ident(f), ident(x)), num(0))");
}

TEST_CASE("Parser handles assignments") {
    CHECK(parse_expr("x = 1") == "assign(ident(x), =, num(1))");
    CHECK(parse_expr("x = y = 2") == "assign(ident(x), =, assign(ident(y), =, num(2)))");
    CHECK(parse_expr("arr[0] += 3") == "assign(index(ident(arr), num(0)), +=, num(3))");
}

TEST_CASE("Parser rejects invalid assignment targets") {
    {
        polonio::Lexer lexer("1 = 2");
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_expression(), polonio::PolonioError);
    }
    {
        polonio::Lexer lexer("(x + 1) = 2");
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_expression(), polonio::PolonioError);
    }
}

namespace {

std::string parse_program(const std::string& input) {
    polonio::Lexer lexer(input, "test.pol");
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens, "test.pol");
    auto program = parser.parse_program();
    return program.dump();
}

polonio::Value eval_runtime_expr(const std::string& input) {
    polonio::Lexer lexer(input, "test.pol");
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens, "test.pol");
    auto expr = parser.parse_expression();
    polonio::Interpreter interpreter(std::make_shared<polonio::Env>(), "test.pol");
    return interpreter.eval_expr(expr);
}

std::string run_program_output(const std::string& input) {
    polonio::Lexer lexer(input, "test.pol");
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens, "test.pol");
    auto program = parser.parse_program();
    polonio::Interpreter interpreter(std::make_shared<polonio::Env>(), "test.pol");
    interpreter.exec_program(program);
    return interpreter.output();
}

} // namespace

TEST_CASE("Statement parser handles var declarations") {
    CHECK(parse_program("var x") == "Program(Var(x))");
    CHECK(parse_program("var x = 1 + 2") == "Program(Var(x, (+ num(1) num(2))))");
}

TEST_CASE("Statement parser handles echo statements") {
    CHECK(parse_program("echo 1 + 2") == "Program(Echo((+ num(1) num(2))))");
}

TEST_CASE("Statement parser handles expression statements") {
    CHECK(parse_program("x = 1") == "Program(Expr(assign(ident(x), =, num(1))))");
}

TEST_CASE("Statement parser handles mixed programs") {
    const char* src = "var x = 1\necho x\nx += 2";
    CHECK(parse_program(src) ==
          "Program(Var(x, num(1)), Echo(ident(x)), Expr(assign(ident(x), +=, num(2))))");
}

TEST_CASE("Statement parser supports optional semicolons") {
    CHECK(parse_program("var x = 1; echo x; x = 2;") ==
          "Program(Var(x, num(1)), Echo(ident(x)), Expr(assign(ident(x), =, num(2))))");
}

TEST_CASE("Statement parser errors on invalid syntax") {
    {
        polonio::Lexer lexer("var");
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_program(), polonio::PolonioError);
    }
    {
        polonio::Lexer lexer("echo");
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_program(), polonio::PolonioError);
    }
}

TEST_CASE("Statement parser handles if/elseif/else") {
    CHECK(parse_program("if true echo 1 end") ==
          "Program(If(Branch(bool(true), [Echo(num(1))])))");

    CHECK(parse_program("if true echo 1 else echo 2 end") ==
          "Program(If(Branch(bool(true), [Echo(num(1))]), Else([Echo(num(2))])))");

    CHECK(parse_program("if x echo 1 elseif y echo 2 else echo 3 end") ==
          "Program(If(Branch(ident(x), [Echo(num(1))]), Branch(ident(y), [Echo(num(2))]), Else([Echo(num(3))])))");
}

TEST_CASE("Statement parser handles nested if") {
    const char* src = "if true if false echo 0 end echo 1 end";
    CHECK(parse_program(src) ==
          "Program(If(Branch(bool(true), [If(Branch(bool(false), [Echo(num(0))])), Echo(num(1))])))");
}

TEST_CASE("Statement parser errors on malformed if") {
    {
        polonio::Lexer lexer("if true echo 1");
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_program(), polonio::PolonioError);
    }
    {
        polonio::Lexer lexer("else echo 1 end");
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_program(), polonio::PolonioError);
    }
    {
        polonio::Lexer lexer("if true else else end");
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_program(), polonio::PolonioError);
    }
    {
        polonio::Lexer lexer("if end");
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_program(), polonio::PolonioError);
    }
}

TEST_CASE("Statement parser handles while loops") {
    CHECK(parse_program("while true echo 1 end") ==
          "Program(While(bool(true), [Echo(num(1))]))");

    const char* src = "while x if y echo 1 end echo 2 end";
    CHECK(parse_program(src) ==
          "Program(While(ident(x), [If(Branch(ident(y), [Echo(num(1))])), Echo(num(2))]))");
}

TEST_CASE("Statement parser handles for loops") {
    CHECK(parse_program("for item in items echo item end") ==
          "Program(For(item, ident(items), [Echo(ident(item))]))");

    CHECK(parse_program("for i, item in items echo i echo item end") ==
          "Program(For(i, item, ident(items), [Echo(ident(i)), Echo(ident(item))]))");
}

TEST_CASE("Statement parser handles nested loops") {
    const char* src = "for i in a for j in b echo j end end";
    CHECK(parse_program(src) ==
          "Program(For(i, ident(a), [For(j, ident(b), [Echo(ident(j))])]))");
}

TEST_CASE("Statement parser errors on malformed loops") {
    std::vector<std::string> cases = {
        "for in xs end",
        "for i, in xs end",
        "for i xs end",
        "for i in end",
        "for i in xs",
        "while end",
    };
    for (const auto& src : cases) {
        polonio::Lexer lexer(src);
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_program(), polonio::PolonioError);
    }
}

TEST_CASE("Statement parser handles function declarations") {
    CHECK(parse_program("function ping() end") == "Program(Function(ping, [], []))");

    const char* src = "function greet(name) echo name end";
    CHECK(parse_program(src) ==
          "Program(Function(greet, [name], [Echo(ident(name))]))");

    const char* nested = "if true function inner(a, b) return a end end";
    CHECK(parse_program(nested) ==
          "Program(If(Branch(bool(true), [Function(inner, [a, b], [Return(ident(a))])])))");
}

TEST_CASE("Statement parser handles return statements") {
    CHECK(parse_program("return 42") == "Program(Return(num(42)))");
    CHECK(parse_program("return") == "Program(Return())");
}

TEST_CASE("Statement parser errors on malformed functions") {
    std::vector<std::string> cases = {
        "function () end",
        "function foo end",
        "function foo( end",
        "function foo(a,) end",
        "function foo(a b) end",
        "function foo()",
    };
    for (const auto& src : cases) {
        polonio::Lexer lexer(src);
        auto tokens = lexer.scan_all();
        polonio::Parser parser(tokens);
        CHECK_THROWS_AS(parser.parse_program(), polonio::PolonioError);
    }
}

TEST_CASE("Value reports type names") {
    polonio::Value null_value;
    CHECK(null_value.type_name() == "null");

    polonio::Value bool_value(true);
    CHECK(bool_value.type_name() == "bool");

    polonio::Value number_value(1.5);
    CHECK(number_value.type_name() == "number");

    polonio::Value string_value("hi");
    CHECK(string_value.type_name() == "string");

    polonio::Value::Array arr = {polonio::Value(1), polonio::Value(2)};
    polonio::Value array_value(arr);
    CHECK(array_value.type_name() == "array");

    polonio::Value::Object obj = {{"a", polonio::Value(1)}};
    polonio::Value object_value(obj);
    CHECK(object_value.type_name() == "object");

    polonio::FunctionValue fn;
    fn.name = "fn";
    fn.closure = std::make_shared<polonio::Env>();
    polonio::Value fn_value(fn);
    CHECK(fn_value.type_name() == "function");
}

TEST_CASE("Value truthiness respects spec rules") {
    CHECK_FALSE(polonio::Value().is_truthy());
    CHECK_FALSE(polonio::Value(false).is_truthy());
    CHECK_FALSE(polonio::Value(0).is_truthy());
    CHECK(polonio::Value(0.1).is_truthy());
    CHECK_FALSE(polonio::Value("").is_truthy());
    CHECK(polonio::Value("x").is_truthy());
    polonio::Value::Array arr;
    CHECK(polonio::Value(arr).is_truthy());
    polonio::Value::Object obj;
    CHECK(polonio::Value(obj).is_truthy());
}

TEST_CASE("Value equality handles nested structures") {
    CHECK(polonio::Value(1) == polonio::Value(1));
    CHECK_FALSE(polonio::Value(1) == polonio::Value(2));
    CHECK(polonio::Value("a") == polonio::Value("a"));
    CHECK(polonio::Value() == polonio::Value());

    polonio::Value::Array arr1 = {polonio::Value(1), polonio::Value(2)};
    polonio::Value::Array arr2 = {polonio::Value(1), polonio::Value(2)};
    CHECK(polonio::Value(arr1) == polonio::Value(arr2));

    polonio::Value::Object obj1 = {{"a", polonio::Value(1)}};
    polonio::Value::Object obj2 = {{"a", polonio::Value(1)}};
    CHECK(polonio::Value(obj1) == polonio::Value(obj2));

    polonio::Value::Object obj3 = {{"a", polonio::Value(2)}};
    CHECK_FALSE(polonio::Value(obj1) == polonio::Value(obj3));
}

TEST_CASE("Env supports lexical scoping and assignment") {
    auto global = std::make_shared<polonio::Env>();
    global->set_local("x", polonio::Value(1));

    auto child = std::make_shared<polonio::Env>(global);
    auto* found = child->find("x");
    REQUIRE(found != nullptr);
    CHECK(*found == polonio::Value(1));

    child->set_local("y", polonio::Value(2));
    CHECK(global->find("y") == nullptr);

    child->assign("x", polonio::Value(3));
    auto* global_x = global->find("x");
    REQUIRE(global_x != nullptr);
    CHECK(*global_x == polonio::Value(3));

    child->set_local("x", polonio::Value(9));
    auto* child_x = child->find("x");
    REQUIRE(child_x != nullptr);
    CHECK(*child_x == polonio::Value(9));
    global_x = global->find("x");
    REQUIRE(global_x != nullptr);
    CHECK(*global_x == polonio::Value(3));

    child->assign("z", polonio::Value(7));
    auto* child_z = child->find("z");
    REQUIRE(child_z != nullptr);
    CHECK(*child_z == polonio::Value(7));
    CHECK(global->find("z") == nullptr);
}

TEST_CASE("Interpreter evaluates expressions") {
    CHECK(eval_runtime_expr("1 + 2 * 3") == polonio::Value(7));
    CHECK(eval_runtime_expr("\"a\" .. \"b\"") == polonio::Value("ab"));
    CHECK(eval_runtime_expr("not true") == polonio::Value(false));
    CHECK(eval_runtime_expr("1 == 1") == polonio::Value(true));
    CHECK(eval_runtime_expr("[1, 2] == [1, 2]") == polonio::Value(true));
    CHECK(eval_runtime_expr("true and false") == polonio::Value(false));
    CHECK(eval_runtime_expr("true or false") == polonio::Value(true));
}

TEST_CASE("Interpreter executes statements and produces output") {
    CHECK(run_program_output("var x = 1; echo x; x += 2; echo x") == "13");
    CHECK(run_program_output("var x; echo x") == "");
}

TEST_CASE("Interpreter reports runtime errors") {
    {
        bool threw = false;
        try {
            run_program_output("echo y");
        } catch (const polonio::PolonioError& err) {
            threw = true;
            CHECK(err.kind() == polonio::ErrorKind::Runtime);
        }
        CHECK(threw);
    }

    {
        bool threw = false;
        try {
            run_program_output("echo 1 + \"a\"");
        } catch (const polonio::PolonioError& err) {
            threw = true;
            CHECK(err.kind() == polonio::ErrorKind::Runtime);
        }
        CHECK(threw);
    }

    CHECK_THROWS_AS(run_program_output("var arr = [1]; arr[0] = 2"), polonio::PolonioError);
}

TEST_CASE("Interpreter executes functions with returns") {
    const char* src = R"(
function add(a, b)
  return a + b
end
echo add(10, 20)
)";
    CHECK(run_program_output(src) == "30");
}

TEST_CASE("Interpreter treats missing arguments as null") {
    const char* src = R"(
function f(a, b)
  if b == null
    return 99
  end
  return b
end
echo f(1)
)";
    CHECK(run_program_output(src) == "99");
}

TEST_CASE("Interpreter supports recursive calls") {
    const char* src = R"(
function fact(n)
  if n <= 1
    return 1
  end
  return n * fact(n - 1)
end
echo fact(5)
)";
    CHECK(run_program_output(src) == "120");
}

TEST_CASE("Interpreter supports closures that capture variables") {
    const char* src = R"(
function make_adder(x)
  function add(y)
    return x + y
  end
  return add
end
var inc = make_adder(1)
echo inc(41)
)";
    CHECK(run_program_output(src) == "42");
}

TEST_CASE("Interpreter handles return without value") {
    const char* src = R"(
function f()
  return
end
var x = f()
echo x
)";
    CHECK(run_program_output(src) == "");
}

TEST_CASE("Interpreter errors when calling a non-function") {
    const char* src = R"(
var x = 1
echo x(1)
)";
    bool threw = false;
    try {
        run_program_output(src);
    } catch (const polonio::PolonioError& err) {
        threw = true;
        CHECK(err.kind() == polonio::ErrorKind::Runtime);
        CHECK(err.message().find("non-function") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("Builtin type returns correct strings") {
    const char* src = R"(
echo type(null)
echo type(true)
echo type(1)
echo type("a")
echo type([1])
echo type({"a": 1})
)";
    CHECK(run_program_output(src) == "nullboolnumberstringarrayobject");
}

TEST_CASE("Builtin tostring mirrors echo formatting") {
    const char* src = R"(
echo tostring(null)
echo tostring(true)
echo tostring(3)
echo tostring("x")
)";
    CHECK(run_program_output(src) == "true3x");
}

TEST_CASE("Builtins to_string and to_number convert values") {
    CHECK(run_program_output("echo to_string(1)") == "1");
    CHECK(run_program_output("echo to_string(true)") == "true");
    CHECK(run_program_output("echo to_number(\"42\")") == "42");
    CHECK(run_program_output("echo to_number(\" 5 \")") == "5");
    CHECK(run_program_output("echo to_number(true)") == "1");
    CHECK(run_program_output("echo to_number(false)") == "0");
    CHECK(run_program_output("echo to_number(null)") == "0");
    CHECK(run_program_output("echo to_number(\"3.14\")") == "3.14");
    CHECK_THROWS_AS(run_program_output("echo to_number(\"abc\")"), polonio::PolonioError);
}

TEST_CASE("Builtin nl2br handles newlines") {
    CHECK(run_program_output("echo nl2br(\"a\\nb\")") == "a<br>\nb");
    CHECK(run_program_output("echo nl2br(\"a\\r\\nb\")") == "a<br>\nb");
}

TEST_CASE("Output builtins print and println emit text") {
    CHECK(run_program_output("print(\"a\", 1)\nprintln(\"b\")") == "a1b\n");
    CHECK(run_program_output("println()\nprint(\"x\")") == "\nx");
}

TEST_CASE("Builtins enforce argument counts") {
    {
        bool threw = false;
        try {
            run_program_output("echo type()");
        } catch (const polonio::PolonioError& err) {
            threw = true;
            CHECK(err.kind() == polonio::ErrorKind::Runtime);
            CHECK(err.message().find("type") != std::string::npos);
        }
        CHECK(threw);
    }
    {
        bool threw = false;
        try {
            run_program_output("echo nl2br()");
        } catch (const polonio::PolonioError& err) {
            threw = true;
            CHECK(err.kind() == polonio::ErrorKind::Runtime);
            CHECK(err.message().find("nl2br") != std::string::npos);
        }
        CHECK(threw);
    }
}

TEST_CASE("String builtins: len lower upper") {
    CHECK(run_program_output("echo len(\"abc\")") == "3");
    CHECK(run_program_output("echo lower(\"AbC\")") == "abc");
    CHECK(run_program_output("echo upper(\"AbC\")") == "ABC");
}

TEST_CASE("String builtins: trim and replace") {
    CHECK(run_program_output("echo trim(\"  hi \\n\")") == "hi");
    CHECK(run_program_output("echo replace(\"a-b-a\", \"a\", \"x\")") == "x-b-x");
    CHECK(run_program_output("echo replace(\"aaaa\", \"aa\", \"b\")") == "bb");
    CHECK(run_program_output("echo replace(\"abc\", \"\", \"x\")") == "abc");
}

TEST_CASE("String builtins: split") {
    const char* program = R"(
var xs = split("a,b,c", ",")
for x in xs
  echo x
end
)";
    CHECK(run_program_output(program) == "abc");
    CHECK(run_program_output("var ys = split(\"abc\", \",\")\nfor y in ys echo y end") == "abc");
}

TEST_CASE("String builtins: contains/starts_with/ends_with") {
    CHECK(run_program_output("echo contains(\"hello\", \"ell\")") == "true");
    CHECK(run_program_output("echo starts_with(\"hello\", \"he\")") == "true");
    CHECK(run_program_output("echo ends_with(\"hello\", \"lo\")") == "true");
    CHECK(run_program_output("echo ends_with(\"hello\", \"xx\")") == "false");
}

TEST_CASE("String builtins: substr handles offsets and lengths") {
    CHECK(run_program_output("echo substr(\"hello\", 1)") == "ello");
    CHECK(run_program_output("echo substr(\"hello\", 1, 3)") == "ell");
    CHECK(run_program_output("echo substr(\"hello\", -2)") == "lo");
    CHECK(run_program_output("echo substr(\"hello\", -10, 2)") == "he");
    CHECK(run_program_output("echo substr(\"hello\", 10)") == "");
}

TEST_CASE("String builtin substr validates arity and types") {
    CHECK_THROWS_AS(run_program_output("echo substr(\"x\")"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo substr(\"x\", \"1\")"), polonio::PolonioError);
}

TEST_CASE("Array builtins: count push pop join range") {
    const char* program = R"(
var a = []
push(a, 1)
push(a, 2)
echo count(a)
echo pop(a)
echo count(a)
)";
    CHECK(run_program_output(program) == "221");
    CHECK(run_program_output("var b = [1, \"x\", true]\necho join(b, \",\")") == "1,x,true");
    CHECK(run_program_output("for i in range(5) echo i end") == "01234");
}

TEST_CASE("Array builtins: shift and unshift mutate arrays") {
    CHECK(run_program_output("var a = [1,2,3]\necho shift(a)\necho join(a, \",\")") == "12,3");
    CHECK(run_program_output("var a = []\necho shift(a)") == "");
    CHECK(run_program_output("var a = []\npush(a, 1)\npush(a, 2)\npush(a, 3)\nshift(a)\nshift(a)\nshift(a)\necho shift(a)") == "");
    CHECK(run_program_output("var a = [2,3]\necho unshift(a, 1)\necho join(a, \",\")") == "31,2,3");
    CHECK(run_program_output("var a = [2,3]\nvar b = a\nunshift(b, 1)\necho join(a, \",\")") == "1,2,3");
}

TEST_CASE("Array builtin concat returns new array without mutating inputs") {
    const char* program = R"(
var a = [1,2]
var b = [3,4]
var c = concat(a, b)
echo join(c, ",")
echo join(a, ",")
echo join(b, ",")
)";
    CHECK(run_program_output(program) == "1,2,3,41,23,4");
    CHECK(run_program_output("echo join(concat([], [1]), \",\")") == "1");
    CHECK(run_program_output("echo count(concat([], []))") == "0");
    const char* shared = R"(
var a = [1]
var b = [2]
var c = concat(a, b)
push(c, 3)
echo join(a, ",")
echo join(b, ",")
echo join(c, ",")
)";
    CHECK(run_program_output(shared) == "121,2,3");
}

TEST_CASE("Array builtin slice copies segments") {
    CHECK(run_program_output("var s = slice([1,2,3,4], 1)\nfor x in s echo x end") == "234");
    CHECK(run_program_output("var s = slice([1,2,3,4], 1, 2)\nfor x in s echo x end") == "23");
    CHECK(run_program_output("var s = slice([1,2,3,4], -2)\nfor x in s echo x end") == "34");
    CHECK(run_program_output("var s = slice([1,2,3,4], 99)\nfor x in s echo x end") == "");
}

TEST_CASE("Array builtin slice does not mutate source") {
    const char* program = R"(
var a = [1,2,3]
var b = slice(a, 1)
push(b, 9)
echo count(a)
)";
    CHECK(run_program_output(program) == "3");
}

TEST_CASE("Object builtin values returns deterministic order") {
    CHECK(run_program_output("var v = values({\"b\": 2, \"a\": 1})\nfor x in v echo x end") == "12");
}

TEST_CASE("Object builtins: keys has_key get set") {
    const char* program = R"(
var o = {"b": 2, "a": 1}
var ks = keys(o)
for k in ks echo k end
)";
    CHECK(run_program_output(program) == "ab");

    const char* prog2 = R"(
var o = {}
set(o, "a", 10)
echo has_key(o, "a")
echo get(o, "a")
echo get(o, "missing")
echo get(o, "missing", 7)
)";
    CHECK(run_program_output(prog2) == "true107");
}

TEST_CASE("Array/Object builtin errors") {
    CHECK_THROWS_AS(run_program_output("echo push(1, 2)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo keys(1)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo slice(1, 0)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo values([1,2])"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo shift(123)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo unshift([1,2,3])"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo unshift(\"x\", 1)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo concat([1], 2)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo concat([1])"), polonio::PolonioError);
}

TEST_CASE("Math builtins work for typical inputs") {
    CHECK(run_program_output("echo abs(-3)") == "3");
    CHECK(run_program_output("echo floor(3.9)") == "3");
    CHECK(run_program_output("echo ceil(3.1)") == "4");
    CHECK(run_program_output("echo round(2.5)") == "3");
    CHECK(run_program_output("echo round(-2.5)") == "-3");
    CHECK(run_program_output("echo min(2, 5)") == "2");
    CHECK(run_program_output("echo max(2, 5)") == "5");
}

TEST_CASE("Math builtins: pow and sqrt basics") {
    CHECK(run_program_output("echo pow(2, 3)") == "8");
    CHECK(run_program_output("echo pow(9, 0)") == "1");
    CHECK(run_program_output("echo sqrt(9)") == "3");
    CHECK(run_program_output("echo sqrt(0)") == "0");
}

TEST_CASE("Math builtins: rand and randint deterministic") {
    auto out = run_program_output("var a = rand()\nvar b = rand()\necho a\necho \"\\n\"\necho b");
    std::istringstream rand_stream(out);
    std::string line;
    std::vector<std::string> values;
    while (std::getline(rand_stream, line)) {
        if (!line.empty()) {
            values.push_back(line);
        }
    }
    REQUIRE(values.size() == 2);
    double first = std::stod(values[0]);
    double second = std::stod(values[1]);
    CHECK(first >= 0.0);
    CHECK(first < 1.0);
    CHECK(second >= 0.0);
    CHECK(second < 1.0);
    CHECK(values[0] == "0.662496");
    CHECK(values[1] == "0.802939");

    auto ints = run_program_output("var i = randint(1, 10)\nvar j = randint(1, 10)\nvar k = randint(1, 10)\necho i\necho j\necho k");
    CHECK(ints == "844");
}

TEST_CASE("Type predicates report correct categories") {
    CHECK(run_program_output("echo is_null(null)") == "true");
    CHECK(run_program_output("echo is_number(1)") == "true");
    CHECK(run_program_output("echo is_string(1)") == "false");
    CHECK(run_program_output("echo is_array([])") == "true");
    CHECK(run_program_output("echo is_object({})") == "true");
    CHECK(run_program_output("echo is_function(type)") == "true");
}

TEST_CASE("now builtin returns sane timestamp") {
    auto out = run_program_output("var t = now()\necho t");
    double value = std::stod(out);
    CHECK(value > 1000000000.0);
}

TEST_CASE("Math builtins error on invalid args") {
    CHECK_THROWS_AS(run_program_output("echo abs(\"x\")"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo min(1)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo pow(2)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo pow(\"2\", 3)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo sqrt(-1)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo rand(1)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo randint(5, 4)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo randint(\"a\", 3)"), polonio::PolonioError);
}

TEST_CASE("Date builtins: date_format/date_parts") {
    CHECK(run_program_output("var p = date_parts(0)\necho get(p, \"year\")\necho \"-\"\necho get(p, \"month\")\necho \"-\"\necho get(p, \"day\")") == "1970-1-1");
    CHECK(run_program_output("echo date_format(0, \"YYYY-MM-DD\")") == "1970-01-01");
    CHECK(run_program_output("echo date_format(0, \"YYYY-MM-DD HH:mm:SS\")") == "1970-01-01 00:00:00");
    auto out = run_program_output("var t = now()\necho t");
    double value = std::stod(out);
    CHECK(value > 1000000000.0);
}

TEST_CASE("Date builtin date_add_days shifts epoch by days") {
    CHECK(run_program_output("echo date_add_days(0, 1)") == "86400");
    CHECK(run_program_output("echo date_add_days(86400, -1)") == "0");
    CHECK(run_program_output("echo date_add_days(0, 0.5)") == "43200");
}

TEST_CASE("Date builtin date_parse handles supported formats") {
    CHECK(run_program_output("echo date_parse(\"1970-01-01\")") == "0");
    CHECK(run_program_output("echo date_parse(\"1970-01-02\")") == "86400");
    CHECK(run_program_output("echo date_parse(\"1970-01-01 00:00:01\")") == "1");
    CHECK(run_program_output("echo date_parse(\"1970-01-01 00:01:00\")") == "60");
    CHECK(run_program_output("echo date_parse(\"1970-01-01T00:00:01\")") == "1");
}

TEST_CASE("Date builtins validate arguments") {
    CHECK_THROWS_AS(run_program_output("echo date_format(\"x\", \"YYYY\")"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo date_parts()"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo date_add_days(0)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo date_add_days(\"0\", 1)"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo date_parse(\"1970-13-01\")"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo date_parse(\"nope\")"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo date_parse()"), polonio::PolonioError);
}

TEST_CASE("Interpreter executes while loops") {
    const char* src = R"(
var i = 0
while i < 5
  echo i
  i += 1
end
)";
    CHECK(run_program_output(src) == "01234");
}

TEST_CASE("Interpreter enforces while loop limit") {
    const char* src = R"(
while true
  echo 1
end
)";
    bool threw = false;
    try {
        run_program_output(src);
    } catch (const polonio::PolonioError& err) {
        threw = true;
        CHECK(err.kind() == polonio::ErrorKind::Runtime);
        CHECK(err.message().find("loop limit") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("Interpreter executes for loops over arrays") {
    const char* src = R"(
var items = [1, 2, 3]
for item in items
  echo item
end
)";
    CHECK(run_program_output(src) == "123");
}

TEST_CASE("Interpreter executes indexed for loops over arrays") {
    const char* src = R"(
var items = [10, 20]
for i, x in items
  echo i
  echo x
end
)";
    CHECK(run_program_output(src) == "010120");
}

TEST_CASE("Interpreter executes for loops over objects deterministically") {
    const char* src = R"(
var o = {"b": 2, "a": 1}
for k, v in o
  echo k
  echo v
end
)";
    CHECK(run_program_output(src) == "a1b2");
}

TEST_CASE("For loop variables do not leak outside loop") {
    const char* src = R"(
var items = [1]
for x in items
  echo x
end
echo x
)";
    bool threw = false;
    try {
        run_program_output(src);
    } catch (const polonio::PolonioError& err) {
        threw = true;
        CHECK(err.kind() == polonio::ErrorKind::Runtime);
        CHECK(err.message().find("undefined variable") != std::string::npos);
    }
    CHECK(threw);
}
