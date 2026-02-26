#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_INCLUDE_CISO646
#define DOCTEST_CONFIG_USE_STD_HEADERS
#include "third_party/doctest/doctest.h"

#include "polonio/common/source.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

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

CommandResult run_polonio(const std::vector<std::string>& args) {
    auto binary = (std::filesystem::current_path() / "build/polonio").string();
    REQUIRE(std::filesystem::exists(binary));

    const std::string stdout_path = create_temp_file("polonio_stdout");
    const std::string stderr_path = create_temp_file("polonio_stderr");

    std::ostringstream cmd;
    cmd << shell_quote(binary);
    for (const auto& arg : args) {
        cmd << " " << shell_quote(arg);
    }
    cmd << " > " << shell_quote(stdout_path) << " 2> " << shell_quote(stderr_path);

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
    return result;
}

} // namespace

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

TEST_CASE("CLI: run command stub message") {
    auto result = run_polonio({"run", "hello.pol"});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("not implemented") != std::string::npos);
}

TEST_CASE("CLI: shorthand file invocation behaves like run") {
    auto result = run_polonio({"hello.pol"});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("not implemented") != std::string::npos);
}

TEST_CASE("CLI: shorthand treats unknown words as file path") {
    auto result = run_polonio({"does-not-exist"});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("not implemented") != std::string::npos);
}

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

namespace {

std::string create_temp_file_with_content(const std::string& prefix, const std::string& content) {
    std::string path = create_temp_file(prefix);
    std::ofstream stream(path, std::ios::binary);
    stream << content;
    stream.close();
    return path;
}

} // namespace

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
    CHECK_THROWS_WITH_AS(polonio::Source::from_file(missing), doctest::Contains(missing.c_str()), std::runtime_error);
}
