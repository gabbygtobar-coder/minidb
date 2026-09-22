#include "minidb/record.hpp"
#include "minidb/storage_error.hpp"
#include "minidb/value.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> bytes_of(std::initializer_list<int> values) {
    std::vector<std::uint8_t> out;
    out.reserve(values.size());
    for (const int value : values) {
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}

}  // namespace

TEST(Record, EncodesIntegersFloatsBooleansAndTextLittleEndian) {
    const std::vector<std::uint8_t> integer =
        minidb::encode_values({minidb::Value::integer(0x0102030405060708ll)});
    EXPECT_EQ(integer, bytes_of({0x01, 0x00, 0x01, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}));

    const std::vector<std::uint8_t> negative = minidb::encode_values({minidb::Value::integer(-2)});
    EXPECT_EQ(negative, bytes_of({0x01, 0x00, 0x01, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));

    // 1.0 is the IEEE-754 pattern 0x3FF0000000000000.
    const std::vector<std::uint8_t> one = minidb::encode_values({minidb::Value::floating(1.0)});
    EXPECT_EQ(one, bytes_of({0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}));

    EXPECT_EQ(minidb::encode_values({minidb::Value::boolean(true)}), bytes_of({0x01, 0x00, 0x03, 0x01}));
    EXPECT_EQ(minidb::encode_values({minidb::Value::boolean(false)}), bytes_of({0x01, 0x00, 0x03, 0x00}));
    EXPECT_EQ(minidb::encode_values({minidb::Value::text("ab")}),
              bytes_of({0x01, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 'a', 'b'}));
    EXPECT_EQ(minidb::encode_values({minidb::Value::text("")}),
              bytes_of({0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00}));

    const std::string embedded("a\0b", 3);
    const std::vector<std::uint8_t> with_nul = minidb::encode_values({minidb::Value::text(embedded)});
    EXPECT_EQ(with_nul, bytes_of({0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 'a', 0x00, 'b'}));
}

TEST(Record, RoundTripsAMixedRow) {
    const std::vector<minidb::Value> row = {
        minidb::Value::integer(7),
        minidb::Value::text("it's"),
        minidb::Value::boolean(false),
        minidb::Value::floating(3.14),
        minidb::Value::text(std::string("a\0b", 3)),
    };
    const std::vector<std::uint8_t> encoded = minidb::encode_values(row);
    const std::vector<minidb::Value> decoded = minidb::decode_values(encoded.data(), encoded.size());
    ASSERT_EQ(decoded.size(), row.size());
    EXPECT_EQ(decoded[0].integer(), 7);
    EXPECT_EQ(decoded[1].text(), "it's");
    EXPECT_FALSE(decoded[2].boolean());
    EXPECT_DOUBLE_EQ(decoded[3].floating(), 3.14);
    EXPECT_EQ(decoded[4].text(), std::string("a\0b", 3));
    EXPECT_EQ(decoded[3].format(), row[3].format());
}

TEST(Record, RejectsTruncationPaddingAndUnknownTags) {
    const std::vector<std::uint8_t> integer =
        minidb::encode_values({minidb::Value::integer(1)});
    EXPECT_THROW(minidb::decode_values(integer.data(), integer.size() - 1), minidb::StorageError);
    std::vector<std::uint8_t> padded = integer;
    padded.push_back(0);
    EXPECT_THROW(minidb::decode_values(padded.data(), padded.size()), minidb::StorageError);

    const std::uint8_t unknown[] = {0x01, 0x00, 0x09};
    EXPECT_THROW(minidb::decode_values(unknown, sizeof(unknown)), minidb::StorageError);

    const std::uint8_t bad_bool[] = {0x01, 0x00, 0x03, 0x02};
    EXPECT_THROW(minidb::decode_values(bad_bool, sizeof(bad_bool)), minidb::StorageError);
    EXPECT_THROW(minidb::decode_values(nullptr, 0), minidb::StorageError);
}

TEST(Record, CatalogEntryRoundTripAndBytes) {
    minidb::CatalogEntry entry;
    entry.name = "t";
    entry.columns.push_back({"id", minidb::DataType::Int});
    entry.head_page = 0;
    entry.tail_page = 0;
    entry.overflow_head = 0;

    const std::vector<std::uint8_t> encoded = minidb::encode_catalog_entry(entry);
    EXPECT_EQ(encoded, bytes_of({0x01, 0x00, 't', 0x01, 0x00, 0x02, 0x00, 'i', 'd', 0x01, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));

    entry.name = "users";
    entry.columns = {
        {"id", minidb::DataType::Int},
        {"name", minidb::DataType::Text},
        {"active", minidb::DataType::Boolean},
        {"score", minidb::DataType::Float},
    };
    entry.head_page = 4;
    entry.tail_page = 9;
    entry.overflow_head = 12;
    const std::vector<std::uint8_t> stored = minidb::encode_catalog_entry(entry);
    const minidb::CatalogEntry decoded = minidb::decode_catalog_entry(stored.data(), stored.size());
    EXPECT_EQ(decoded.name, "users");
    ASSERT_EQ(decoded.columns.size(), 4u);
    EXPECT_EQ(decoded.columns[0].name, "id");
    EXPECT_EQ(decoded.columns[0].type, minidb::DataType::Int);
    EXPECT_EQ(decoded.columns[1].type, minidb::DataType::Text);
    EXPECT_EQ(decoded.columns[2].type, minidb::DataType::Boolean);
    EXPECT_EQ(decoded.columns[3].type, minidb::DataType::Float);
    EXPECT_EQ(decoded.head_page, 4u);
    EXPECT_EQ(decoded.tail_page, 9u);
    EXPECT_EQ(decoded.overflow_head, 12u);

    EXPECT_THROW(minidb::decode_catalog_entry(stored.data(), 2), minidb::StorageError);

    entry.indexes.push_back({"idx", 0, 7});
    const std::vector<std::uint8_t> with_index = minidb::encode_catalog_entry(entry);
    EXPECT_GT(with_index.size(), stored.size());
    const minidb::CatalogEntry indexed =
        minidb::decode_catalog_entry(with_index.data(), with_index.size());
    ASSERT_EQ(indexed.indexes.size(), 1u);
    EXPECT_EQ(indexed.indexes[0].name, "idx");
    EXPECT_EQ(indexed.indexes[0].column, 0u);
    EXPECT_EQ(indexed.indexes[0].root_page, 7u);
    EXPECT_EQ(indexed.head_page, 4u);
}
