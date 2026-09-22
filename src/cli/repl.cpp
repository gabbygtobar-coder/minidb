#include "minidb/repl.hpp"

#include "minidb/execution_error.hpp"
#include "minidb/executor.hpp"
#include "minidb/parser.hpp"
#include "minidb/storage_error.hpp"
#include "minidb/version.hpp"

#include <cctype>
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

// The command word, lowercased, up to the first whitespace.
// `.EXPLAIN` and `.explain` are the same command. The rest of the line is not
// folded: table names and SQL stay case-sensitive.
std::string meta_head(const std::string& command) {
    std::string head;
    head.reserve(command.size());
    for (const char ch : command) {
        if (kWhitespace.find(ch) != std::string_view::npos) {
            break;
        }
        head.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return head;
}

void print_indexes(std::ostream& out, const Database& database) {
    const std::vector<IndexInfo> indexes = database.list_indexes();
    if (indexes.empty()) {
        out << "(no indexes)\n" << std::flush;
        return;
    }
    for (const IndexInfo& index : indexes) {
        out << index.name << " ON " << index.table << " (" << index.column << ")\n";
    }
    out << std::flush;
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
    : in_(in),
      out_(out),
      options_(std::move(options)),
      database_(options_.path.empty() ? Database() : Database(options_.path)) {}

void Repl::print_banner() {
    const std::string label = options_.path.empty() ? std::string("memory") : options_.path;
    out_ << "MiniDB v" << kVersion << '\n'
         << "Database: " << label << '\n'
         << std::flush;
}

void Repl::print_help() {
    out_ << "MiniDB meta-commands:\n"
         << "  .help              Show this message\n"
         << "  .tables            List tables\n"
         << "  .schema <table>    Show one table's columns\n"
         << "  .indexes           List indexes\n"
         << "  .explain <sql>     Show Seq Scan or Index Scan without running it\n"
         << "  .exit              Exit the shell\n"
         << "  .quit              Exit the shell\n"
         << "\n"
         << "Command names are case-insensitive. .EXPLAIN and .explain are the same.\n"
         << "SQL runs against the open database. A database file keeps tables, rows, and\n"
         << "indexes after the shell exits. An in-memory database is discarded when the shell exits.\n"
         << ".explain prints the scan and, when there is a WHERE clause, the filter or index\n"
         << "condition. It does not run the statement and it does not estimate a cost.\n"
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
        const std::string head = meta_head(command);
        const bool bare = command.size() == head.size();
        if (bare && (head == ".exit" || head == ".quit")) {
            return 0;
        }
        if (bare && head == ".help") {
            print_help();
            continue;
        }
        if (bare && head == ".tables") {
            print_tables(out_, database_);
            continue;
        }
        if (bare && head == ".indexes") {
            print_indexes(out_, database_);
            continue;
        }
        if (head == ".schema") {
            const std::string argument = trim_copy(std::string_view(command).substr(head.size()));
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
        if (head == ".explain") {
            const std::string sql = trim_copy(std::string_view(command).substr(head.size()));
            if (sql.empty()) {
                out_ << "Usage: .explain <sql>\n" << std::flush;
                continue;
            }
            try {
                const Statement statement = parse_statement(sql);
                out_ << explain_statement(database_, statement) << '\n' << std::flush;
            } catch (const ParseError& error) {
                out_ << "Parse error at " << error.line() << ':' << error.column() << ": "
                     << error.what() << '\n'
                     << std::flush;
            } catch (const ExecutionError& error) {
                out_ << "Error: " << error.what() << '\n' << std::flush;
            } catch (const StorageError& error) {
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
        } catch (const StorageError& error) {
            out_ << "Error: " << error.what() << '\n' << std::flush;
        }
    }
}

}  // namespace minidb
