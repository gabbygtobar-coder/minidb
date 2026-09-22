#include "minidb/repl.hpp"

#include "minidb/execution_error.hpp"
#include "minidb/executor.hpp"
#include "minidb/parser.hpp"
#include "minidb/version.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace minidb {
namespace {

constexpr std::string_view kWhitespace = " \t\r\n\v\f";

std::string trim_copy(std::string_view text) {
    const auto begin = text.find_first_not_of(kWhitespace);
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(kWhitespace);
    return std::string(text.substr(begin, end - begin + 1));
}

bool is_identifier(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    const auto is_start = [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    const auto is_continue = [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
               (c >= '0' && c <= '9');
    };
    if (!is_start(static_cast<unsigned char>(text.front()))) {
        return false;
    }
    for (const char ch : text) {
        if (!is_continue(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

// True when `command` is `name` or `name` followed by whitespace.
bool is_meta(const std::string& command, std::string_view name) {
    if (command.size() < name.size() || command.compare(0, name.size(), name) != 0) {
        return false;
    }
    return command.size() == name.size() ||
           kWhitespace.find(command[name.size()]) != std::string_view::npos;
}

void print_tables(std::ostream& out, const Database& database) {
    const std::vector<std::string> names = database.table_names();
    if (names.empty()) {
        out << "(no tables)\n" << std::flush;
        return;
    }
    for (const std::string& name : names) {
        out << name << '\n';
    }
    out << std::flush;
}

void print_outcome(std::ostream& out, const StatementResult& outcome) {
    if (outcome.result.has_value()) {
        out << format_result_set(*outcome.result) << '\n' << std::flush;
        return;
    }
    out << outcome.message << '\n' << std::flush;
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
         << "  .help            Show this message\n"
         << "  .tables          List tables in memory\n"
         << "  .schema <table>  Show one table's columns\n"
         << "  .exit            Exit the shell\n"
         << "  .quit            Exit the shell\n"
         << "\n"
         << "SQL runs against an in-memory database. Data is lost when the shell exits.\n"
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
        if (command == ".tables") {
            print_tables(out_, database_);
            continue;
        }
        if (is_meta(command, ".schema")) {
            const std::string argument =
                trim_copy(std::string_view(command).substr(std::string(".schema").size()));
            if (!is_identifier(argument)) {
                out_ << "Usage: .schema <table>\n" << std::flush;
                continue;
            }
            try {
                out_ << format_schema(database_.require_table(argument)) << '\n' << std::flush;
            } catch (const ExecutionError& error) {
                out_ << "Error: " << error.what() << '\n' << std::flush;
            }
            continue;
        }
        if (command.front() == '.') {
            out_ << "Unknown meta-command: " << command << '\n' << std::flush;
            continue;
        }

        try {
            const Statement statement = parse_statement(command);
            print_outcome(out_, execute(database_, statement));
        } catch (const ParseError& error) {
            out_ << "Parse error at " << error.line() << ':' << error.column() << ": "
                 << error.what() << '\n'
                 << std::flush;
        } catch (const ExecutionError& error) {
            out_ << "Error: " << error.what() << '\n' << std::flush;
        }
    }
}

}  // namespace minidb
