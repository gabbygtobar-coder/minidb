#include "minidb/catalog.hpp"

#include "minidb/btree.hpp"
#include "minidb/execution_error.hpp"
#include "minidb/pager.hpp"
#include "minidb/record.hpp"
#include "minidb/storage_error.hpp"
#include "slotted_page.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

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

std::string too_big(const char* what, std::size_t bytes) {
    return std::string(what) + " does not fit in a single page (" + std::to_string(bytes) +
           " bytes; limit " + std::to_string(kMaxRecordBytes) + ")";
}

void ensure_row(const Table& table, const std::vector<Value>& row) {
    if (row.size() != table.columns.size()) {
        throw ExecutionError("Row for " + table.name + " expected " +
                             std::to_string(table.columns.size()) + " values, got " +
                             std::to_string(row.size()));
    }
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (row[i].type() != table.columns[i].type) {
            throw ExecutionError("Type mismatch for column " + table.columns[i].name +
                                 ": expected " + data_type_name(table.columns[i].type) + ", got " +
                                 data_type_name(row[i].type()));
        }
    }
}

std::vector<Value> decode_checked(const Table& table, const std::vector<std::uint8_t>& bytes) {
    const std::vector<Value> values = decode_values(bytes.data(), bytes.size());
    if (values.size() != table.columns.size()) {
        throw StorageError("Corrupt row in table " + table.name);
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i].type() != table.columns[i].type) {
            throw StorageError("Corrupt row in table " + table.name);
        }
    }
    return values;
}

}  // namespace

struct Database::Impl {
    struct IndexRecord {
        std::string name;
        std::uint16_t column = 0;
        std::uint32_t root_page = 0;
    };

    struct TableRecord {
        Table table;
        std::uint32_t head_page = 0;
        std::uint32_t tail_page = 0;
        std::uint32_t overflow_head = 0;
        std::uint32_t catalog_page = 0;
        std::uint16_t catalog_slot = 0;
        std::vector<IndexRecord> indexes;
    };

    struct SlotRef {
        std::uint32_t page = 0;
        std::uint16_t slot = 0;
    };

    explicit Impl(Pager open_pager) : pager(std::move(open_pager)) {}

    TableRecord& require(const std::string& name);
    const TableRecord& require(const std::string& name) const;

    void load_catalog();
    void persist(const TableRecord& meta);
    SlotRef append(std::uint32_t& head, std::uint32_t& tail, const char* what,
                   const std::vector<std::uint8_t>& bytes);
    void free_chain(std::uint32_t head);
    bool chain_contains(std::uint32_t head, std::uint32_t page_id) const;
    void unlink_overflow(TableRecord& meta, std::uint32_t page_id);
    std::vector<std::uint8_t> payload(std::uint32_t page_id, std::uint16_t slot) const;

    IndexKey make_key(const Value& value, RowId id) const;
    void ensure_index_values(const TableRecord& meta, const std::vector<Value>& row) const;
    bool insert_indexes(TableRecord& meta, RowId id, const std::vector<Value>& row);
    bool remove_indexes(TableRecord& meta, RowId id, const std::vector<Value>& row);
    bool replace_indexes(TableRecord& meta, RowId id, const std::vector<Value>& old_row,
                         const std::vector<Value>& new_row);
    const IndexRecord* find_index(const TableRecord& meta, const std::string& index_name) const;

    Pager pager;
    std::map<std::string, TableRecord> tables;
    // Index name -> table name. Names are unique in the whole database.
    std::map<std::string, std::string> index_owner;
};

Database::Impl::TableRecord& Database::Impl::require(const std::string& name) {
    const auto found = tables.find(name);
    if (found == tables.end()) {
        throw ExecutionError("No such table: " + name);
    }
    return found->second;
}

const Database::Impl::TableRecord& Database::Impl::require(const std::string& name) const {
    const auto found = tables.find(name);
    if (found == tables.end()) {
        throw ExecutionError("No such table: " + name);
    }
    return found->second;
}

void Database::Impl::load_catalog() {
    std::size_t steps = 0;
    for (std::uint32_t page_id = pager.catalog_root(); page_id != 0;) {
        if (++steps > pager.page_count()) {
            throw StorageError("Page chain is cyclic");
        }
        const Page page = pager.read_page(page_id);
        const std::uint16_t count = slotted::slot_count(page);
        for (std::uint16_t slot = 0; slot < count; ++slot) {
            const slotted::Slot entry = slotted::read_slot(page, slot);
            if (entry.kind == slotted::Slot::Kind::Tombstone) {
                continue;
            }
            if (entry.kind != slotted::Slot::Kind::Record) {
                throw StorageError("Corrupt catalog page");
            }
            CatalogEntry decoded = decode_catalog_entry(entry.bytes.data(), entry.bytes.size());
            if (decoded.name.empty() || tables.find(decoded.name) != tables.end()) {
                throw StorageError("Corrupt catalog entry");
            }
            if ((decoded.head_page == 0) != (decoded.tail_page == 0)) {
                throw StorageError("Corrupt catalog entry");
            }
            TableRecord record;
            record.table.name = decoded.name;
            record.table.columns = std::move(decoded.columns);
            record.head_page = decoded.head_page;
            record.tail_page = decoded.tail_page;
            record.overflow_head = decoded.overflow_head;
            record.catalog_page = page_id;
            record.catalog_slot = slot;
            for (const IndexCatalog& index : decoded.indexes) {
                if (index.column >= record.table.columns.size() || index.root_page == 0 ||
                    index.root_page >= pager.page_count() ||
                    index_owner.find(index.name) != index_owner.end()) {
                    throw StorageError("Corrupt catalog entry");
                }
                const Page root = pager.read_page(index.root_page);
                if (root.read_u8(0) != kPageTypeIndex) {
                    throw StorageError("Corrupt catalog entry");
                }
                IndexRecord stored;
                stored.name = index.name;
                stored.column = index.column;
                stored.root_page = index.root_page;
                index_owner.emplace(stored.name, record.table.name);
                record.indexes.push_back(std::move(stored));
            }
            tables.emplace(record.table.name, std::move(record));
        }
        page_id = slotted::next_page(page);
    }
}

void Database::Impl::persist(const TableRecord& meta) {
    CatalogEntry entry;
    entry.name = meta.table.name;
    entry.columns = meta.table.columns;
    entry.head_page = meta.head_page;
    entry.tail_page = meta.tail_page;
    entry.overflow_head = meta.overflow_head;
    entry.indexes.reserve(meta.indexes.size());
    for (const IndexRecord& index : meta.indexes) {
        IndexCatalog stored;
        stored.name = index.name;
        stored.column = index.column;
        stored.root_page = index.root_page;
        entry.indexes.push_back(std::move(stored));
    }
    const std::vector<std::uint8_t> bytes = encode_catalog_entry(entry);
    Page page = pager.read_page(meta.catalog_page);
    if (!slotted::rewrite_slot(page, meta.catalog_slot, bytes.data(), bytes.size())) {
        throw StorageError("Failed to update the catalog");
    }
    pager.write_page(meta.catalog_page, page);
}

Database::Impl::SlotRef Database::Impl::append(std::uint32_t& head, std::uint32_t& tail,
                                              const char* what,
                                              const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > kMaxRecordBytes) {
        throw ExecutionError(too_big(what, bytes.size()));
    }

    auto place = [&](Page& page) {
        if (slotted::try_append_record(page, bytes.data(), bytes.size())) {
            return true;
        }
        if (slotted::next_page(page) == 0 && !slotted::has_live_slot(page)) {
            const std::uint32_t prev = slotted::prev_page(page);
            const std::uint8_t page_flags = slotted::flags(page);
            slotted::initialize_heap_page(page);
            slotted::set_prev_page(page, prev);
            slotted::set_flags(page, page_flags);
            return slotted::try_append_record(page, bytes.data(), bytes.size());
        }
        slotted::compact_heap_page(page);
        return slotted::try_append_record(page, bytes.data(), bytes.size());
    };

    if (head == 0) {
        const std::uint32_t id = pager.allocate_page();
        Page page;
        slotted::initialize_heap_page(page);
        if (!slotted::try_append_record(page, bytes.data(), bytes.size())) {
            pager.free_page(id);
            throw ExecutionError(too_big(what, bytes.size()));
        }
        pager.write_page(id, page);
        head = id;
        tail = id;
        return SlotRef{id, 0};
    }
    if (tail == 0 || tail >= pager.page_count()) {
        throw StorageError("Heap tail page is missing");
    }

    Page page = pager.read_page(tail);
    if (place(page)) {
        const std::uint16_t slot = static_cast<std::uint16_t>(slotted::slot_count(page) - 1);
        pager.write_page(tail, page);
        return SlotRef{tail, slot};
    }

    const std::uint32_t id = pager.allocate_page();
    Page fresh;
    slotted::initialize_heap_page(fresh);
    if (!slotted::try_append_record(fresh, bytes.data(), bytes.size())) {
        pager.free_page(id);
        throw ExecutionError(too_big(what, bytes.size()));
    }
    pager.write_page(id, fresh);
    slotted::set_next_page(page, id);
    pager.write_page(tail, page);
    tail = id;
    return SlotRef{id, 0};
}

void Database::Impl::free_chain(std::uint32_t head) {
    std::vector<std::uint32_t> ids;
    std::size_t steps = 0;
    for (std::uint32_t page_id = head; page_id != 0;) {
        if (++steps > pager.page_count()) {
            throw StorageError("Page chain is cyclic");
        }
        const Page page = pager.read_page(page_id);
        const std::uint32_t next = slotted::next_page(page);
        ids.push_back(page_id);
        page_id = next;
    }
    for (const std::uint32_t page_id : ids) {
        pager.free_page(page_id);
    }
}

bool Database::Impl::chain_contains(std::uint32_t head, std::uint32_t page_id) const {
    std::size_t steps = 0;
    for (std::uint32_t id = head; id != 0;) {
        if (id == page_id) {
            return true;
        }
        if (++steps > pager.page_count()) {
            throw StorageError("Page chain is cyclic");
        }
        const Page page = pager.read_page(id);
        id = slotted::next_page(page);
    }
    return false;
}

void Database::Impl::unlink_overflow(TableRecord& meta, std::uint32_t page_id) {
    Page page = pager.read_page(page_id);
    if ((slotted::flags(page) & kPageFlagOverflow) == 0) {
        throw StorageError("Broken forward pointer");
    }
    const std::uint32_t next = slotted::next_page(page);
    const std::uint32_t prev = slotted::prev_page(page);
    if (prev == 0) {
        meta.overflow_head = next;
    } else {
        Page previous = pager.read_page(prev);
        slotted::set_next_page(previous, next);
        pager.write_page(prev, previous);
    }
    if (next != 0) {
        Page following = pager.read_page(next);
        slotted::set_prev_page(following, prev);
        pager.write_page(next, following);
    }
    pager.free_page(page_id);
}

std::vector<std::uint8_t> Database::Impl::payload(std::uint32_t page_id, std::uint16_t slot) const {
    const Page page = pager.read_page(page_id);
    const slotted::Slot entry = slotted::read_slot(page, slot);
    if (entry.kind == slotted::Slot::Kind::Tombstone) {
        throw StorageError("Row is missing");
    }
    if (entry.kind == slotted::Slot::Kind::Record) {
        return entry.bytes;
    }
    if (entry.forward_page == 0 || entry.forward_page >= pager.page_count()) {
        throw StorageError("Broken forward pointer");
    }
    const Page overflow = pager.read_page(entry.forward_page);
    if ((slotted::flags(overflow) & kPageFlagOverflow) == 0) {
        throw StorageError("Broken forward pointer");
    }
    const slotted::Slot target = slotted::read_slot(overflow, entry.forward_slot);
    if (target.kind != slotted::Slot::Kind::Record) {
        throw StorageError("Broken forward pointer");
    }
    return target.bytes;
}

IndexKey Database::Impl::make_key(const Value& value, RowId id) const {
    IndexKey key;
    key.column = encode_index_column(value);
    key.page_id = id.page_id;
    key.slot = id.slot;
    return key;
}

void Database::Impl::ensure_index_values(const TableRecord& meta, const std::vector<Value>& row) const {
    for (const IndexRecord& index : meta.indexes) {
        const Value& value = row[index.column];
        if (value.type() == DataType::Text && value.text().size() > kMaxIndexKeyBytes) {
            throw ExecutionError("Value for indexed column " + meta.table.columns[index.column].name +
                                 " exceeds " + std::to_string(kMaxIndexKeyBytes) + " bytes");
        }
    }
}

bool Database::Impl::insert_indexes(TableRecord& meta, RowId id, const std::vector<Value>& row) {
    bool changed = false;
    for (IndexRecord& index : meta.indexes) {
        const std::uint32_t before = index.root_page;
        std::uint32_t root = index.root_page;
        btree_insert(pager, root, make_key(row[index.column], id));
        index.root_page = root;
        changed = changed || root != before;
    }
    return changed;
}

bool Database::Impl::remove_indexes(TableRecord& meta, RowId id, const std::vector<Value>& row) {
    bool changed = false;
    for (IndexRecord& index : meta.indexes) {
        const std::uint32_t before = index.root_page;
        std::uint32_t root = index.root_page;
        btree_remove(pager, root, make_key(row[index.column], id));
        index.root_page = root;
        changed = changed || root != before;
    }
    return changed;
}

bool Database::Impl::replace_indexes(TableRecord& meta, RowId id, const std::vector<Value>& old_row,
                                     const std::vector<Value>& new_row) {
    bool changed = false;
    for (IndexRecord& index : meta.indexes) {
        const IndexKey old_key = make_key(old_row[index.column], id);
        const IndexKey new_key = make_key(new_row[index.column], id);
        if (compare_index_keys(old_key, new_key) == 0) {
            continue;
        }
        const std::uint32_t before = index.root_page;
        std::uint32_t root = index.root_page;
        btree_remove(pager, root, old_key);
        btree_insert(pager, root, new_key);
        index.root_page = root;
        changed = changed || root != before;
    }
    return changed;
}

const Database::Impl::IndexRecord* Database::Impl::find_index(const TableRecord& meta,
                                                             const std::string& index_name) const {
    for (const IndexRecord& index : meta.indexes) {
        if (index.name == index_name) {
            return &index;
        }
    }
    return nullptr;
}

Database::Database() : impl_(std::make_unique<Impl>(Pager::open_memory())) {
    impl_->load_catalog();
}

Database::Database(std::string path) : impl_(std::make_unique<Impl>(Pager::open_file(path))) {
    impl_->load_catalog();
}

Database::~Database() = default;

void Database::create_table(std::string name, std::vector<ColumnDefinition> columns) {
    if (impl_->tables.find(name) != impl_->tables.end()) {
        throw ExecutionError("Table already exists: " + name);
    }
    if (columns.empty()) {
        throw ExecutionError("Table " + name + " must have at least one column");
    }
    ensure_unique_columns(columns);

    CatalogEntry entry;
    entry.name = name;
    entry.columns = columns;
    const std::vector<std::uint8_t> bytes = encode_catalog_entry(entry);
    std::uint32_t head = impl_->pager.catalog_root();
    std::uint32_t tail = impl_->pager.catalog_tail();
    const Impl::SlotRef slot = impl_->append(head, tail, "Table definition", bytes);
    if (head != impl_->pager.catalog_root()) {
        throw StorageError("Catalog root changed");
    }
    if (tail != impl_->pager.catalog_tail()) {
        impl_->pager.set_catalog_tail(tail);
    }

    Impl::TableRecord record;
    record.table.name = name;
    record.table.columns = std::move(columns);
    record.catalog_page = slot.page;
    record.catalog_slot = slot.slot;
    impl_->tables.emplace(std::move(name), std::move(record));
    impl_->pager.flush();
}

void Database::drop_table(const std::string& name) {
    Impl::TableRecord meta = impl_->require(name);
    std::vector<std::uint32_t> index_roots;
    index_roots.reserve(meta.indexes.size());
    for (const Impl::IndexRecord& index : meta.indexes) {
        index_roots.push_back(index.root_page);
        impl_->index_owner.erase(index.name);
    }
    impl_->free_chain(meta.head_page);
    impl_->free_chain(meta.overflow_head);
    Page catalog = impl_->pager.read_page(meta.catalog_page);
    slotted::tombstone_slot(catalog, meta.catalog_slot);
    impl_->pager.write_page(meta.catalog_page, catalog);
    impl_->tables.erase(name);
    for (const std::uint32_t root : index_roots) {
        btree_destroy(impl_->pager, root);
    }
    impl_->pager.flush();
}

std::vector<std::string> Database::table_names() const {
    std::vector<std::string> names;
    names.reserve(impl_->tables.size());
    for (const auto& entry : impl_->tables) {
        names.push_back(entry.first);
    }
    return names;
}

Table& Database::require_table(const std::string& name) {
    return impl_->require(name).table;
}

const Table& Database::require_table(const std::string& name) const {
    return impl_->require(name).table;
}

std::size_t Database::row_count(const std::string& name) const {
    return scan_rows(name).size();
}

std::vector<StoredRow> Database::scan_rows(const std::string& name) const {
    const Impl::TableRecord& meta = impl_->require(name);
    std::vector<StoredRow> rows;
    std::size_t steps = 0;
    for (std::uint32_t page_id = meta.head_page; page_id != 0;) {
        if (++steps > impl_->pager.page_count()) {
            throw StorageError("Page chain is cyclic");
        }
        const Page page = impl_->pager.read_page(page_id);
        const std::uint16_t count = slotted::slot_count(page);
        for (std::uint16_t slot = 0; slot < count; ++slot) {
            const slotted::Slot entry = slotted::read_slot(page, slot);
            if (entry.kind == slotted::Slot::Kind::Tombstone) {
                continue;
            }
            StoredRow row;
            row.id.page_id = page_id;
            row.id.slot = slot;
            row.values = decode_checked(meta.table, impl_->payload(page_id, slot));
            rows.push_back(std::move(row));
        }
        page_id = slotted::next_page(page);
    }
    return rows;
}

void Database::insert_row(const std::string& name, std::vector<Value> row) {
    Impl::TableRecord& meta = impl_->require(name);
    ensure_row(meta.table, row);
    impl_->ensure_index_values(meta, row);
    const std::vector<std::uint8_t> bytes = encode_values(row);
    std::uint32_t head = meta.head_page;
    std::uint32_t tail = meta.tail_page;
    const Impl::SlotRef placed = impl_->append(head, tail, "Row", bytes);
    const bool moved = head != meta.head_page || tail != meta.tail_page;
    meta.head_page = head;
    meta.tail_page = tail;
    const RowId id{placed.page, placed.slot};
    const bool indexes_changed = impl_->insert_indexes(meta, id, row);
    if (moved || indexes_changed) {
        impl_->persist(meta);
    }
    impl_->pager.flush();
}

void Database::update_row(const std::string& name, RowId id, std::vector<Value> row) {
    Impl::TableRecord& meta = impl_->require(name);
    ensure_row(meta.table, row);
    impl_->ensure_index_values(meta, row);
    if (!impl_->chain_contains(meta.head_page, id.page_id)) {
        throw StorageError("Row is not in this table");
    }
    const std::vector<Value> previous = decode_checked(meta.table, impl_->payload(id.page_id, id.slot));
    const std::vector<std::uint8_t> bytes = encode_values(row);
    if (bytes.size() > kMaxRecordBytes) {
        throw ExecutionError(too_big("Row", bytes.size()));
    }
    const auto maintain = [&]() {
        if (impl_->replace_indexes(meta, id, previous, row)) {
            impl_->persist(meta);
        }
    };

    Page page = impl_->pager.read_page(id.page_id);
    const slotted::Slot entry = slotted::read_slot(page, id.slot);
    if (entry.kind == slotted::Slot::Kind::Tombstone) {
        throw StorageError("Row is missing");
    }
    if (entry.kind == slotted::Slot::Kind::Forward) {
        Page overflow = impl_->pager.read_page(entry.forward_page);
        if ((slotted::flags(overflow) & kPageFlagOverflow) == 0 ||
            !slotted::rewrite_slot(overflow, entry.forward_slot, bytes.data(), bytes.size())) {
            throw StorageError("Failed to update an overflow row");
        }
        impl_->pager.write_page(entry.forward_page, overflow);
        maintain();
        impl_->pager.flush();
        return;
    }
    if (slotted::rewrite_slot(page, id.slot, bytes.data(), bytes.size())) {
        impl_->pager.write_page(id.page_id, page);
        maintain();
        impl_->pager.flush();
        return;
    }

    const std::uint32_t overflow_id = impl_->pager.allocate_page();
    Page overflow;
    slotted::initialize_heap_page(overflow);
    slotted::set_flags(overflow, kPageFlagOverflow);
    if (!slotted::try_append_record(overflow, bytes.data(), bytes.size())) {
        impl_->pager.free_page(overflow_id);
        throw ExecutionError(too_big("Row", bytes.size()));
    }
    const std::uint32_t old_head = meta.overflow_head;
    slotted::set_next_page(overflow, old_head);
    slotted::set_prev_page(overflow, 0);
    impl_->pager.write_page(overflow_id, overflow);
    if (old_head != 0) {
        Page previous_head = impl_->pager.read_page(old_head);
        slotted::set_prev_page(previous_head, overflow_id);
        impl_->pager.write_page(old_head, previous_head);
    }
    // `page` was read before allocate_page. allocate does not touch this page.
    slotted::set_forward_slot(page, id.slot, overflow_id, 0);
    impl_->pager.write_page(id.page_id, page);
    meta.overflow_head = overflow_id;
    impl_->persist(meta);
    maintain();
    impl_->pager.flush();
}

void Database::delete_row(const std::string& name, RowId id) {
    Impl::TableRecord& meta = impl_->require(name);
    if (!impl_->chain_contains(meta.head_page, id.page_id)) {
        throw StorageError("Row is not in this table");
    }
    const std::vector<Value> previous = decode_checked(meta.table, impl_->payload(id.page_id, id.slot));
    const bool indexes_changed = impl_->remove_indexes(meta, id, previous);
    Page page = impl_->pager.read_page(id.page_id);
    const slotted::Slot entry = slotted::read_slot(page, id.slot);
    if (entry.kind == slotted::Slot::Kind::Tombstone) {
        throw StorageError("Row is missing");
    }
    const bool forwarded = entry.kind == slotted::Slot::Kind::Forward;
    if (forwarded) {
        impl_->unlink_overflow(meta, entry.forward_page);
    }
    slotted::tombstone_slot(page, id.slot);
    impl_->pager.write_page(id.page_id, page);
    if (forwarded || indexes_changed) {
        impl_->persist(meta);
    }
    impl_->pager.flush();
}

void Database::clear_rows(const std::string& name) {
    Impl::TableRecord& meta = impl_->require(name);
    std::vector<std::uint32_t> fresh_roots;
    fresh_roots.reserve(meta.indexes.size());
    try {
        for (std::size_t i = 0; i < meta.indexes.size(); ++i) {
            fresh_roots.push_back(btree_create(impl_->pager));
        }
    } catch (...) {
        for (const std::uint32_t root : fresh_roots) {
            btree_destroy(impl_->pager, root);
        }
        throw;
    }
    std::vector<std::uint32_t> old_roots;
    old_roots.reserve(meta.indexes.size());
    for (std::size_t i = 0; i < meta.indexes.size(); ++i) {
        old_roots.push_back(meta.indexes[i].root_page);
        meta.indexes[i].root_page = fresh_roots[i];
    }
    impl_->free_chain(meta.head_page);
    impl_->free_chain(meta.overflow_head);
    meta.head_page = 0;
    meta.tail_page = 0;
    meta.overflow_head = 0;
    impl_->persist(meta);
    for (const std::uint32_t root : old_roots) {
        btree_destroy(impl_->pager, root);
    }
    impl_->pager.flush();
}

void Database::create_index(std::string index_name, const std::string& table, const std::string& column) {
    if (impl_->index_owner.find(index_name) != impl_->index_owner.end()) {
        throw ExecutionError("Index already exists: " + index_name);
    }
    Impl::TableRecord& meta = impl_->require(table);
    const std::optional<std::size_t> column_index = meta.table.index_of(column);
    if (!column_index.has_value()) {
        throw ExecutionError("Unknown column: " + column);
    }
    if (*column_index > std::numeric_limits<std::uint16_t>::max()) {
        throw ExecutionError("Too many columns to index");
    }

    Impl::IndexRecord index;
    index.name = index_name;
    index.column = static_cast<std::uint16_t>(*column_index);
    index.root_page = btree_create(impl_->pager);
    try {
        const std::string& column_name = meta.table.columns[index.column].name;
        for (const StoredRow& stored : scan_rows(table)) {
            const Value& value = stored.values[index.column];
            if (value.type() == DataType::Text && value.text().size() > kMaxIndexKeyBytes) {
                throw ExecutionError("Value for indexed column " + column_name + " exceeds " +
                                     std::to_string(kMaxIndexKeyBytes) + " bytes");
            }
            std::uint32_t root = index.root_page;
            btree_insert(impl_->pager, root, impl_->make_key(value, stored.id));
            index.root_page = root;
        }
        Impl::TableRecord copy = meta;
        copy.indexes.push_back(index);
        impl_->persist(copy);
    } catch (...) {
        btree_destroy(impl_->pager, index.root_page);
        throw;
    }
    meta.indexes.push_back(index);
    impl_->index_owner.emplace(std::move(index_name), table);
    impl_->pager.flush();
}

void Database::drop_index(const std::string& index_name) {
    const auto owner = impl_->index_owner.find(index_name);
    if (owner == impl_->index_owner.end()) {
        throw ExecutionError("No such index: " + index_name);
    }
    Impl::TableRecord& meta = impl_->require(owner->second);
    const Impl::IndexRecord* found = impl_->find_index(meta, index_name);
    if (found == nullptr) {
        throw StorageError("Corrupt catalog entry");
    }
    const std::uint32_t root = found->root_page;
    Impl::TableRecord copy = meta;
    copy.indexes.erase(std::remove_if(copy.indexes.begin(), copy.indexes.end(),
                                      [&](const Impl::IndexRecord& index) {
                                          return index.name == index_name;
                                      }),
                       copy.indexes.end());
    impl_->persist(copy);
    btree_destroy(impl_->pager, root);
    meta.indexes = std::move(copy.indexes);
    impl_->index_owner.erase(owner);
    impl_->pager.flush();
}

std::vector<std::string> Database::index_names() const {
    std::vector<std::string> names;
    names.reserve(impl_->index_owner.size());
    for (const auto& entry : impl_->index_owner) {
        names.push_back(entry.first);
    }
    return names;
}

std::optional<std::string> Database::index_for_column(const std::string& table,
                                                     const std::string& column) const {
    const Impl::TableRecord& meta = impl_->require(table);
    const std::optional<std::size_t> column_index = meta.table.index_of(column);
    if (!column_index.has_value()) {
        return std::nullopt;
    }
    for (const Impl::IndexRecord& index : meta.indexes) {
        if (index.column == *column_index) {
            return index.name;
        }
    }
    return std::nullopt;
}

std::vector<StoredRow> Database::scan_index(const std::string& table, const std::string& index_name,
                                            const IndexRange& range) const {
    const Impl::TableRecord& meta = impl_->require(table);
    const Impl::IndexRecord* index = impl_->find_index(meta, index_name);
    if (index == nullptr) {
        throw ExecutionError("No such index: " + index_name);
    }
    const DataType type = meta.table.columns[index->column].type;
    IndexBound low;
    IndexBound high;
    if (!range.low_unbounded) {
        if (range.low.type() != type) {
            throw ExecutionError("Type mismatch for column " + meta.table.columns[index->column].name);
        }
        low.unbounded = false;
        low.inclusive = range.low_inclusive;
        low.column = encode_index_column(range.low);
    }
    if (!range.high_unbounded) {
        if (range.high.type() != type) {
            throw ExecutionError("Type mismatch for column " + meta.table.columns[index->column].name);
        }
        high.unbounded = false;
        high.inclusive = range.high_inclusive;
        high.column = encode_index_column(range.high);
    }

    const std::vector<RowId> ids = btree_scan(impl_->pager, index->root_page, low, high);
    std::vector<StoredRow> rows;
    rows.reserve(ids.size());
    for (const RowId id : ids) {
        StoredRow row;
        row.id = id;
        row.values = decode_checked(meta.table, impl_->payload(id.page_id, id.slot));
        rows.push_back(std::move(row));
    }
    return rows;
}

void Database::reset_page_reads() {
    impl_->pager.reset_read_count();
}

std::uint64_t Database::page_reads() const {
    return impl_->pager.read_count();
}

}  // namespace minidb
