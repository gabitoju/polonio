#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_INCLUDE_CISO646
#define DOCTEST_CONFIG_USE_STD_HEADERS
#include "third_party/doctest/doctest.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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
    const std::string stdout_path = create_temp_file("polonio_cli_stdout");
    const std::string stderr_path = create_temp_file("polonio_cli_stderr");

    std::ostringstream cmd;
    cmd << "\"" << binary << "\"";
    for (const auto& arg : args) {
        cmd << " " << arg;
    }
    cmd << " > \"" << stdout_path << "\" 2> \"" << stderr_path << "\"";

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

TEST_CASE("CLI: unknown command errors") {
    auto result = run_polonio({"does-not-exist"});
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_output.find("Unknown command") != std::string::npos);
    CHECK(result.stderr_output.find("Usage:") != std::string::npos);
}
