#pragma once

#include "minidb/ast.hpp"
#include "minidb/value.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace minidb {

// Schema for one table. Rows are not stored here; they live in heap pages.
// `index_of` is case-sensitive and empty when `column_name` is not a column.
struct Table {
    std::string name;
    std::vector<ColumnDefinition> columns;

    std::optional<std::size_t> index_of(const std::string& column_name) const;
};

// Address of a live row in a table's primary page chain. Slot indexes stay
// valid across updates and tombstones in the same statement. Page 0 is the
// file header and is never a row address.
struct RowId {
    std::uint32_t page_id = 0;
    std::uint16_t slot = 0;
};

struct StoredRow {
    RowId id;
    std::vector<Value> values;
};

// One database. The default constructor keeps pages in memory and never
// creates a file. The path constructor opens or creates a database file.
// Destroying the object flushes dirty pages (fflush, not fsync).
//
// Table names are case-sensitive. `table_names` is lexicographic.
class Database {
  public:
    Database();
    explicit Database(std::string path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    void create_table(std::string name, std::vector<ColumnDefinition> columns);
    void drop_table(const std::string& name);

    std::vector<std::string> table_names() const;

    Table& require_table(const std::string& name);
    const Table& require_table(const std::string& name) const;

    // Heap scan in insertion order, skipping tombstones. Forwarded rows are
    // returned at the slot they were inserted into.
    std::size_t row_count(const std::string& name) const;
    std::vector<StoredRow> scan_rows(const std::string& name) const;

    void insert_row(const std::string& name, std::vector<Value> row);
    void update_row(const std::string& name, RowId id, std::vector<Value> row);
    void delete_row(const std::string& name, RowId id);
    void clear_rows(const std::string& name);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// One line the shell can print for `.schema`, and that CREATE TABLE would
// accept again: `CREATE TABLE name (col TYPE, ...);`
std::string format_schema(const Table& table);

}  // namespace minidb
