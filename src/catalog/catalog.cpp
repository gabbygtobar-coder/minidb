#include "minidb/catalog.hpp"

#include "minidb/execution_error.hpp"

namespace minidb {
namespace {

void ensure_unique_columns(const std::vector<ColumnDefinition>& columns) {
    for (std::size_t i = 0; i < columns.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (columns[j].name == columns[i].name) {
                throw ExecutionError("Duplicate column name: " + columns[i].name);
            }
        }
    }
}

}  // namespace

std::optional<std::size_t> Table::index_of(const std::string& column_name) const {
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].name == column_name) {
            return i;
        }
    }
    return std::nullopt;
}

void Database::create_table(std::string name, std::vector<ColumnDefinition> columns) {
    if (tables_.find(name) != tables_.end()) {
        throw ExecutionError("Table already exists: " + name);
    }
    if (columns.empty()) {
        throw ExecutionError("Table " + name + " must have at least one column");
    }
    ensure_unique_columns(columns);

    Table table;
    table.name = name;
    table.columns = std::move(columns);
    tables_.emplace(std::move(name), std::move(table));
}

void Database::drop_table(const std::string& name) {
    const auto found = tables_.find(name);
    if (found == tables_.end()) {
        throw ExecutionError("No such table: " + name);
    }
    tables_.erase(found);
}

std::vector<std::string> Database::table_names() const {
    std::vector<std::string> names;
    names.reserve(tables_.size());
    for (const auto& entry : tables_) {
        names.push_back(entry.first);
    }
    return names;
}

Table& Database::require_table(const std::string& name) {
    const auto found = tables_.find(name);
    if (found == tables_.end()) {
        throw ExecutionError("No such table: " + name);
    }
    return found->second;
}

const Table& Database::require_table(const std::string& name) const {
    const auto found = tables_.find(name);
    if (found == tables_.end()) {
        throw ExecutionError("No such table: " + name);
    }
    return found->second;
}

std::string format_schema(const Table& table) {
    std::string out = "CREATE TABLE " + table.name + " (";
    for (std::size_t i = 0; i < table.columns.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += table.columns[i].name;
        out += ' ';
        out += data_type_name(table.columns[i].type);
    }
    out += ");";
    return out;
}

}  // namespace minidb
