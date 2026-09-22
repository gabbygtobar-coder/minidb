#pragma once

#include "minidb/ast.hpp"
#include "minidb/catalog.hpp"

#include <optional>
#include <string>
#include <vector>

namespace minidb {

struct ResultSet {
    std::vector<std::string> column_names;
    std::vector<std::vector<std::string>> rows;
};

// `message` is set for CREATE, DROP, INSERT, UPDATE, and DELETE.
// `result` is set for SELECT.
struct StatementResult {
    std::string message;
    std::optional<ResultSet> result;
};

// Runs one parsed statement against `database`.
//
// The executor does not lex or parse, and it does not read or write files.
// Invalid requests throw ExecutionError instead of mutating state:
//   - unknown table or column
//   - duplicate table or column on CREATE
//   - INSERT arity that does not match the schema
//   - a literal whose kind does not match the column type
//
// WHERE comparisons (also used by UPDATE and DELETE):
//   - INT accepts only integer literals; FLOAT only float literals;
//     TEXT only strings; BOOLEAN only TRUE/FALSE. No cross-type coercion.
//   - INT and FLOAT allow =, !=, <, >, <=, and >=. FLOAT uses the stored
//     IEEE value with no tolerance.
//   - TEXT and BOOLEAN allow only = and !=. TEXT equality is byte-wise and
//     case-sensitive. Ordering either type is an execution error.
StatementResult execute(Database& database, const Statement& statement);

// Aligned text table plus a "(N row)" / "(N rows)" footer. No trailing newline.
std::string format_result_set(const ResultSet& result);

}  // namespace minidb
