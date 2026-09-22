#pragma once

#include "minidb/ast.hpp"
#include "minidb/value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace minidb {

// On-disk table description stored in the catalog heap. Page ids are zero
// when the table has no heap pages yet. This is a byte format, not a dump
// of the in-memory Table object.
struct CatalogEntry {
    std::string name;
    std::vector<ColumnDefinition> columns;
    std::uint32_t head_page = 0;
    std::uint32_t tail_page = 0;
    std::uint32_t overflow_head = 0;
};

// Encode and decode one row. The buffer is little-endian and self-describing
// (a type tag per cell). `decode_values` throws StorageError if the bytes are
// truncated, padded, or tagged with an unknown type.
std::vector<std::uint8_t> encode_values(const std::vector<Value>& values);
std::vector<Value> decode_values(const std::uint8_t* data, std::size_t size);

std::vector<std::uint8_t> encode_catalog_entry(const CatalogEntry& entry);
CatalogEntry decode_catalog_entry(const std::uint8_t* data, std::size_t size);

}  // namespace minidb
