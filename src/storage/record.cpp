#include "minidb/record.hpp"

#include "minidb/storage_error.hpp"
#include "minidb/value.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace minidb {
namespace {

constexpr std::uint8_t kTagInt = 1;
constexpr std::uint8_t kTagText = 2;
constexpr std::uint8_t kTagBoolean = 3;
constexpr std::uint8_t kTagFloat = 4;

std::uint8_t type_tag(DataType type) {
    switch (type) {
        case DataType::Int:
            return kTagInt;
        case DataType::Text:
            return kTagText;
        case DataType::Boolean:
            return kTagBoolean;
        case DataType::Float:
            return kTagFloat;
    }
    throw std::logic_error("unknown data type");
}

DataType type_from_tag(std::uint8_t tag) {
    switch (tag) {
        case kTagInt:
            return DataType::Int;
        case kTagText:
            return DataType::Text;
        case kTagBoolean:
            return DataType::Boolean;
        case kTagFloat:
            return DataType::Float;
        default:
            throw StorageError("Unknown type tag " + std::to_string(tag));
    }
}

class ByteWriter {
  public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }

    void u16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    }

    void u32(std::uint32_t value) {
        u16(static_cast<std::uint16_t>(value & 0xFFFFu));
        u16(static_cast<std::uint16_t>((value >> 16) & 0xFFFFu));
    }

    void u64(std::uint64_t value) {
        u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
        u32(static_cast<std::uint32_t>((value >> 32) & 0xFFFFFFFFu));
    }

    void f64(double value) {
        static_assert(sizeof(double) == sizeof(std::uint64_t), "IEEE-754 binary64 required");
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u64(bits);
    }

    void raw(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        bytes_.insert(bytes_.end(), bytes, bytes + size);
    }

    void str16(const std::string& text) {
        if (text.size() > std::numeric_limits<std::uint16_t>::max()) {
            throw StorageError("Name exceeds the storage limit");
        }
        u16(static_cast<std::uint16_t>(text.size()));
        raw(text.data(), text.size());
    }

    void str32(const std::string& text) {
        if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw StorageError("Text value exceeds the storage limit");
        }
        u32(static_cast<std::uint32_t>(text.size()));
        raw(text.data(), text.size());
    }

    std::vector<std::uint8_t> take() { return std::move(bytes_); }

  private:
    std::vector<std::uint8_t> bytes_;
};

class ByteReader {
  public:
    ByteReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::uint8_t u8() {
        require(1);
        return data_[index_++];
    }

    std::uint16_t u16() {
        const std::uint16_t low = u8();
        const std::uint16_t high = u8();
        return static_cast<std::uint16_t>(low | (high << 8));
    }

    std::uint32_t u32() {
        const std::uint32_t low = u16();
        const std::uint32_t high = u16();
        return low | (high << 16);
    }

    std::uint64_t u64() {
        const std::uint64_t low = u32();
        const std::uint64_t high = u32();
        return low | (high << 32);
    }

    double f64() {
        static_assert(std::numeric_limits<double>::is_iec559, "IEEE-754 double required");
        const std::uint64_t bits = u64();
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::string str16() { return take_string(u16()); }

    std::string str32() { return take_string(u32()); }

    void finish() const {
        if (index_ != size_) {
            throw StorageError("Trailing bytes in record");
        }
    }

  private:
    std::string take_string(std::uint32_t length) {
        require(length);
        std::string text(reinterpret_cast<const char*>(data_ + index_), length);
        index_ += length;
        return text;
    }

    void require(std::size_t n) const {
        if (index_ > size_ || n > size_ - index_) {
            throw StorageError("Truncated record");
        }
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t index_ = 0;
};

std::int64_t bits_to_int64(std::uint64_t bits) {
    std::int64_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint64_t int64_to_bits(std::int64_t value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

}  // namespace

std::vector<std::uint8_t> encode_values(const std::vector<Value>& values) {
    if (values.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw StorageError("Row has too many columns to store");
    }
    ByteWriter writer;
    writer.u16(static_cast<std::uint16_t>(values.size()));
    for (const Value& value : values) {
        writer.u8(type_tag(value.type()));
        switch (value.type()) {
            case DataType::Int:
                writer.u64(int64_to_bits(value.integer()));
                break;
            case DataType::Float:
                writer.f64(value.floating());
                break;
            case DataType::Boolean:
                writer.u8(value.boolean() ? 1 : 0);
                break;
            case DataType::Text:
                writer.str32(value.text());
                break;
        }
    }
    return writer.take();
}

std::vector<Value> decode_values(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr && size != 0) {
        throw StorageError("Truncated record");
    }
    ByteReader reader(data, size);
    const std::uint16_t count = reader.u16();
    std::vector<Value> values;
    values.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        const DataType type = type_from_tag(reader.u8());
        switch (type) {
            case DataType::Int:
                values.push_back(Value::integer(bits_to_int64(reader.u64())));
                break;
            case DataType::Float:
                values.push_back(Value::floating(reader.f64()));
                break;
            case DataType::Boolean: {
                const std::uint8_t bit = reader.u8();
                if (bit > 1) {
                    throw StorageError("Invalid boolean byte");
                }
                values.push_back(Value::boolean(bit == 1));
                break;
            }
            case DataType::Text:
                values.push_back(Value::text(reader.str32()));
                break;
        }
    }
    reader.finish();
    return values;
}

std::vector<std::uint8_t> encode_catalog_entry(const CatalogEntry& entry) {
    if (entry.columns.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw StorageError("Table has too many columns to store");
    }
    ByteWriter writer;
    writer.str16(entry.name);
    writer.u16(static_cast<std::uint16_t>(entry.columns.size()));
    for (const ColumnDefinition& column : entry.columns) {
        writer.str16(column.name);
        writer.u8(type_tag(column.type));
    }
    writer.u32(entry.head_page);
    writer.u32(entry.tail_page);
    writer.u32(entry.overflow_head);
    return writer.take();
}

CatalogEntry decode_catalog_entry(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr && size != 0) {
        throw StorageError("Truncated record");
    }
    ByteReader reader(data, size);
    CatalogEntry entry;
    entry.name = reader.str16();
    const std::uint16_t columns = reader.u16();
    entry.columns.reserve(columns);
    for (std::uint16_t i = 0; i < columns; ++i) {
        ColumnDefinition column;
        column.name = reader.str16();
        column.type = type_from_tag(reader.u8());
        entry.columns.push_back(std::move(column));
    }
    entry.head_page = reader.u32();
    entry.tail_page = reader.u32();
    entry.overflow_head = reader.u32();
    reader.finish();
    return entry;
}

}  // namespace minidb
