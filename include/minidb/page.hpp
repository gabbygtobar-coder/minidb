#pragma once

#include <cstddef>
#include <cstdint>

namespace minidb {

// Fixed page size for every database file. Documented in docs/storage.md.
inline constexpr std::size_t kPageSize = 4096;

inline constexpr std::uint8_t kPageTypeHeap = 1;
inline constexpr std::uint8_t kPageTypeFree = 2;
// B+ tree node. Layout is docs/index.md, not a slotted heap page.
inline constexpr std::uint8_t kPageTypeIndex = 3;

// Heap pages (everything except the file header) use a 16-byte header and
// an 8-byte slot directory entry. One record has to fit in the bytes that
// remain on an otherwise empty page.
inline constexpr std::size_t kHeapHeaderSize = 16;
inline constexpr std::size_t kSlotSize = 8;
inline constexpr std::size_t kMaxRecordBytes = kPageSize - kHeapHeaderSize - kSlotSize;

// Bit 0 of the heap-page flags byte. Overflow pages hold a row that no
// longer fit beside its neighbors. Primary scans do not follow them.
inline constexpr std::uint8_t kPageFlagOverflow = 0x01;

// offset == 0 is a tombstone. offset == this value is a forward pointer
// stored in the slot itself (the record bytes live on an overflow page).
inline constexpr std::uint16_t kSlotOffsetForward = 0xFFFF;

// One fixed-size page. Integers and IEEE-754 doubles are stored
// little-endian on purpose; nothing here memcpy's a C++ object to disk.
class Page {
  public:
    Page();

    std::uint8_t* data() noexcept;
    const std::uint8_t* data() const noexcept;

    void read_bytes(std::size_t offset, void* dst, std::size_t n) const;
    void write_bytes(std::size_t offset, const void* src, std::size_t n);

    std::uint8_t read_u8(std::size_t offset) const;
    std::uint16_t read_u16(std::size_t offset) const;
    std::uint32_t read_u32(std::size_t offset) const;
    std::uint64_t read_u64(std::size_t offset) const;
    double read_f64(std::size_t offset) const;

    void write_u8(std::size_t offset, std::uint8_t value);
    void write_u16(std::size_t offset, std::uint16_t value);
    void write_u32(std::size_t offset, std::uint32_t value);
    void write_u64(std::size_t offset, std::uint64_t value);
    void write_f64(std::size_t offset, double value);

  private:
    void check(std::size_t offset, std::size_t n) const;

    std::uint8_t bytes_[kPageSize];
};

}  // namespace minidb
