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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
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

std::string create_temp_file_with_content(const std::string& prefix, const std::string& content) {
    std::string path = create_temp_file(prefix);
    std::ofstream stream(path, std::ios::binary);
    stream << content;
    stream.close();
    return path;
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

TEST_CASE("Builtin nl2br handles newlines") {
    CHECK(run_program_output("echo nl2br(\"a\\nb\")") == "a<br>\nb");
    CHECK(run_program_output("echo nl2br(\"a\\r\\nb\")") == "a<br>\nb");
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
}

TEST_CASE("Date builtins: date_format/date_parts") {
    CHECK(run_program_output("var p = date_parts(0)\necho get(p, \"year\")\necho \"-\"\necho get(p, \"month\")\necho \"-\"\necho get(p, \"day\")") == "1970-1-1");
    CHECK(run_program_output("echo date_format(0, \"YYYY-MM-DD\")") == "1970-01-01");
    CHECK(run_program_output("echo date_format(0, \"YYYY-MM-DD HH:mm:SS\")") == "1970-01-01 00:00:00");
    auto out = run_program_output("var t = now()\necho t");
    double value = std::stod(out);
    CHECK(value > 1000000000.0);
}

TEST_CASE("Date builtins validate arguments") {
    CHECK_THROWS_AS(run_program_output("echo date_format(\"x\", \"YYYY\")"), polonio::PolonioError);
    CHECK_THROWS_AS(run_program_output("echo date_parts()"), polonio::PolonioError);
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
