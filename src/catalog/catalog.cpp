#include "minidb/catalog.hpp"

namespace minidb {

std::optional<std::size_t> Table::index_of(const std::string& column_name) const {
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].name == column_name) {
            return i;
        }
    }
    return std::nullopt;
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
