#pragma once

#include "minidb/page.hpp"

#include <cstdint>
#include <vector>

namespace minidb {
namespace slotted {

// One slot in a heap page. Record bytes are copied out so the caller can
// rewrite the page without dangling into its old contents.
struct Slot {
    enum class Kind { Tombstone, Record, Forward };

    Kind kind = Kind::Tombstone;
    std::uint16_t offset = 0;
    std::uint16_t length = 0;
    std::uint32_t forward_page = 0;
    std::uint16_t forward_slot = 0;
    std::vector<std::uint8_t> bytes;
};

void initialize_heap_page(Page& page);

std::uint16_t slot_count(const Page& page);
std::uint32_t next_page(const Page& page);
void set_next_page(Page& page, std::uint32_t page_id);
std::uint32_t prev_page(const Page& page);
void set_prev_page(Page& page, std::uint32_t page_id);
std::uint8_t flags(const Page& page);
void set_flags(Page& page, std::uint8_t value);

Slot read_slot(const Page& page, std::uint16_t index);
bool has_live_slot(const Page& page);

// False when the record does not fit. The page is unchanged in that case.
bool try_append_record(Page& page, const std::uint8_t* data, std::size_t size);

// Replaces a live record slot. False when the new bytes do not fit even
// after packing the page. A false return leaves `page` unchanged.
// Forward slots are not rewritten here; the caller updates the overflow page.
bool rewrite_slot(Page& page, std::uint16_t index, const std::uint8_t* data, std::size_t size);

void tombstone_slot(Page& page, std::uint16_t index);
void set_forward_slot(Page& page, std::uint16_t index, std::uint32_t target_page,
                      std::uint16_t target_slot);

// Packs live records toward the front of the page. Slot indexes stay put,
// so a RowId (page, slot) remains valid. Tombstone payloads are dropped.
void compact_heap_page(Page& page);

}  // namespace slotted
}  // namespace minidb
