#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr const char* kVersion = "0.1.0";
constexpr const char* kRunStubMessage = "run: not implemented\n";

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

    std::cerr << kRunStubMessage;
    return EXIT_FAILURE;
}

bool is_flag(const std::string& arg) {
    return !arg.empty() && arg[0] == '-';
}

bool is_known_command(const std::string& arg) {
    return arg == "help" || arg == "version" || arg == "run" || arg == "serve";
}

} // namespace

int main(int argc, char** argv) {
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
}
