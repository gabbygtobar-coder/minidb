#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace minidb {

enum class DataType {
    Int,
    Text,
    Boolean,
    Float,
};

struct ColumnDefinition {
    std::string name;
    DataType type = DataType::Int;
};

enum class ComparisonOp {
    Equal,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
};

enum class LiteralKind {
    Integer,
    Float,
    Boolean,
    String,
};

// A literal from the source. `text` is the spelling used when printing the AST:
// the original digits for numbers, TRUE/FALSE for booleans, and the decoded
// characters for strings. Numeric and boolean members hold the typed value.
struct Literal {
    LiteralKind kind = LiteralKind::Integer;
    std::string text;
    std::int64_t integer = 0;
    double floating = 0.0;
    bool boolean = false;
};

struct WhereClause {
    std::string column;
    ComparisonOp op = ComparisonOp::Equal;
    Literal value;
};

struct CreateTableStatement {
    std::string name;
    std::vector<ColumnDefinition> columns;
};

struct DropTableStatement {
    std::string name;
};

struct CreateIndexStatement {
    std::string name;
    std::string table;
    std::string column;
};

struct DropIndexStatement {
    std::string name;
};

struct InsertStatement {
    std::string table;
    std::vector<Literal> values;
};

struct SelectStatement {
    bool select_all = false;
    std::vector<std::string> columns;
    std::string table;
    std::optional<WhereClause> where;
};

struct UpdateStatement {
    std::string table;
    std::string column;
    Literal value;
    std::optional<WhereClause> where;
};

struct DeleteStatement {
    std::string table;
    std::optional<WhereClause> where;
};

using Statement = std::variant<CreateTableStatement,
                               DropTableStatement,
                               CreateIndexStatement,
                               DropIndexStatement,
                               InsertStatement,
                               SelectStatement,
                               UpdateStatement,
                               DeleteStatement>;

// One-line summary of a statement. This is a printer, not an executor.
std::string format_statement(const Statement& statement);

}  // namespace minidb
