#include "minidb/executor.hpp"

#include "minidb/execution_error.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace minidb {
namespace {

struct Predicate {
    std::size_t column = 0;
    ComparisonOp op = ComparisonOp::Equal;
    Value expected = Value::integer(0);
};

const char* comparison_symbol(ComparisonOp op) {
    switch (op) {
        case ComparisonOp::Equal:
            return "=";
        case ComparisonOp::NotEqual:
            return "!=";
        case ComparisonOp::Less:
            return "<";
        case ComparisonOp::Greater:
            return ">";
        case ComparisonOp::LessEqual:
            return "<=";
        case ComparisonOp::GreaterEqual:
            return ">=";
    }
    throw std::logic_error("unknown comparison operator");
}

std::string affected(const char* verb, std::size_t count) {
    std::string out = verb;
    out += ' ';
    out += std::to_string(count);
    out += (count == 1) ? " row." : " rows.";
    return out;
}

void ensure_operator(DataType type, ComparisonOp op) {
    if (type != DataType::Text && type != DataType::Boolean) {
        return;
    }
    if (op == ComparisonOp::Equal || op == ComparisonOp::NotEqual) {
        return;
    }
    throw ExecutionError(std::string("Operator ") + comparison_symbol(op) +
                         " is not supported for " + data_type_name(type) +
                         " columns (only = and !=)");
}

int compare_int(std::int64_t left, std::int64_t right) {
    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

int compare_float(double left, double right) {
    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

bool apply_order(int order, ComparisonOp op) {
    switch (op) {
        case ComparisonOp::Equal:
            return order == 0;
        case ComparisonOp::NotEqual:
            return order != 0;
        case ComparisonOp::Less:
            return order < 0;
        case ComparisonOp::Greater:
            return order > 0;
        case ComparisonOp::LessEqual:
            return order <= 0;
        case ComparisonOp::GreaterEqual:
            return order >= 0;
    }
    throw std::logic_error("unknown comparison operator");
}

bool compare_values(const Value& left, ComparisonOp op, const Value& right) {
    switch (left.type()) {
        case DataType::Int:
            return apply_order(compare_int(left.integer(), right.integer()), op);
        case DataType::Float:
            return apply_order(compare_float(left.floating(), right.floating()), op);
        case DataType::Text: {
            const bool equal = left.text() == right.text();
            if (op == ComparisonOp::Equal) {
                return equal;
            }
            if (op == ComparisonOp::NotEqual) {
                return !equal;
            }
            throw std::logic_error("TEXT ordering should have been rejected");
        }
        case DataType::Boolean: {
            const bool equal = left.boolean() == right.boolean();
            if (op == ComparisonOp::Equal) {
                return equal;
            }
            if (op == ComparisonOp::NotEqual) {
                return !equal;
            }
            throw std::logic_error("BOOLEAN ordering should have been rejected");
        }
    }
    throw std::logic_error("unknown data type");
}

std::size_t require_column(const Table& table, const std::string& name) {
    const std::optional<std::size_t> index = table.index_of(name);
    if (!index.has_value()) {
        throw ExecutionError("Unknown column: " + name);
    }
    return *index;
}

std::optional<Predicate> bind_where(const Table& table, const std::optional<WhereClause>& where) {
    if (!where.has_value()) {
        return std::nullopt;
    }
    const std::size_t index = require_column(table, where->column);
    const DataType type = table.columns[index].type;
    ensure_operator(type, where->op);
    Predicate predicate;
    predicate.column = index;
    predicate.op = where->op;
    predicate.expected = value_from_literal(where->value, type, where->column);
    return predicate;
}

bool matches(const std::vector<Value>& row, const Predicate& predicate) {
    return compare_values(row[predicate.column], predicate.op, predicate.expected);
}

struct ChosenScan {
    bool use_index = false;
    std::string index_name;
    Database::IndexRange range;
};

bool indexable(ComparisonOp op) {
    return op != ComparisonOp::NotEqual;
}

ChosenScan choose_scan(const Database& database, const Table& table,
                       const std::optional<Predicate>& predicate) {
    ChosenScan scan;
    if (!predicate.has_value() || !indexable(predicate->op)) {
        return scan;
    }
    const std::optional<std::string> index =
        database.index_for_column(table.name, table.columns[predicate->column].name);
    if (!index.has_value()) {
        return scan;
    }
    scan.use_index = true;
    scan.index_name = *index;
    const Value& bound = predicate->expected;
    switch (predicate->op) {
        case ComparisonOp::Equal:
            scan.range.low_unbounded = false;
            scan.range.high_unbounded = false;
            scan.range.low_inclusive = true;
            scan.range.high_inclusive = true;
            scan.range.low = bound;
            scan.range.high = bound;
            break;
        case ComparisonOp::Less:
            scan.range.high_unbounded = false;
            scan.range.high_inclusive = false;
            scan.range.high = bound;
            break;
        case ComparisonOp::LessEqual:
            scan.range.high_unbounded = false;
            scan.range.high_inclusive = true;
            scan.range.high = bound;
            break;
        case ComparisonOp::Greater:
            scan.range.low_unbounded = false;
            scan.range.low_inclusive = false;
            scan.range.low = bound;
            break;
        case ComparisonOp::GreaterEqual:
            scan.range.low_unbounded = false;
            scan.range.low_inclusive = true;
            scan.range.low = bound;
            break;
        case ComparisonOp::NotEqual:
            break;
    }
    return scan;
}

std::vector<StoredRow> read_matching(Database& database, const Table& table,
                                     const std::optional<Predicate>& predicate, const ChosenScan& scan) {
    std::vector<StoredRow> stored =
        scan.use_index ? database.scan_index(table.name, scan.index_name, scan.range)
                       : database.scan_rows(table.name);
    if (!predicate.has_value()) {
        return stored;
    }
    std::vector<StoredRow> matched;
    for (StoredRow& row : stored) {
        if (matches(row.values, *predicate)) {
            matched.push_back(std::move(row));
        }
    }
    return matched;
}

std::string quote_literal(const std::string& value) {
    std::string out;
    out.push_back('\'');
    for (const char c : value) {
        if (c == '\'') {
            out += "''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

// Source spelling, not the normalized cell format, so `.explain` shows the
// predicate the user wrote (`007`, `1.50`) rather than a rewritten literal.
std::string literal_sql(const Literal& literal) {
    switch (literal.kind) {
        case LiteralKind::Integer:
        case LiteralKind::Float:
            return literal.text;
        case LiteralKind::Boolean:
            return literal.boolean ? "TRUE" : "FALSE";
        case LiteralKind::String:
            return quote_literal(literal.text);
    }
    throw std::logic_error("unknown literal kind");
}

std::string predicate_sql(const WhereClause& where) {
    return where.column + " " + comparison_symbol(where.op) + " " + literal_sql(where.value);
}

std::string plan_text(const std::string& table, const ChosenScan& scan,
                      const std::optional<std::string>& predicate) {
    std::string out;
    if (scan.use_index) {
        out = "Index Scan using " + scan.index_name + " on " + table;
        if (predicate.has_value()) {
            out += "\n  Index Cond: ";
            out += *predicate;
        }
        return out;
    }
    out = "Seq Scan on " + table;
    if (predicate.has_value()) {
        out += "\n  Filter: ";
        out += *predicate;
    }
    return out;
}

constexpr const char* kWriteNote = "(read plan only; EXPLAIN does not describe the write)";
constexpr const char* kNoScanDml =
    "No scan\n(CREATE, DROP, and INSERT do not scan a table)";
constexpr const char* kNoScanDelete =
    "No scan\n(DELETE without WHERE clears the heap and does not walk rows)";

std::string explain_read(const Database& database, const std::string& table_name,
                         const std::optional<WhereClause>& where, bool select_only) {
    const Table& table = database.require_table(table_name);
    const std::optional<Predicate> predicate = bind_where(table, where);
    const ChosenScan scan = choose_scan(database, table, predicate);
    std::optional<std::string> pred;
    if (where.has_value()) {
        pred = predicate_sql(*where);
    }
    std::string text = plan_text(table.name, scan, pred);
    if (!select_only) {
        text += '\n';
        text += kWriteNote;
    }
    return text;
}

StatementResult execute_create(Database& database, const CreateTableStatement& statement) {
    database.create_table(statement.name, statement.columns);
    StatementResult outcome;
    outcome.message = "Created table " + statement.name + ".";
    return outcome;
}

StatementResult execute_drop(Database& database, const DropTableStatement& statement) {
    database.drop_table(statement.name);
    StatementResult outcome;
    outcome.message = "Dropped table " + statement.name + ".";
    return outcome;
}

StatementResult execute_create_index(Database& database, const CreateIndexStatement& statement) {
    database.create_index(statement.name, statement.table, statement.column);
    StatementResult outcome;
    outcome.message = "Created index " + statement.name + " on " + statement.table + "(" +
                      statement.column + ").";
    return outcome;
}

StatementResult execute_drop_index(Database& database, const DropIndexStatement& statement) {
    database.drop_index(statement.name);
    StatementResult outcome;
    outcome.message = "Dropped index " + statement.name + ".";
    return outcome;
}

StatementResult execute_insert(Database& database, const InsertStatement& statement) {
    Table& table = database.require_table(statement.table);
    if (statement.values.size() != table.columns.size()) {
        throw ExecutionError("INSERT INTO " + table.name + " expected " +
                             std::to_string(table.columns.size()) + " values, got " +
                             std::to_string(statement.values.size()));
    }

    std::vector<Value> row;
    row.reserve(table.columns.size());
    for (std::size_t i = 0; i < table.columns.size(); ++i) {
        row.push_back(value_from_literal(statement.values[i], table.columns[i].type,
                                         table.columns[i].name));
    }
    database.insert_row(table.name, std::move(row));

    StatementResult outcome;
    outcome.message = "Inserted 1 row.";
    return outcome;
}

std::vector<std::size_t> projection(const Table& table, const SelectStatement& statement) {
    if (statement.select_all) {
        std::vector<std::size_t> indexes(table.columns.size());
        for (std::size_t i = 0; i < indexes.size(); ++i) {
            indexes[i] = i;
        }
        return indexes;
    }
    if (statement.columns.empty()) {
        throw ExecutionError("SELECT requires at least one column");
    }
    std::vector<std::size_t> indexes;
    indexes.reserve(statement.columns.size());
    for (const std::string& name : statement.columns) {
        indexes.push_back(require_column(table, name));
    }
    return indexes;
}

StatementResult execute_select(Database& database, const SelectStatement& statement) {
    const Table& table = database.require_table(statement.table);
    const std::vector<std::size_t> indexes = projection(table, statement);
    const std::optional<Predicate> predicate = bind_where(table, statement.where);
    const ChosenScan scan = choose_scan(database, table, predicate);

    ResultSet result;
    result.column_names.reserve(indexes.size());
    for (const std::size_t index : indexes) {
        result.column_names.push_back(table.columns[index].name);
    }
    for (const StoredRow& stored : read_matching(database, table, predicate, scan)) {
        const std::vector<Value>& row = stored.values;
        std::vector<std::string> cells;
        cells.reserve(indexes.size());
        for (const std::size_t index : indexes) {
            cells.push_back(row[index].format());
        }
        result.rows.push_back(std::move(cells));
    }

    StatementResult outcome;
    outcome.result = std::move(result);
    return outcome;
}

StatementResult execute_update(Database& database, const UpdateStatement& statement) {
    Table& table = database.require_table(statement.table);
    const std::size_t index = require_column(table, statement.column);
    const Value value =
        value_from_literal(statement.value, table.columns[index].type, statement.column);
    const std::optional<Predicate> predicate = bind_where(table, statement.where);
    const ChosenScan scan = choose_scan(database, table, predicate);

    std::vector<StoredRow> stored_rows = read_matching(database, table, predicate, scan);
    std::size_t count = 0;
    for (StoredRow& stored : stored_rows) {
        stored.values[index] = value;
        database.update_row(table.name, stored.id, std::move(stored.values));
        ++count;
    }

    StatementResult outcome;
    outcome.message = affected("Updated", count);
    return outcome;
}

StatementResult execute_delete(Database& database, const DeleteStatement& statement) {
    Table& table = database.require_table(statement.table);
    const std::optional<Predicate> predicate = bind_where(table, statement.where);
    const ChosenScan scan = choose_scan(database, table, predicate);

    std::size_t count = 0;
    if (!predicate.has_value()) {
        count = database.row_count(table.name);
        database.clear_rows(table.name);
    } else {
        std::vector<RowId> ids;
        for (const StoredRow& stored : read_matching(database, table, predicate, scan)) {
            ids.push_back(stored.id);
        }
        for (const RowId id : ids) {
            database.delete_row(table.name, id);
        }
        count = ids.size();
    }

    StatementResult outcome;
    outcome.message = affected("Deleted", count);
    return outcome;
}

std::string pad(const std::string& text, std::size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

struct StatementExecutor {
    Database& database;

    StatementResult operator()(const CreateTableStatement& statement) const {
        return execute_create(database, statement);
    }
    StatementResult operator()(const DropTableStatement& statement) const {
        return execute_drop(database, statement);
    }
    StatementResult operator()(const CreateIndexStatement& statement) const {
        return execute_create_index(database, statement);
    }
    StatementResult operator()(const DropIndexStatement& statement) const {
        return execute_drop_index(database, statement);
    }
    StatementResult operator()(const InsertStatement& statement) const {
        return execute_insert(database, statement);
    }
    StatementResult operator()(const SelectStatement& statement) const {
        return execute_select(database, statement);
    }
    StatementResult operator()(const UpdateStatement& statement) const {
        return execute_update(database, statement);
    }
    StatementResult operator()(const DeleteStatement& statement) const {
        return execute_delete(database, statement);
    }
};

}  // namespace

StatementResult execute(Database& database, const Statement& statement) {
    return std::visit(StatementExecutor{database}, statement);
}

std::string explain_statement(const Database& database, const Statement& statement) {
    if (const auto* select = std::get_if<SelectStatement>(&statement)) {
        return explain_read(database, select->table, select->where, true);
    }
    if (const auto* update = std::get_if<UpdateStatement>(&statement)) {
        const Table& table = database.require_table(update->table);
        require_column(table, update->column);
        value_from_literal(update->value, table.columns[require_column(table, update->column)].type,
                           update->column);
        return explain_read(database, update->table, update->where, false);
    }
    if (const auto* deleted = std::get_if<DeleteStatement>(&statement)) {
        if (!deleted->where.has_value()) {
            database.require_table(deleted->table);
            return kNoScanDelete;
        }
        return explain_read(database, deleted->table, deleted->where, false);
    }
    return kNoScanDml;
}

std::string format_result_set(const ResultSet& result) {
    const std::size_t column_count = result.column_names.size();
    std::vector<std::size_t> widths(column_count, 0);
    for (std::size_t i = 0; i < column_count; ++i) {
        widths[i] = result.column_names[i].size();
    }
    for (const std::vector<std::string>& row : result.rows) {
        for (std::size_t i = 0; i < column_count && i < row.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }

    auto join_line = [&](const std::vector<std::string>& cells) {
        std::string line;
        for (std::size_t i = 0; i < column_count; ++i) {
            if (i != 0) {
                line += " | ";
            }
            const std::string cell = i < cells.size() ? cells[i] : std::string();
            // Leave the last cell unpadded so a real trailing space in TEXT
            // is not trimmed off with the alignment padding.
            if (i + 1 == column_count) {
                line += cell;
            } else {
                line += pad(cell, widths[i]);
            }
        }
        return line;
    };

    std::string out = join_line(result.column_names);
    out += '\n';
    for (std::size_t i = 0; i < column_count; ++i) {
        if (i != 0) {
            out += "-+-";
        }
        out += std::string(widths[i] == 0 ? 1 : widths[i], '-');
    }
    for (const std::vector<std::string>& row : result.rows) {
        out += '\n';
        out += join_line(row);
    }
    out += "\n(";
    out += std::to_string(result.rows.size());
    out += result.rows.size() == 1 ? " row)" : " rows)";
    return out;
}

}  // namespace minidb
