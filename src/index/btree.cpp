#include "minidb/btree.hpp"

#include "minidb/execution_error.hpp"
#include "minidb/storage_error.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace minidb {
namespace {

constexpr std::size_t kTypeOff = 0;
constexpr std::size_t kFlagsOff = 1;
constexpr std::size_t kCountOff = 2;
constexpr std::size_t kEndOff = 4;
constexpr std::size_t kRightOff = 8;
constexpr std::size_t kLeftOff = 12;
constexpr std::size_t kHeader = 16;
constexpr std::uint8_t kFlagLeaf = 0x01;

struct Sep {
    IndexKey key;
    std::uint32_t right_child = 0;
};

struct InternalNode {
    bool has_leftmost = false;
    std::uint32_t leftmost = 0;
    std::vector<Sep> seps;
};

struct Promotion {
    IndexKey key;
    std::uint32_t right_page = 0;
};

struct Cursor {
    std::uint32_t page_id = 0;
    std::uint16_t index = 0;
};

int compare_columns(const std::vector<std::uint8_t>& left, const std::vector<std::uint8_t>& right) {
    const std::size_t n = std::min(left.size(), right.size());
    if (n > 0) {
        const int cmp = std::memcmp(left.data(), right.data(), n);
        if (cmp != 0) {
            return cmp < 0 ? -1 : 1;
        }
    }
    if (left.size() < right.size()) {
        return -1;
    }
    if (left.size() > right.size()) {
        return 1;
    }
    return 0;
}

std::vector<std::uint8_t> big_endian(std::uint64_t value) {
    std::vector<std::uint8_t> out(8);
    for (int shift = 56; shift >= 0; shift -= 8) {
        out[static_cast<std::size_t>(56 - shift) / 8] =
            static_cast<std::uint8_t>((value >> shift) & 0xFFu);
    }
    return out;
}

bool is_leaf(const Page& page) {
    return (page.read_u8(kFlagsOff) & kFlagLeaf) != 0;
}

void require_index_page(const Page& page) {
    if (page.read_u8(kTypeOff) != kPageTypeIndex) {
        throw StorageError("Expected an index page");
    }
    const auto count = page.read_u16(kCountOff);
    const auto end = page.read_u16(kEndOff);
    const std::size_t min_end = is_leaf(page) ? kHeader : kHeader + 4;
    if (end < min_end || end > kPageSize) {
        throw StorageError("Corrupt index page");
    }
    const std::size_t directory = static_cast<std::size_t>(count) * 2;
    if (end > kPageSize - directory) {
        throw StorageError("Corrupt index page");
    }
}

std::uint16_t entry_count(const Page& page) {
    return page.read_u16(kCountOff);
}

std::size_t directory_at(std::uint16_t index) {
    return kPageSize - (static_cast<std::size_t>(index) + 1) * 2;
}

std::size_t leaf_entry_size(const IndexKey& key) {
    return 8 + key.column.size();
}

std::size_t sep_entry_size(const Sep& sep) {
    return 12 + sep.key.column.size();
}

std::size_t leaf_page_bytes(const std::vector<IndexKey>& entries, std::size_t begin, std::size_t end) {
    std::size_t bytes = kHeader;
    for (std::size_t i = begin; i < end; ++i) {
        bytes += leaf_entry_size(entries[i]);
    }
    bytes += (end - begin) * 2;
    return bytes;
}

bool leaf_range_fits(const std::vector<IndexKey>& entries, std::size_t begin, std::size_t end) {
    return leaf_page_bytes(entries, begin, end) <= kPageSize;
}

std::size_t internal_page_bytes(const InternalNode& node) {
    std::size_t bytes = kHeader + 4;
    for (const Sep& sep : node.seps) {
        bytes += sep_entry_size(sep);
    }
    bytes += node.seps.size() * 2;
    return bytes;
}

bool internal_fits(const InternalNode& node) {
    return node.has_leftmost && internal_page_bytes(node) <= kPageSize;
}

void ensure_key_size(const IndexKey& key) {
    if (key.column.size() > kMaxIndexKeyBytes) {
        throw ExecutionError("Indexed value exceeds " + std::to_string(kMaxIndexKeyBytes) +
                             " bytes");
    }
}

IndexKey read_leaf_entry(const Page& page, std::size_t off, std::size_t limit) {
    if (off + 2 > limit) {
        throw StorageError("Corrupt index page");
    }
    const auto length = page.read_u16(off);
    const std::size_t size = static_cast<std::size_t>(length) + 8;
    if (off + size > limit) {
        throw StorageError("Corrupt index page");
    }
    IndexKey key;
    key.column.resize(length);
    if (length != 0) {
        page.read_bytes(off + 2, key.column.data(), length);
    }
    key.page_id = page.read_u32(off + 2 + length);
    key.slot = page.read_u16(off + 6 + length);
    return key;
}

Sep read_sep(const Page& page, std::size_t off, std::size_t limit) {
    if (off + 2 > limit) {
        throw StorageError("Corrupt index page");
    }
    const auto length = page.read_u16(off);
    const std::size_t size = static_cast<std::size_t>(length) + 12;
    if (off + size > limit) {
        throw StorageError("Corrupt index page");
    }
    Sep sep;
    sep.key.column.resize(length);
    if (length != 0) {
        page.read_bytes(off + 2, sep.key.column.data(), length);
    }
    sep.key.page_id = page.read_u32(off + 2 + length);
    sep.key.slot = page.read_u16(off + 6 + length);
    sep.right_child = page.read_u32(off + 8 + length);
    if (sep.right_child == 0) {
        throw StorageError("Corrupt index page");
    }
    return sep;
}

std::vector<IndexKey> read_leaf(const Page& page) {
    require_index_page(page);
    if (!is_leaf(page)) {
        throw StorageError("Expected a leaf index page");
    }
    const auto count = entry_count(page);
    const auto end = page.read_u16(kEndOff);
    std::vector<IndexKey> entries;
    entries.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        const auto off = page.read_u16(directory_at(i));
        if (off < kHeader || off >= end) {
            throw StorageError("Corrupt index page");
        }
        entries.push_back(read_leaf_entry(page, off, end));
    }
    return entries;
}

InternalNode read_internal(const Page& page) {
    require_index_page(page);
    if (is_leaf(page)) {
        throw StorageError("Expected an internal index page");
    }
    InternalNode node;
    node.has_leftmost = true;
    node.leftmost = page.read_u32(kHeader);
    if (node.leftmost == 0) {
        throw StorageError("Corrupt index page");
    }
    const auto count = entry_count(page);
    const auto end = page.read_u16(kEndOff);
    node.seps.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        const auto off = page.read_u16(directory_at(i));
        if (off < kHeader + 4 || off >= end) {
            throw StorageError("Corrupt index page");
        }
        node.seps.push_back(read_sep(page, off, end));
    }
    return node;
}

std::size_t write_leaf_entry(Page& page, std::size_t off, const IndexKey& key) {
    const auto length = static_cast<std::uint16_t>(key.column.size());
    page.write_u16(off, length);
    if (length != 0) {
        page.write_bytes(off + 2, key.column.data(), length);
    }
    page.write_u32(off + 2 + length, key.page_id);
    page.write_u16(off + 6 + length, key.slot);
    return off + leaf_entry_size(key);
}

std::size_t write_sep_entry(Page& page, std::size_t off, const Sep& sep) {
    const auto length = static_cast<std::uint16_t>(sep.key.column.size());
    page.write_u16(off, length);
    if (length != 0) {
        page.write_bytes(off + 2, sep.key.column.data(), length);
    }
    page.write_u32(off + 2 + length, sep.key.page_id);
    page.write_u16(off + 6 + length, sep.key.slot);
    page.write_u32(off + 8 + length, sep.right_child);
    return off + sep_entry_size(sep);
}

void format_leaf(Page& page, const std::vector<IndexKey>& entries, std::uint32_t left,
                 std::uint32_t right) {
    if (entries.size() > std::numeric_limits<std::uint16_t>::max() ||
        !leaf_range_fits(entries, 0, entries.size())) {
        throw StorageError("Index leaf does not fit");
    }
    page = Page();
    page.write_u8(kTypeOff, kPageTypeIndex);
    page.write_u8(kFlagsOff, kFlagLeaf);
    page.write_u16(kCountOff, static_cast<std::uint16_t>(entries.size()));
    page.write_u32(kLeftOff, left);
    page.write_u32(kRightOff, right);
    std::size_t cursor = kHeader;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::size_t entry_off = cursor;
        cursor = write_leaf_entry(page, cursor, entries[i]);
        page.write_u16(directory_at(static_cast<std::uint16_t>(i)),
                       static_cast<std::uint16_t>(entry_off));
    }
    page.write_u16(kEndOff, static_cast<std::uint16_t>(cursor));
}

void format_internal(Page& page, const InternalNode& node) {
    if (!internal_fits(node) || node.seps.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw StorageError("Index internal node does not fit");
    }
    page = Page();
    page.write_u8(kTypeOff, kPageTypeIndex);
    page.write_u16(kCountOff, static_cast<std::uint16_t>(node.seps.size()));
    page.write_u32(kHeader, node.leftmost);
    std::size_t cursor = kHeader + 4;
    for (std::size_t i = 0; i < node.seps.size(); ++i) {
        const std::size_t entry_off = cursor;
        cursor = write_sep_entry(page, cursor, node.seps[i]);
        page.write_u16(directory_at(static_cast<std::uint16_t>(i)),
                       static_cast<std::uint16_t>(entry_off));
    }
    page.write_u16(kEndOff, static_cast<std::uint16_t>(cursor));
}

std::uint32_t child_at(const InternalNode& node, std::uint16_t index) {
    if (!node.has_leftmost) {
        throw StorageError("Corrupt index page");
    }
    if (index == 0) {
        return node.leftmost;
    }
    if (static_cast<std::size_t>(index - 1) >= node.seps.size()) {
        throw StorageError("Corrupt index page");
    }
    return node.seps[index - 1].right_child;
}

std::uint16_t child_index_for(const InternalNode& node, const IndexKey& key) {
    std::uint16_t index = 0;
    for (std::size_t i = 0; i < node.seps.size(); ++i) {
        if (compare_index_keys(node.seps[i].key, key) <= 0) {
            index = static_cast<std::uint16_t>(i + 1);
        } else {
            break;
        }
    }
    return index;
}

void erase_child(InternalNode& node, std::uint16_t index) {
    if (!node.has_leftmost) {
        throw StorageError("Corrupt index page");
    }
    if (index == 0) {
        if (node.seps.empty()) {
            node.has_leftmost = false;
            node.leftmost = 0;
            return;
        }
        node.leftmost = node.seps.front().right_child;
        node.seps.erase(node.seps.begin());
        return;
    }
    if (static_cast<std::size_t>(index - 1) >= node.seps.size()) {
        throw StorageError("Corrupt index page");
    }
    node.seps.erase(node.seps.begin() + (index - 1));
}

std::size_t choose_leaf_split(const std::vector<IndexKey>& entries) {
    std::size_t best = 0;
    std::size_t best_gap = std::numeric_limits<std::size_t>::max();
    for (std::size_t split = 1; split < entries.size(); ++split) {
        if (!leaf_range_fits(entries, 0, split) || !leaf_range_fits(entries, split, entries.size())) {
            continue;
        }
        const std::size_t left_bytes = leaf_page_bytes(entries, 0, split);
        const std::size_t right_bytes = leaf_page_bytes(entries, split, entries.size());
        const std::size_t gap = left_bytes > right_bytes ? left_bytes - right_bytes : right_bytes - left_bytes;
        if (gap < best_gap) {
            best_gap = gap;
            best = split;
        }
    }
    if (best == 0) {
        throw StorageError("Index entry does not fit in a page");
    }
    return best;
}

void unlink_leaf(Pager& pager, const Page& page) {
    const std::uint32_t left = page.read_u32(kLeftOff);
    const std::uint32_t right = page.read_u32(kRightOff);
    if (left != 0) {
        Page neighbor = pager.read_page(left);
        require_index_page(neighbor);
        neighbor.write_u32(kRightOff, right);
        pager.write_page(left, neighbor);
    }
    if (right != 0) {
        Page neighbor = pager.read_page(right);
        require_index_page(neighbor);
        neighbor.write_u32(kLeftOff, left);
        pager.write_page(right, neighbor);
    }
}

std::optional<Promotion> insert_leaf(Pager& pager, std::uint32_t page_id, Page page, const IndexKey& key) {
    std::vector<IndexKey> entries = read_leaf(page);
    const auto place = std::lower_bound(
        entries.begin(), entries.end(), key,
        [](const IndexKey& left, const IndexKey& right) { return compare_index_keys(left, right) < 0; });
    if (place != entries.end() && compare_index_keys(*place, key) == 0) {
        throw StorageError("Duplicate index entry");
    }
    entries.insert(place, key);
    if (leaf_range_fits(entries, 0, entries.size())) {
        format_leaf(page, entries, page.read_u32(kLeftOff), page.read_u32(kRightOff));
        pager.write_page(page_id, page);
        return std::nullopt;
    }

    const std::size_t split = choose_leaf_split(entries);
    std::vector<IndexKey> right_entries(entries.begin() + static_cast<std::ptrdiff_t>(split), entries.end());
    entries.resize(split);
    const IndexKey separator = right_entries.front();
    const std::uint32_t old_left = page.read_u32(kLeftOff);
    const std::uint32_t old_right = page.read_u32(kRightOff);
    const std::uint32_t new_id = pager.allocate_page();

    Page left_page;
    format_leaf(left_page, entries, old_left, new_id);
    Page right_page;
    format_leaf(right_page, right_entries, page_id, old_right);
    pager.write_page(page_id, left_page);
    pager.write_page(new_id, right_page);
    if (old_right != 0) {
        Page neighbor = pager.read_page(old_right);
        require_index_page(neighbor);
        neighbor.write_u32(kLeftOff, new_id);
        pager.write_page(old_right, neighbor);
    }

    Promotion promotion;
    promotion.key = separator;
    promotion.right_page = new_id;
    return promotion;
}

// `mid` is the separator that moves up. Either side may hold zero separators
// (a single child). A node that fit before this separator was added always has
// at least one such cut: the pre-insert separators are a subset of one side
// when the new separator is the one promoted, and each single separator fits.
std::size_t choose_internal_split(const InternalNode& node) {
    std::size_t best = node.seps.size();
    std::size_t best_gap = std::numeric_limits<std::size_t>::max();
    for (std::size_t mid = 0; mid < node.seps.size(); ++mid) {
        InternalNode left_node;
        left_node.has_leftmost = true;
        left_node.leftmost = node.leftmost;
        left_node.seps.assign(node.seps.begin(), node.seps.begin() + static_cast<std::ptrdiff_t>(mid));

        InternalNode right_node;
        right_node.has_leftmost = true;
        right_node.leftmost = node.seps[mid].right_child;
        right_node.seps.assign(node.seps.begin() + static_cast<std::ptrdiff_t>(mid + 1), node.seps.end());
        if (!internal_fits(left_node) || !internal_fits(right_node)) {
            continue;
        }
        const std::size_t left_bytes = internal_page_bytes(left_node);
        const std::size_t right_bytes = internal_page_bytes(right_node);
        const std::size_t gap =
            left_bytes > right_bytes ? left_bytes - right_bytes : right_bytes - left_bytes;
        if (gap < best_gap) {
            best_gap = gap;
            best = mid;
        }
    }
    if (best == node.seps.size()) {
        throw StorageError("Index entry does not fit in a page");
    }
    return best;
}

std::optional<Promotion> insert_internal(Pager& pager, std::uint32_t page_id, const Page& page,
                                         const Promotion& promotion, const IndexKey& inserted) {
    InternalNode node = read_internal(page);
    const std::uint16_t child_index = child_index_for(node, inserted);
    Sep created;
    created.key = promotion.key;
    created.right_child = promotion.right_page;
    node.seps.insert(node.seps.begin() + child_index, std::move(created));
    if (internal_fits(node)) {
        Page updated;
        format_internal(updated, node);
        pager.write_page(page_id, updated);
        return std::nullopt;
    }

    const std::size_t mid = choose_internal_split(node);
    InternalNode left_node;
    left_node.has_leftmost = true;
    left_node.leftmost = node.leftmost;
    left_node.seps.assign(node.seps.begin(), node.seps.begin() + static_cast<std::ptrdiff_t>(mid));

    InternalNode right_node;
    right_node.has_leftmost = true;
    right_node.leftmost = node.seps[mid].right_child;
    right_node.seps.assign(node.seps.begin() + static_cast<std::ptrdiff_t>(mid + 1), node.seps.end());

    const std::uint32_t new_id = pager.allocate_page();
    Page left_page;
    format_internal(left_page, left_node);
    Page right_page;
    format_internal(right_page, right_node);
    pager.write_page(page_id, left_page);
    pager.write_page(new_id, right_page);

    Promotion up;
    up.key = node.seps[mid].key;
    up.right_page = new_id;
    return up;
}

std::optional<Promotion> insert_rec(Pager& pager, std::uint32_t page_id, const IndexKey& key, std::size_t depth) {
    if (depth > pager.page_count()) {
        throw StorageError("Index page chain is cyclic");
    }
    const Page page = pager.read_page(page_id);
    require_index_page(page);
    if (is_leaf(page)) {
        return insert_leaf(pager, page_id, page, key);
    }
    const InternalNode node = read_internal(page);
    const std::uint32_t child = child_at(node, child_index_for(node, key));
    const std::optional<Promotion> promotion = insert_rec(pager, child, key, depth + 1);
    if (!promotion.has_value()) {
        return std::nullopt;
    }
    return insert_internal(pager, page_id, page, *promotion, key);
}

void install_root(Pager& pager, std::uint32_t& root, const Promotion& promotion) {
    InternalNode node;
    node.has_leftmost = true;
    node.leftmost = root;
    Sep sep;
    sep.key = promotion.key;
    sep.right_child = promotion.right_page;
    node.seps.push_back(std::move(sep));
    const std::uint32_t id = pager.allocate_page();
    Page page;
    format_internal(page, node);
    pager.write_page(id, page);
    root = id;
}

// True when `page_id` is now empty and its parent should free it.
// A root with one remaining child is collapsed into that child.
bool remove_rec(Pager& pager, std::uint32_t page_id, const IndexKey& key, bool is_root, std::uint32_t& root) {
    if (pager.page_count() == 0) {
        throw StorageError("Index page chain is cyclic");
    }
    Page page = pager.read_page(page_id);
    require_index_page(page);
    if (is_leaf(page)) {
        std::vector<IndexKey> entries = read_leaf(page);
        const auto place = std::lower_bound(
            entries.begin(), entries.end(), key, [](const IndexKey& left, const IndexKey& right) {
                return compare_index_keys(left, right) < 0;
            });
        if (place == entries.end() || compare_index_keys(*place, key) != 0) {
            throw StorageError("Index entry is missing");
        }
        entries.erase(place);
        if (entries.empty() && !is_root) {
            unlink_leaf(pager, page);
            return true;
        }
        format_leaf(page, entries, page.read_u32(kLeftOff), page.read_u32(kRightOff));
        pager.write_page(page_id, page);
        return false;
    }

    InternalNode node = read_internal(page);
    const std::uint16_t child_index = child_index_for(node, key);
    const std::uint32_t child = child_at(node, child_index);
    const bool gone = remove_rec(pager, child, key, false, root);
    if (!gone) {
        return false;
    }
    pager.free_page(child);
    erase_child(node, child_index);
    if (!node.has_leftmost) {
        if (is_root) {
            format_leaf(page, {}, 0, 0);
            pager.write_page(page_id, page);
            return false;
        }
        return true;
    }
    if (is_root && node.seps.empty()) {
        const std::uint32_t only = node.leftmost;
        pager.free_page(page_id);
        root = only;
        return false;
    }
    format_internal(page, node);
    pager.write_page(page_id, page);
    return false;
}

Cursor first_leaf(const Pager& pager, std::uint32_t root) {
    std::uint32_t page_id = root;
    for (std::size_t depth = 0; depth <= pager.page_count(); ++depth) {
        const Page page = pager.read_page(page_id);
        require_index_page(page);
        if (is_leaf(page)) {
            return Cursor{page_id, 0};
        }
        page_id = read_internal(page).leftmost;
    }
    throw StorageError("Index page chain is cyclic");
}

Cursor lower_bound_leaf(const Pager& pager, std::uint32_t root, const IndexKey& probe) {
    std::uint32_t page_id = root;
    for (std::size_t depth = 0; depth <= pager.page_count(); ++depth) {
        const Page page = pager.read_page(page_id);
        require_index_page(page);
        if (is_leaf(page)) {
            const std::vector<IndexKey> entries = read_leaf(page);
            const auto place = std::lower_bound(
                entries.begin(), entries.end(), probe,
                [](const IndexKey& left, const IndexKey& right) {
                    return compare_index_keys(left, right) < 0;
                });
            const auto index = static_cast<std::uint16_t>(place - entries.begin());
            return Cursor{page_id, index};
        }
        const InternalNode node = read_internal(page);
        page_id = child_at(node, child_index_for(node, probe));
    }
    throw StorageError("Index page chain is cyclic");
}

struct Span {
    bool empty = true;
    IndexKey min;
    IndexKey max;
};

Span check_node(const Pager& pager, std::uint32_t page_id, int depth, int& leaf_depth,
                std::vector<std::uint32_t>& leaves) {
    if (depth > static_cast<int>(pager.page_count())) {
        throw StorageError("Index page chain is cyclic");
    }
    const Page page = pager.read_page(page_id);
    require_index_page(page);
    if (is_leaf(page)) {
        if (leaf_depth < 0) {
            leaf_depth = depth;
        } else if (leaf_depth != depth) {
            throw StorageError("Index leaves are not at the same depth");
        }
        leaves.push_back(page_id);
        const std::vector<IndexKey> entries = read_leaf(page);
        for (std::size_t i = 1; i < entries.size(); ++i) {
            if (compare_index_keys(entries[i - 1], entries[i]) >= 0) {
                throw StorageError("Index leaf is not strictly ordered");
            }
        }
        Span span;
        if (!entries.empty()) {
            span.empty = false;
            span.min = entries.front();
            span.max = entries.back();
        }
        return span;
    }

    const InternalNode node = read_internal(page);
    if (!node.has_leftmost) {
        throw StorageError("Corrupt index page");
    }
    Span left = check_node(pager, node.leftmost, depth + 1, leaf_depth, leaves);
    if (left.empty) {
        throw StorageError("Empty index subtree");
    }
    for (const Sep& sep : node.seps) {
        if (compare_index_keys(left.max, sep.key) >= 0) {
            throw StorageError("Index separator is not greater than the left subtree");
        }
        const Span right = check_node(pager, sep.right_child, depth + 1, leaf_depth, leaves);
        if (right.empty || compare_index_keys(right.min, sep.key) < 0) {
            throw StorageError("Index separator is greater than the right subtree");
        }
        left.max = right.max;
    }
    return left;
}

void check_leaf_links(const Pager& pager, const std::vector<std::uint32_t>& leaves) {
    if (leaves.empty()) {
        return;
    }
    std::uint32_t previous = 0;
    for (std::size_t i = 0; i < leaves.size(); ++i) {
        const Page page = pager.read_page(leaves[i]);
        require_index_page(page);
        if (!is_leaf(page)) {
            throw StorageError("Expected a leaf index page");
        }
        if (page.read_u32(kLeftOff) != previous) {
            throw StorageError("Index leaf chain is broken");
        }
        const std::uint32_t right = page.read_u32(kRightOff);
        if (i + 1 == leaves.size()) {
            if (right != 0) {
                throw StorageError("Index leaf chain is broken");
            }
        } else if (right != leaves[i + 1]) {
            throw StorageError("Index leaf chain is broken");
        }
        previous = leaves[i];
    }
}

void stats_rec(const Pager& pager, std::uint32_t page_id, BTreeStats& stats, std::uint32_t depth) {
    if (depth > pager.page_count()) {
        throw StorageError("Index page chain is cyclic");
    }
    const Page page = pager.read_page(page_id);
    require_index_page(page);
    ++stats.pages;
    if (is_leaf(page)) {
        ++stats.leaves;
        stats.keys += entry_count(page);
        stats.height = std::max(stats.height, depth + 1);
        return;
    }
    const InternalNode node = read_internal(page);
    stats_rec(pager, node.leftmost, stats, depth + 1);
    for (const Sep& sep : node.seps) {
        stats_rec(pager, sep.right_child, stats, depth + 1);
    }
}

void destroy_rec(Pager& pager, std::uint32_t page_id, std::unordered_set<std::uint32_t>& seen, std::size_t depth) {
    if (depth > pager.page_count() || !seen.insert(page_id).second) {
        throw StorageError("Index page chain is cyclic");
    }
    const Page page = pager.read_page(page_id);
    require_index_page(page);
    if (!is_leaf(page)) {
        const InternalNode node = read_internal(page);
        if (node.has_leftmost) {
            destroy_rec(pager, node.leftmost, seen, depth + 1);
        }
        for (const Sep& sep : node.seps) {
            destroy_rec(pager, sep.right_child, seen, depth + 1);
        }
    }
    pager.free_page(page_id);
}

}  // namespace

int compare_index_keys(const IndexKey& left, const IndexKey& right) {
    const int cmp = compare_columns(left.column, right.column);
    if (cmp != 0) {
        return cmp;
    }
    if (left.page_id < right.page_id) {
        return -1;
    }
    if (left.page_id > right.page_id) {
        return 1;
    }
    if (left.slot < right.slot) {
        return -1;
    }
    if (left.slot > right.slot) {
        return 1;
    }
    return 0;
}

std::vector<std::uint8_t> encode_index_column(const Value& value) {
    switch (value.type()) {
        case DataType::Int: {
            auto bits = static_cast<std::uint64_t>(value.integer());
            bits ^= 0x8000000000000000ULL;
            return big_endian(bits);
        }
        case DataType::Float: {
            std::uint64_t bits = 0x8000000000000000ULL;
            const double number = value.floating();
            if (number != 0.0) {
                std::memcpy(&bits, &number, sizeof(bits));
                if ((bits & 0x8000000000000000ULL) != 0) {
                    bits = ~bits;
                } else {
                    bits ^= 0x8000000000000000ULL;
                }
            }
            return big_endian(bits);
        }
        case DataType::Boolean:
            return {static_cast<std::uint8_t>(value.boolean() ? 1 : 0)};
        case DataType::Text: {
            const std::string& text = value.text();
            if (text.size() > kMaxIndexKeyBytes) {
                throw ExecutionError("Indexed value exceeds " + std::to_string(kMaxIndexKeyBytes) +
                                     " bytes");
            }
            return std::vector<std::uint8_t>(text.begin(), text.end());
        }
    }
    throw std::logic_error("unknown data type");
}

std::uint32_t btree_create(Pager& pager) {
    const std::uint32_t id = pager.allocate_page();
    Page page;
    format_leaf(page, {}, 0, 0);
    pager.write_page(id, page);
    return id;
}

void btree_insert(Pager& pager, std::uint32_t& root, const IndexKey& key) {
    if (root == 0 || root >= pager.page_count()) {
        throw StorageError("Index root is missing");
    }
    ensure_key_size(key);
    const std::optional<Promotion> promotion = insert_rec(pager, root, key, 0);
    if (promotion.has_value()) {
        install_root(pager, root, *promotion);
    }
}

void btree_remove(Pager& pager, std::uint32_t& root, const IndexKey& key) {
    if (root == 0 || root >= pager.page_count()) {
        throw StorageError("Index root is missing");
    }
    remove_rec(pager, root, key, true, root);
}

std::vector<RowId> btree_scan(const Pager& pager, std::uint32_t root, const IndexBound& low,
                              const IndexBound& high) {
    if (root == 0 || root >= pager.page_count()) {
        throw StorageError("Index root is missing");
    }
    Cursor cursor;
    if (low.unbounded) {
        cursor = first_leaf(pager, root);
    } else {
        IndexKey probe;
        probe.column = low.column;
        cursor = lower_bound_leaf(pager, root, probe);
    }

    std::vector<RowId> rows;
    std::size_t pages_seen = 0;
    std::uint32_t previous_page = 0;
    while (cursor.page_id != 0) {
        if (cursor.page_id != previous_page) {
            previous_page = cursor.page_id;
            if (++pages_seen > pager.page_count()) {
                throw StorageError("Index leaf chain is cyclic");
            }
        }
        const Page page = pager.read_page(cursor.page_id);
        require_index_page(page);
        if (!is_leaf(page)) {
            throw StorageError("Expected a leaf index page");
        }
        const auto count = entry_count(page);
        if (cursor.index >= count) {
            cursor.page_id = page.read_u32(kRightOff);
            cursor.index = 0;
            continue;
        }
        const auto end = page.read_u16(kEndOff);
        const auto off = page.read_u16(directory_at(cursor.index));
        const IndexKey key = read_leaf_entry(page, off, end);
        if (!low.unbounded && !low.inclusive && compare_columns(key.column, low.column) == 0) {
            ++cursor.index;
            continue;
        }
        if (!high.unbounded) {
            const int cmp = compare_columns(key.column, high.column);
            if (cmp > 0 || (cmp == 0 && !high.inclusive)) {
                break;
            }
        }
        rows.push_back(RowId{key.page_id, key.slot});
        ++cursor.index;
    }
    return rows;
}

void btree_destroy(Pager& pager, std::uint32_t root) {
    if (root == 0 || root >= pager.page_count()) {
        throw StorageError("Index root is missing");
    }
    std::unordered_set<std::uint32_t> seen;
    destroy_rec(pager, root, seen, 0);
}

BTreeStats btree_stats(const Pager& pager, std::uint32_t root) {
    if (root == 0 || root >= pager.page_count()) {
        throw StorageError("Index root is missing");
    }
    BTreeStats stats;
    stats_rec(pager, root, stats, 0);
    const Page page = pager.read_page(root);
    stats.root_is_leaf = is_leaf(page);
    return stats;
}

void btree_check(const Pager& pager, std::uint32_t root) {
    if (root == 0 || root >= pager.page_count()) {
        throw StorageError("Index root is missing");
    }
    int leaf_depth = -1;
    std::vector<std::uint32_t> leaves;
    check_node(pager, root, 0, leaf_depth, leaves);
    check_leaf_links(pager, leaves);
}

}  // namespace minidb
