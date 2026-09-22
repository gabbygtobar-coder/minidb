#include "minidb/page.hpp"

#include "minidb/storage_error.hpp"

#include <cstring>
#include <limits>

namespace minidb {
namespace {

void store_le(std::uint8_t* dst, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        dst[i] = static_cast<std::uint8_t>(value & 0xFFu);
        value >>= 8;
    }
}

std::uint64_t load_le(const std::uint8_t* src, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        value |= static_cast<std::uint64_t>(src[i]) << (8 * i);
    }
    return value;
}

}  // namespace

Page::Page() {
    std::memset(bytes_, 0, kPageSize);
}

std::uint8_t* Page::data() noexcept {
    return bytes_;
}

const std::uint8_t* Page::data() const noexcept {
    return bytes_;
}

void Page::check(std::size_t offset, std::size_t n) const {
    if (n > kPageSize || offset > kPageSize - n) {
        throw StorageError("Page offset out of range");
    }
}

void Page::read_bytes(std::size_t offset, void* dst, std::size_t n) const {
    check(offset, n);
    if (n == 0) {
        return;
    }
    std::memcpy(dst, bytes_ + offset, n);
}

void Page::write_bytes(std::size_t offset, const void* src, std::size_t n) {
    check(offset, n);
    if (n == 0) {
        return;
    }
    std::memcpy(bytes_ + offset, src, n);
}

std::uint8_t Page::read_u8(std::size_t offset) const {
    check(offset, 1);
    return bytes_[offset];
}

std::uint16_t Page::read_u16(std::size_t offset) const {
    std::uint8_t raw[2];
    read_bytes(offset, raw, 2);
    return static_cast<std::uint16_t>(load_le(raw, 2));
}

std::uint32_t Page::read_u32(std::size_t offset) const {
    std::uint8_t raw[4];
    read_bytes(offset, raw, 4);
    return static_cast<std::uint32_t>(load_le(raw, 4));
}

std::uint64_t Page::read_u64(std::size_t offset) const {
    std::uint8_t raw[8];
    read_bytes(offset, raw, 8);
    return load_le(raw, 8);
}

double Page::read_f64(std::size_t offset) const {
    static_assert(sizeof(double) == 8, "MiniDB stores IEEE-754 binary64");
    static_assert(std::numeric_limits<double>::is_iec559, "IEEE-754 double required");
    const std::uint64_t bits = read_u64(offset);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void Page::write_u8(std::size_t offset, std::uint8_t value) {
    check(offset, 1);
    bytes_[offset] = value;
}

void Page::write_u16(std::size_t offset, std::uint16_t value) {
    std::uint8_t raw[2];
    store_le(raw, value, 2);
    write_bytes(offset, raw, 2);
}

void Page::write_u32(std::size_t offset, std::uint32_t value) {
    std::uint8_t raw[4];
    store_le(raw, value, 4);
    write_bytes(offset, raw, 4);
}

void Page::write_u64(std::size_t offset, std::uint64_t value) {
    std::uint8_t raw[8];
    store_le(raw, value, 8);
    write_bytes(offset, raw, 8);
}

void Page::write_f64(std::size_t offset, double value) {
    static_assert(sizeof(double) == 8, "MiniDB stores IEEE-754 binary64");
    static_assert(std::numeric_limits<double>::is_iec559, "IEEE-754 double required");
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_u64(offset, bits);
}

}  // namespace minidb
