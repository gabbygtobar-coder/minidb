#include "minidb/repl.hpp"
#include "minidb/storage_error.hpp"

#include <cstring>
#include <iostream>

namespace {

void print_usage(std::ostream& out) {
    out << "Usage: minidb [database_file]\n"
        << "\n"
        << "Starts the MiniDB shell. Tables and rows are stored in database_file\n"
        << "and survive exit. The default file is minidb.db in the current\n"
        << "directory.\n"
        << "\n"
        << "Each successful statement is flushed with fflush. MiniDB does not\n"
        << "fsync, and it has no write-ahead log, so a crash can tear a page or\n"
        << "lose writes the operating system has not flushed to disk.\n"
        << "\n"
        << "Meta-commands: .help, .tables, .schema <table>, .indexes, .explain <sql>,\n"
        << ".exit, .quit. Command names are case-insensitive.\n";
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
    options.path = "minidb.db";
    if (argc == 2 && argv[1][0] != '\0') {
        options.path = argv[1];
    }

    try {
        minidb::Repl repl(std::cin, std::cout, std::move(options));
        return repl.run();
    } catch (const minidb::StorageError& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
