#include "minidb/repl.hpp"

#include <cstring>
#include <iostream>

namespace {

void print_usage(std::ostream& out) {
    out << "Usage: minidb [data_dir]\n"
        << "\n"
        << "Starts the MiniDB shell. The optional data_dir is shown in the\n"
        << "banner and is not created or read.\n"
        << "\n"
        << "Meta-commands inside the shell: .help, .exit, .quit\n"
        << "SQL statements are parsed and printed as an AST. They are not executed.\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 &&
        (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)) {
        print_usage(std::cout);
        return 0;
    }
    if (argc > 2) {
        print_usage(std::cerr);
        return 1;
    }

    minidb::ReplOptions options;
    if (argc == 2 && argv[1][0] != '\0') {
        options.database = argv[1];
    }

    minidb::Repl repl(std::cin, std::cout, std::move(options));
    return repl.run();
}
