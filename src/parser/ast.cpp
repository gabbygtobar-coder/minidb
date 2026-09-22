#include "minidb/ast.hpp"

#include <stdexcept>

namespace minidb {
namespace {

const char* data_type_name(DataType type) {
    switch (type) {
        case DataType::Int:
            return "INT";
        case DataType::Text:
            return "TEXT";
        case DataType::Boolean:
            return "BOOLEAN";
        case DataType::Float:
            return "FLOAT";
    }
    throw std::logic_error("unknown data type");
}

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

std::string quote_string(const std::string& value) {
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

std::string format_literal(const Literal& literal) {
    switch (literal.kind) {
        case LiteralKind::Integer:
        case LiteralKind::Float:
            return literal.text;
        case LiteralKind::Boolean:
            return literal.boolean ? "TRUE" : "FALSE";
        case LiteralKind::String:
            return quote_string(literal.text);
    }
    throw std::logic_error("unknown literal kind");
}

std::string format_where(const WhereClause& where) {
    return " WHERE " + where.column + " " + comparison_symbol(where.op) + " " +
           format_literal(where.value);
}

std::string format_create(const CreateTableStatement& statement) {
    std::string out = "CreateTable " + statement.name + " (";
    for (std::size_t i = 0; i < statement.columns.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += statement.columns[i].name;
        out += ' ';
        out += data_type_name(statement.columns[i].type);
    }
    out += ')';
    return out;
}

std::string format_drop(const DropTableStatement& statement) {
    return "DropTable " + statement.name;
}

std::string format_insert(const InsertStatement& statement) {
    std::string out = "Insert " + statement.table + " VALUES (";
    for (std::size_t i = 0; i < statement.values.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += format_literal(statement.values[i]);
    }
    out += ')';
    return out;
}

std::string format_select(const SelectStatement& statement) {
    std::string columns;
    if (statement.select_all) {
        columns = "*";
    } else {
        for (std::size_t i = 0; i < statement.columns.size(); ++i) {
            if (i != 0) {
                columns += ", ";
            }
            columns += statement.columns[i];
        }
    }
    std::string out = "Select " + columns + " FROM " + statement.table;
    if (statement.where.has_value()) {
        out += format_where(*statement.where);
    }
    return out;
}

std::string format_update(const UpdateStatement& statement) {
    std::string out = "Update " + statement.table + " SET " + statement.column + " = " +
                      format_literal(statement.value);
    if (statement.where.has_value()) {
        out += format_where(*statement.where);
    }
    return out;
}

std::string format_delete(const DeleteStatement& statement) {
    std::string out = "Delete FROM " + statement.table;
    if (statement.where.has_value()) {
        out += format_where(*statement.where);
    }
    return out;
}

struct StatementFormatter {
    std::string operator()(const CreateTableStatement& statement) const {
        return format_create(statement);
    }
    std::string operator()(const DropTableStatement& statement) const {
        return format_drop(statement);
    }
    std::string operator()(const InsertStatement& statement) const {
        return format_insert(statement);
    }
    std::string operator()(const SelectStatement& statement) const {
        return format_select(statement);
    }
    std::string operator()(const UpdateStatement& statement) const {
        return format_update(statement);
    }
    std::string operator()(const DeleteStatement& statement) const {
        return format_delete(statement);
    }
};

}  // namespace

std::string format_statement(const Statement& statement) {
    return std::visit(StatementFormatter{}, statement);
}

}  // namespace minidb
