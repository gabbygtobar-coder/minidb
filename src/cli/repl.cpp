#include "minidb/repl.hpp"

#include "minidb/version.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace minidb {
namespace {

constexpr std::string_view kWhitespace = " \t\r\n\v\f";
constexpr std::string_view kNotImplemented =
    "SQL engine is not implemented yet (M1+).";

std::string trim_copy(std::string_view text) {
    const auto begin = text.find_first_not_of(kWhitespace);
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(kWhitespace);
    return std::string(text.substr(begin, end - begin + 1));
}

}  // namespace

Repl::Repl(std::istream& in, std::ostream& out, ReplOptions options)
    : in_(in), out_(out), options_(std::move(options)) {
    if (options_.database.empty()) {
        options_.database = "local";
    }
}

void Repl::print_banner() {
    out_ << "MiniDB v" << kVersion << '\n'
         << "Database: " << options_.database << '\n'
         << std::flush;
}

void Repl::print_help() {
    out_ << "MiniDB meta-commands:\n"
         << "  .help          Show this message\n"
         << "  .exit          Exit the shell\n"
         << "  .quit          Exit the shell\n"
         << "\n"
         << "No SQL is available in M0. Other input is rejected until M1.\n"
         << std::flush;
}

int Repl::run() {
    print_banner();

    std::string line;
    while (true) {
        out_ << "MiniDB> " << std::flush;
        if (!std::getline(in_, line)) {
            // Keep the caller's shell on a fresh line after Ctrl-D.
            out_ << '\n' << std::flush;
            return 0;
        }

        const std::string command = trim_copy(line);
        if (command.empty()) {
            continue;
        }
        if (command == ".exit" || command == ".quit") {
            return 0;
        }
        if (command == ".help") {
            print_help();
            continue;
        }

        out_ << kNotImplemented << '\n' << std::flush;
    }
}

}  // namespace minidb
