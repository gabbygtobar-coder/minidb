#include "slotted_page.hpp"

#include "minidb/storage_error.hpp"

namespace minidb {
namespace slotted {
namespace {

constexpr std::size_t kTypeOffset = 0;
constexpr std::size_t kFlagsOffset = 1;
constexpr std::size_t kSlotCountOffset = 2;
constexpr std::size_t kFreeStartOffset = 4;
constexpr std::size_t kNextOffset = 8;
constexpr std::size_t kPrevOffset = 12;

std::size_t slot_position(std::uint16_t index) {
    return kPageSize - (static_cast<std::size_t>(index) + 1) * kSlotSize;
}

std::size_t directory_start(std::uint16_t count) {
    return kPageSize - static_cast<std::size_t>(count) * kSlotSize;
}

void require_heap(const Page& page) {
    if (page.read_u8(kTypeOffset) != kPageTypeHeap) {
        throw StorageError("Expected a heap page");
    }
}

void check_directory(std::uint16_t count, std::uint16_t free_start) {
    const std::size_t directory = static_cast<std::size_t>(count) * kSlotSize;
    if (directory > kPageSize - kHeapHeaderSize) {
        throw StorageError("Corrupt slotted page");
    }
    if (free_start < kHeapHeaderSize || free_start > directory_start(count)) {
        throw StorageError("Corrupt slotted page");
    }
}

void write_slot_raw(Page& page, std::uint16_t index, std::uint16_t offset, std::uint16_t length,
                    std::uint32_t extra) {
    const std::size_t at = slot_position(index);
    page.write_u16(at, offset);
    page.write_u16(at + 2, length);
    page.write_u32(at + 4, extra);
}

void rebuild(Page& page, const std::vector<Slot>& slots) {
    const std::uint8_t type = page.read_u8(kTypeOffset);
    const std::uint8_t page_flags = page.read_u8(kFlagsOffset);
    const std::uint32_t next = page.read_u32(kNextOffset);
    const std::uint32_t prev = page.read_u32(kPrevOffset);

    std::size_t payload = 0;
    for (const Slot& slot : slots) {
        if (slot.kind == Slot::Kind::Record) {
            payload += slot.bytes.size();
        }
    }
    if (slots.size() > static_cast<std::size_t>(UINT16_MAX) ||
        kHeapHeaderSize + payload + slots.size() * kSlotSize > kPageSize) {
        throw StorageError("Corrupt slotted page");
    }

    Page rebuilt;
    initialize_heap_page(rebuilt);
    rebuilt.write_u8(kTypeOffset, type);
    rebuilt.write_u8(kFlagsOffset, page_flags);
    rebuilt.write_u32(kNextOffset, next);
    rebuilt.write_u32(kPrevOffset, prev);

    auto cursor = static_cast<std::uint16_t>(kHeapHeaderSize);
    const auto count = static_cast<std::uint16_t>(slots.size());
    for (std::uint16_t index = 0; index < count; ++index) {
        const Slot& slot = slots[index];
        if (slot.kind == Slot::Kind::Record) {
            const auto length = static_cast<std::uint16_t>(slot.bytes.size());
            rebuilt.write_bytes(cursor, slot.bytes.data(), length);
            write_slot_raw(rebuilt, index, cursor, length, 0);
            cursor = static_cast<std::uint16_t>(cursor + length);
        } else if (slot.kind == Slot::Kind::Forward) {
            write_slot_raw(rebuilt, index, kSlotOffsetForward, slot.forward_slot, slot.forward_page);
        } else {
            write_slot_raw(rebuilt, index, 0, 0, 0);
        }
    }
    rebuilt.write_u16(kSlotCountOffset, count);
    rebuilt.write_u16(kFreeStartOffset, cursor);
    page = rebuilt;
}

}  // namespace

void initialize_heap_page(Page& page) {
    page = Page{};
    page.write_u8(kTypeOffset, kPageTypeHeap);
    page.write_u16(kSlotCountOffset, 0);
    page.write_u16(kFreeStartOffset, static_cast<std::uint16_t>(kHeapHeaderSize));
}

std::uint16_t slot_count(const Page& page) {
    require_heap(page);
    const std::uint16_t count = page.read_u16(kSlotCountOffset);
    check_directory(count, page.read_u16(kFreeStartOffset));
    return count;
}

std::uint32_t next_page(const Page& page) {
    require_heap(page);
    return page.read_u32(kNextOffset);
}

void set_next_page(Page& page, std::uint32_t page_id) {
    require_heap(page);
    page.write_u32(kNextOffset, page_id);
}

std::uint32_t prev_page(const Page& page) {
    require_heap(page);
    return page.read_u32(kPrevOffset);
}

void set_prev_page(Page& page, std::uint32_t page_id) {
    require_heap(page);
    page.write_u32(kPrevOffset, page_id);
}

std::uint8_t flags(const Page& page) {
    require_heap(page);
    return page.read_u8(kFlagsOffset);
}

void set_flags(Page& page, std::uint8_t value) {
    require_heap(page);
    page.write_u8(kFlagsOffset, value);
}

Slot read_slot(const Page& page, std::uint16_t index) {
    const std::uint16_t count = slot_count(page);
    if (index >= count) {
        throw StorageError("Slot index out of range");
    }
    const std::size_t at = slot_position(index);
    Slot slot;
    slot.offset = page.read_u16(at);
    slot.length = page.read_u16(at + 2);
    const std::uint32_t extra = page.read_u32(at + 4);
    if (slot.offset == 0) {
        slot.kind = Slot::Kind::Tombstone;
        return slot;
    }
    if (slot.offset == kSlotOffsetForward) {
        slot.kind = Slot::Kind::Forward;
        slot.forward_page = extra;
        slot.forward_slot = slot.length;
        return slot;
    }
    const std::size_t end = static_cast<std::size_t>(slot.offset) + slot.length;
    if (slot.offset < kHeapHeaderSize || end > directory_start(count)) {
        throw StorageError("Corrupt slotted page");
    }
    slot.kind = Slot::Kind::Record;
    slot.bytes.resize(slot.length);
    if (slot.length != 0) {
        page.read_bytes(slot.offset, slot.bytes.data(), slot.length);
    }
    return slot;
}

bool has_live_slot(const Page& page) {
    const std::uint16_t count = slot_count(page);
    for (std::uint16_t index = 0; index < count; ++index) {
        if (read_slot(page, index).kind != Slot::Kind::Tombstone) {
            return true;
        }
    }
    return false;
}

bool try_append_record(Page& page, const std::uint8_t* data, std::size_t size) {
    if (size > kMaxRecordBytes) {
        return false;
    }
    const std::uint16_t count = slot_count(page);
    const std::uint16_t free_start = page.read_u16(kFreeStartOffset);
    const std::size_t directory = directory_start(static_cast<std::uint16_t>(count + 1));
    if (directory < free_start || directory - free_start < size) {
        return false;
    }
    if (data == nullptr && size != 0) {
        throw StorageError("Missing record bytes");
    }
    page.write_bytes(free_start, data, size);
    write_slot_raw(page, count, free_start, static_cast<std::uint16_t>(size), 0);
    page.write_u16(kSlotCountOffset, static_cast<std::uint16_t>(count + 1));
    page.write_u16(kFreeStartOffset, static_cast<std::uint16_t>(free_start + size));
    return true;
}

bool rewrite_slot(Page& page, std::uint16_t index, const std::uint8_t* data, std::size_t size) {
    if (size > kMaxRecordBytes) {
        return false;
    }
    if (data == nullptr && size != 0) {
        throw StorageError("Missing record bytes");
    }
    const std::uint16_t count = slot_count(page);
    if (index >= count) {
        throw StorageError("Slot index out of range");
    }
    const Slot current = read_slot(page, index);
    if (current.kind != Slot::Kind::Record) {
        throw StorageError("Cannot rewrite an empty slot");
    }

    if (size <= current.length) {
        page.write_bytes(current.offset, data, size);
        write_slot_raw(page, index, current.offset, static_cast<std::uint16_t>(size), 0);
        return true;
    }

    const std::uint16_t free_start = page.read_u16(kFreeStartOffset);
    const std::size_t directory = directory_start(count);
    if (free_start <= directory && directory - free_start >= size) {
        page.write_bytes(free_start, data, size);
        write_slot_raw(page, index, free_start, static_cast<std::uint16_t>(size), 0);
        page.write_u16(kFreeStartOffset, static_cast<std::uint16_t>(free_start + size));
        return true;
    }

    std::vector<Slot> slots;
    slots.reserve(count);
    std::size_t payload = size;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (i == index) {
            Slot replacement;
            replacement.kind = Slot::Kind::Record;
            replacement.bytes.assign(data, data + size);
            slots.push_back(std::move(replacement));
            continue;
        }
        Slot slot = read_slot(page, i);
        if (slot.kind == Slot::Kind::Record) {
            payload += slot.bytes.size();
        }
        slots.push_back(std::move(slot));
    }
    if (kHeapHeaderSize + payload + static_cast<std::size_t>(count) * kSlotSize > kPageSize) {
        return false;
    }
    rebuild(page, slots);
    return true;
}

void tombstone_slot(Page& page, std::uint16_t index) {
    const std::uint16_t count = slot_count(page);
    if (index >= count) {
        throw StorageError("Slot index out of range");
    }
    write_slot_raw(page, index, 0, 0, 0);
}

void set_forward_slot(Page& page, std::uint16_t index, std::uint32_t target_page,
                      std::uint16_t target_slot) {
    const std::uint16_t count = slot_count(page);
    if (index >= count) {
        throw StorageError("Slot index out of range");
    }
    write_slot_raw(page, index, kSlotOffsetForward, target_slot, target_page);
}

void compact_heap_page(Page& page) {
    const std::uint16_t count = slot_count(page);
    std::vector<Slot> slots;
    slots.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        slots.push_back(read_slot(page, index));
    }
    rebuild(page, slots);
}

}  // namespace slotted
}  // namespace minidb
