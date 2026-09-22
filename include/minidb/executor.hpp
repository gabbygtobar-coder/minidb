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
// The executor does not lex or parse. INSERT, SELECT, UPDATE, DELETE, CREATE,
// and DROP go through Database, which stores rows in heap pages. Invalid
// requests throw ExecutionError instead of mutating state:
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
//
// A WHERE comparison other than != uses an index when one exists on that
// column. != and a missing index scan the heap. Index scans return rows in
// index order. Heap scans stay in insertion order.
StatementResult execute(Database& database, const Statement& statement);

// Plan text for `.explain`. Does not modify the database and does not estimate
// a cost or a row count.
//
// SELECT, and UPDATE or DELETE that read rows, report one of:
//   Index Scan using <name> on <table>
//     Index Cond: <column> <op> <literal>
//   Seq Scan on <table>
//     Filter: <column> <op> <literal>
// The filter or index condition line is omitted when there is no WHERE.
// UPDATE and DELETE append a note that the write itself is not described.
// DELETE without WHERE, and CREATE, DROP, and INSERT, report `No scan` plus
// why that statement does not walk rows.
std::string explain_statement(const Database& database, const Statement& statement);

// Aligned text table plus a "(N row)" / "(N rows)" footer. No trailing newline.
std::string format_result_set(const ResultSet& result);

}  // namespace minidb
