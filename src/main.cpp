#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "polonio/common/error.h"
#include "polonio/common/source.h"
#include "polonio/lexer/lexer.h"
#include "polonio/parser/parser.h"
#include "polonio/runtime/interpreter.h"
#include "polonio/runtime/env.h"
#include "polonio/runtime/template_renderer.h"
#include "polonio/runtime/cgi.h"
#include "polonio/server/http_server.h"

namespace {

constexpr const char* kVersion = "0.1.0";
void print_usage(std::ostream& os) {
    os << "Usage: polonio <command|file>\n"
          "\n"
          "Commands:\n"
          "  polonio help                Show this help message\n"
          "  polonio version             Show version information\n"
          "  polonio --dump-ast <expr>   Dump AST for expression (dev)\n"
          "  polonio run <file.pol>      Run a Polonio template\n"
          "  polonio <file.pol>          Shorthand for run\n"
          "  polonio serve ...           Development server (coming soon)\n";
}

int handle_run(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "run: missing file argument\n";
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }

    if (args.size() > 1) {
        std::cerr << "run: too many arguments\n";
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }
    const std::string& path = args[0];
    polonio::Source source = polonio::Source::from_file(path);

    if (source.content().find("<%") != std::string::npos) {
        std::cout << polonio::render_template(source);
        return EXIT_SUCCESS;
    }

    polonio::Lexer lexer(source.content(), source.path());
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens, source.path());
    auto program = parser.parse_program();

    polonio::Interpreter interpreter(std::make_shared<polonio::Env>(), source.path());
    interpreter.exec_program(program);
    std::cout << interpreter.output();
    return EXIT_SUCCESS;
}

int handle_dump_ast(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "--dump-ast requires an expression argument\n";
        return EXIT_FAILURE;
    }
    std::string expression;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            expression += ' ';
        }
        expression += args[i];
    }

    polonio::Lexer lexer(expression, "<expr>");
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens, "<expr>");
    auto expr = parser.parse_expression();
    std::cout << expr->dump() << '\n';
    return EXIT_SUCCESS;
}

bool is_flag(const std::string& arg) {
    return !arg.empty() && arg[0] == '-';
}

bool is_known_command(const std::string& arg) {
    return arg == "help" || arg == "version" || arg == "run" || arg == "serve";
}

int handle_serve(const std::vector<std::string>& args) {
    int port = 8080;
    std::filesystem::path root = ".";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--port") {
            if (i + 1 >= args.size()) {
                std::cerr << "serve: --port requires a value\n";
                return EXIT_FAILURE;
            }
            const std::string& value = args[++i];
            try {
                port = std::stoi(value);
            } catch (const std::exception&) {
                std::cerr << "serve: invalid port: " << value << '\n';
                return EXIT_FAILURE;
            }
        } else if (arg == "--root") {
            if (i + 1 >= args.size()) {
                std::cerr << "serve: --root requires a directory path\n";
                return EXIT_FAILURE;
            }
            root = args[++i];
        } else {
            std::cerr << "serve: unknown option " << arg << '\n';
            print_usage(std::cerr);
            return EXIT_FAILURE;
        }
    }

    if (port < 1 || port > 65535) {
        std::cerr << "serve: port must be between 1 and 65535\n";
        return EXIT_FAILURE;
    }

    std::error_code ec;
    auto root_exists = std::filesystem::exists(root, ec);
    if (ec || !root_exists) {
        std::cerr << "serve: root directory not found: " << root << '\n';
        return EXIT_FAILURE;
    }
    auto is_dir = std::filesystem::is_directory(root, ec);
    if (ec || !is_dir) {
        std::cerr << "serve: root path is not a directory: " << root << '\n';
        return EXIT_FAILURE;
    }
    auto normalized = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        normalized = std::filesystem::absolute(root);
    }

    polonio::ServerConfig config;
    config.port = port;
    config.root = normalized;
    try {
        polonio::run_http_server(config);
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "serve: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}

} // namespace

int handle_cgi_request() {
    try {
        auto ctx = polonio::build_cgi_context();
        polonio::Source source = polonio::Source::from_file(ctx.script_filename);
        polonio::Interpreter interpreter(std::make_shared<polonio::Env>(), ctx.script_filename);
        polonio::ResponseContext response;
        interpreter.set_response_context(&response);
        auto env = interpreter.env();
        env->set_local("_GET", polonio::Value(ctx.get));
        env->set_local("_POST", polonio::Value(ctx.post));
        env->set_local("_COOKIE", polonio::Value(ctx.cookie));
        env->set_local("_SERVER", polonio::Value(ctx.server));
        interpreter.set_cgi_context(&ctx);
        auto body = polonio::render_template_with_interpreter(source, interpreter);
        response.emit(std::cout);
        std::cout << body;
        return EXIT_SUCCESS;
    } catch (const polonio::PolonioError& err) {
        std::cout << "Status: 500\r\nContent-Type: text/plain\r\n\r\n" << err.format();
        return EXIT_FAILURE;
    }
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            if (polonio::is_cgi_mode()) {
                return handle_cgi_request();
            }
            print_usage(std::cerr);
            return EXIT_FAILURE;
        }

        std::vector<std::string> args(argv + 1, argv + argc);
        const std::string& command = args[0];
        if (command == "help") {
            print_usage(std::cout);
            return EXIT_SUCCESS;
        }

        if (command == "version") {
            std::cout << kVersion << '\n';
            return EXIT_SUCCESS;
        }

        if (command == "run") {
            std::vector<std::string> run_args(args.begin() + 1, args.end());
            return handle_run(run_args);
        }

        if (command == "--dump-ast") {
            std::vector<std::string> dump_args(args.begin() + 1, args.end());
            return handle_dump_ast(dump_args);
        }

        if (command == "serve") {
            std::vector<std::string> serve_args(args.begin() + 1, args.end());
            return handle_serve(serve_args);
        }

        if (!is_flag(command) && !is_known_command(command)) {
            std::vector<std::string> run_args(args.begin(), args.end());
            return handle_run(run_args);
        }

        std::cerr << "Unknown command: " << command << '\n';
        print_usage(std::cerr);
        return EXIT_FAILURE;
    } catch (const polonio::PolonioError& err) {
        std::cerr << err.format() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
