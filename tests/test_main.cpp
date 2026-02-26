#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_INCLUDE_CISO646
#define DOCTEST_CONFIG_USE_STD_HEADERS
#include "third_party/doctest/doctest.h"

#include "polonio/common/source.h"
#include "polonio/common/location.h"
#include "polonio/common/error.h"
#include "polonio/lexer/lexer.h"

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
