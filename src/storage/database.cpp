#include "minidb/catalog.hpp"

#include "minidb/execution_error.hpp"
#include "minidb/pager.hpp"
#include "minidb/record.hpp"
#include "minidb/storage_error.hpp"
#include "slotted_page.hpp"

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
    struct TableRecord {
        Table table;
        std::uint32_t head_page = 0;
        std::uint32_t tail_page = 0;
        std::uint32_t overflow_head = 0;
        std::uint32_t catalog_page = 0;
        std::uint16_t catalog_slot = 0;
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

    Pager pager;
    std::map<std::string, TableRecord> tables;
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
    impl_->free_chain(meta.head_page);
    impl_->free_chain(meta.overflow_head);
    Page catalog = impl_->pager.read_page(meta.catalog_page);
    slotted::tombstone_slot(catalog, meta.catalog_slot);
    impl_->pager.write_page(meta.catalog_page, catalog);
    impl_->tables.erase(name);
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
    const std::vector<std::uint8_t> bytes = encode_values(row);
    std::uint32_t head = meta.head_page;
    std::uint32_t tail = meta.tail_page;
    impl_->append(head, tail, "Row", bytes);
    const bool moved = head != meta.head_page || tail != meta.tail_page;
    meta.head_page = head;
    meta.tail_page = tail;
    if (moved) {
        impl_->persist(meta);
    }
    impl_->pager.flush();
}

void Database::update_row(const std::string& name, RowId id, std::vector<Value> row) {
    Impl::TableRecord& meta = impl_->require(name);
    ensure_row(meta.table, row);
    if (!impl_->chain_contains(meta.head_page, id.page_id)) {
        throw StorageError("Row is not in this table");
    }
    const std::vector<std::uint8_t> bytes = encode_values(row);
    if (bytes.size() > kMaxRecordBytes) {
        throw ExecutionError(too_big("Row", bytes.size()));
    }

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
        impl_->pager.flush();
        return;
    }
    if (slotted::rewrite_slot(page, id.slot, bytes.data(), bytes.size())) {
        impl_->pager.write_page(id.page_id, page);
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
    impl_->pager.flush();
}

void Database::delete_row(const std::string& name, RowId id) {
    Impl::TableRecord& meta = impl_->require(name);
    if (!impl_->chain_contains(meta.head_page, id.page_id)) {
        throw StorageError("Row is not in this table");
    }
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
    if (forwarded) {
        impl_->persist(meta);
    }
    impl_->pager.flush();
}

void Database::clear_rows(const std::string& name) {
    Impl::TableRecord& meta = impl_->require(name);
    impl_->free_chain(meta.head_page);
    impl_->free_chain(meta.overflow_head);
    meta.head_page = 0;
    meta.tail_page = 0;
    meta.overflow_head = 0;
    impl_->persist(meta);
    impl_->pager.flush();
}

}  // namespace minidb
