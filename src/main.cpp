#include <cstdlib>
#include <exception>
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

namespace {

constexpr const char* kVersion = "0.1.0";
void print_usage(std::ostream& os) {
    os << "Usage: polonio <command|file>\n"
          "\n"
          "Commands:\n"
          "  polonio help                Show this help message\n"
          "  polonio version             Show version information\n"
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
    polonio::Lexer lexer(source.content(), source.path());
    auto tokens = lexer.scan_all();
    polonio::Parser parser(tokens, source.path());
    auto program = parser.parse_program();

    polonio::Interpreter interpreter(std::make_shared<polonio::Env>(), source.path());
    interpreter.exec_program(program);
    std::cout << interpreter.output();
    return EXIT_SUCCESS;
}

bool is_flag(const std::string& arg) {
    return !arg.empty() && arg[0] == '-';
}

bool is_known_command(const std::string& arg) {
    return arg == "help" || arg == "version" || arg == "run" || arg == "serve";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
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

        if (command == "serve") {
            std::cerr << "serve: not implemented yet\n";
            return EXIT_FAILURE;
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
