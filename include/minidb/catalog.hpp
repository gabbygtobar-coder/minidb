#pragma once

#include "minidb/ast.hpp"
#include "minidb/value.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace minidb {

// One in-memory table. Rows stay in insertion order. Dropping the table
// drops its rows. Nothing here is written to disk.
struct Table {
    std::string name;
    std::vector<ColumnDefinition> columns;
    std::vector<std::vector<Value>> rows;

    // Column names are case-sensitive. Empty when `column_name` is not here.
    std::optional<std::size_t> index_of(const std::string& column_name) const;
};

// The whole database for one process. Table names are case-sensitive.
// Destroying the database discards every table.
class Database {
  public:
    void create_table(std::string name, std::vector<ColumnDefinition> columns);
    void drop_table(const std::string& name);

    // Names in lexicographic order.
    std::vector<std::string> table_names() const;

    Table& require_table(const std::string& name);
    const Table& require_table(const std::string& name) const;

  private:
    std::map<std::string, Table> tables_;
};

// One line the shell can print for `.schema`, and that CREATE TABLE would
// accept again: `CREATE TABLE name (col TYPE, ...);`
std::string format_schema(const Table& table);

}  // namespace minidb
