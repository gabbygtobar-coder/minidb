#pragma once

#include "minidb/catalog.hpp"
#include "minidb/pager.hpp"
#include "minidb/value.hpp"

#include <cstdint>
#include <vector>

namespace minidb {

// Encoded column bytes in a leaf or separator. TEXT longer than this cannot
// be indexed. INT, FLOAT, and BOOLEAN encodings are much smaller.
inline constexpr std::size_t kMaxIndexKeyBytes = 1024;

// Sortable encoding of one column value. INT and FLOAT compare in numeric
// order under memcmp of these bytes (after the sign-bit transform). TEXT is
// the raw bytes, ordered like std::string. BOOLEAN is 0 or 1.
// Throws ExecutionError when a TEXT value is longer than kMaxIndexKeyBytes.
std::vector<std::uint8_t> encode_index_column(const Value& value);

// Full leaf order: column encoding, then heap page id, then slot.
struct IndexKey {
    std::vector<std::uint8_t> column;
    std::uint32_t page_id = 0;
    std::uint16_t slot = 0;
};

int compare_index_keys(const IndexKey& left, const IndexKey& right);

// A missing side is unbounded. `inclusive` is ignored when `unbounded` is set.
struct IndexBound {
    bool unbounded = true;
    bool inclusive = true;
    std::vector<std::uint8_t> column;
};

struct BTreeStats {
    bool root_is_leaf = true;
    std::uint32_t height = 0;
    std::uint32_t pages = 0;
    std::uint32_t leaves = 0;
    std::uint32_t keys = 0;
};

// On-disk B+ tree. Every node is one index page in `pager`. `root` changes
// when the root splits or collapses; the caller stores it in the catalog.
// Keys are unique on (column, page_id, slot). Duplicate column values are
// allowed and ordered by row id.
std::uint32_t btree_create(Pager& pager);
void btree_insert(Pager& pager, std::uint32_t& root, const IndexKey& key);
void btree_remove(Pager& pager, std::uint32_t& root, const IndexKey& key);
std::vector<RowId> btree_scan(const Pager& pager, std::uint32_t root, const IndexBound& low,
                              const IndexBound& high);
void btree_destroy(Pager& pager, std::uint32_t root);

BTreeStats btree_stats(const Pager& pager, std::uint32_t root);

// Throws StorageError if leaf links, separator bounds, or balance are wrong.
// Deletion may leave nodes underfull; it must not leave them unbalanced or
// out of order.
void btree_check(const Pager& pager, std::uint32_t root);

}  // namespace minidb
