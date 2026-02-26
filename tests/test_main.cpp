#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_INCLUDE_CISO646
#define DOCTEST_CONFIG_USE_STD_HEADERS
#include "third_party/doctest/doctest.h"

#include "polonio/common/source.h"
#include "polonio/common/location.h"
#include "polonio/common/error.h"
#include "polonio/lexer/lexer.h"
#include "polonio/parser/parser.h"

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
    polonio::Lexer lexer(input);
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens);
    auto program = parser.parse_program();
    return program.dump();
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
