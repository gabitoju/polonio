#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr const char* kVersion = "0.1.0";

void print_usage(std::ostream& os) {
    os << "Usage: polonio <command>\n"
          "\n"
          "Commands:\n"
          "  polonio help                Show this help message\n"
          "  polonio version             Show version information\n"
          "  polonio run <file.pol>      Run a Polonio template (coming soon)\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }

    std::string command = argv[1];
    if (command == "help") {
        print_usage(std::cout);
        return EXIT_SUCCESS;
    }

    if (command == "version") {
        std::cout << kVersion << '\n';
        return EXIT_SUCCESS;
    }

    std::cerr << "Unknown command: " << command << '\n';
    print_usage(std::cerr);
    return EXIT_FAILURE;
}
