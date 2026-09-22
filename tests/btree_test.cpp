#include "minidb/btree.hpp"
#include "minidb/execution_error.hpp"
#include "minidb/pager.hpp"
#include "minidb/storage_error.hpp"
#include "minidb/value.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

minidb::IndexKey key_of(std::int64_t value, std::uint32_t page, std::uint16_t slot) {
    minidb::IndexKey key;
    key.column = minidb::encode_index_column(minidb::Value::integer(value));
    key.page_id = page;
    key.slot = slot;
    return key;
}

minidb::IndexBound column_bound(const minidb::Value& value, bool inclusive) {
    minidb::IndexBound bound;
    bound.unbounded = false;
    bound.inclusive = inclusive;
    bound.column = minidb::encode_index_column(value);
    return bound;
}

std::vector<minidb::RowId> scan_all(const minidb::Pager& pager, std::uint32_t root) {
    return minidb::btree_scan(pager, root, {}, {});
}

}  // namespace

TEST(BTree, EncodesColumnOrder) {
    const auto negative = minidb::encode_index_column(minidb::Value::integer(-2));
    const auto zero = minidb::encode_index_column(minidb::Value::integer(0));
    const auto one = minidb::encode_index_column(minidb::Value::integer(1));
    const auto min = minidb::encode_index_column(minidb::Value::integer(INT64_MIN));
    const auto max = minidb::encode_index_column(minidb::Value::integer(INT64_MAX));
    EXPECT_LT(min, negative);
    EXPECT_LT(negative, zero);
    EXPECT_LT(zero, one);
    EXPECT_LT(one, max);

    const auto neg_zero = minidb::encode_index_column(minidb::Value::floating(-0.0));
    const auto pos_zero = minidb::encode_index_column(minidb::Value::floating(0.0));
    const auto half = minidb::encode_index_column(minidb::Value::floating(0.5));
    const auto two = minidb::encode_index_column(minidb::Value::floating(2.0));
    const auto neg = minidb::encode_index_column(minidb::Value::floating(-1.5));
    EXPECT_EQ(neg_zero, pos_zero);
    EXPECT_LT(neg, pos_zero);
    EXPECT_LT(pos_zero, half);
    EXPECT_LT(half, two);

    EXPECT_LT(minidb::encode_index_column(minidb::Value::text("a")),
              minidb::encode_index_column(minidb::Value::text("b")));
    EXPECT_LT(minidb::encode_index_column(minidb::Value::text("a")),
              minidb::encode_index_column(minidb::Value::text("aa")));
    EXPECT_EQ(minidb::encode_index_column(minidb::Value::boolean(false)),
              std::vector<std::uint8_t>{0});
    EXPECT_EQ(minidb::encode_index_column(minidb::Value::boolean(true)),
              std::vector<std::uint8_t>{1});

    EXPECT_THROW(minidb::encode_index_column(minidb::Value::text(std::string(minidb::kMaxIndexKeyBytes + 1, 'x'))),
                 minidb::ExecutionError);
}

TEST(BTree, InsertSplitLookupDeleteAndCheck) {
    minidb::Pager pager = minidb::Pager::open_memory();
    std::uint32_t root = minidb::btree_create(pager);
    minidb::btree_check(pager, root);
    EXPECT_TRUE(minidb::btree_stats(pager, root).root_is_leaf);
    EXPECT_EQ(minidb::btree_stats(pager, root).keys, 0u);

    constexpr int kCount = 400;
    for (int value = 0; value < kCount; ++value) {
        minidb::btree_insert(pager, root, key_of(value, 3, static_cast<std::uint16_t>(value)));
    }
    minidb::btree_check(pager, root);
    const minidb::BTreeStats grown = minidb::btree_stats(pager, root);
    EXPECT_FALSE(grown.root_is_leaf);
    EXPECT_GE(grown.height, 2u);
    EXPECT_GE(grown.pages, 3u);
    EXPECT_EQ(grown.keys, static_cast<std::uint32_t>(kCount));
    EXPECT_GT(grown.leaves, 1u);

    const minidb::IndexBound low = column_bound(minidb::Value::integer(10), true);
    const minidb::IndexBound high = column_bound(minidb::Value::integer(12), true);
    const std::vector<minidb::RowId> range = minidb::btree_scan(pager, root, low, high);
    ASSERT_EQ(range.size(), 3u);
    EXPECT_EQ(range[0].slot, 10u);
    EXPECT_EQ(range[1].slot, 11u);
    EXPECT_EQ(range[2].slot, 12u);

    const minidb::IndexBound only = column_bound(minidb::Value::integer(250), true);
    const std::vector<minidb::RowId> one = minidb::btree_scan(pager, root, only, only);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0].slot, 250u);

    const minidb::IndexBound missing = column_bound(minidb::Value::integer(1000), true);
    EXPECT_TRUE(minidb::btree_scan(pager, root, missing, missing).empty());

    const minidb::IndexBound above = column_bound(minidb::Value::integer(390), false);
    const std::vector<minidb::RowId> tail = minidb::btree_scan(pager, root, above, {});
    ASSERT_EQ(tail.size(), 9u);
    EXPECT_EQ(tail.front().slot, 391u);
    EXPECT_EQ(tail.back().slot, 399u);

    for (int value = 0; value < kCount; value += 2) {
        minidb::btree_remove(pager, root, key_of(value, 3, static_cast<std::uint16_t>(value)));
    }
    minidb::btree_check(pager, root);
    EXPECT_EQ(minidb::btree_stats(pager, root).keys, static_cast<std::uint32_t>(kCount / 2));
    EXPECT_TRUE(minidb::btree_scan(pager, root, column_bound(minidb::Value::integer(2), true),
                                   column_bound(minidb::Value::integer(2), true))
                    .empty());
    const std::vector<minidb::RowId> odd =
        minidb::btree_scan(pager, root, column_bound(minidb::Value::integer(1), true),
                           column_bound(minidb::Value::integer(1), true));
    ASSERT_EQ(odd.size(), 1u);
    EXPECT_EQ(odd[0].slot, 1u);

    for (int value = 1; value < kCount; value += 2) {
        minidb::btree_remove(pager, root, key_of(value, 3, static_cast<std::uint16_t>(value)));
    }
    minidb::btree_check(pager, root);
    const minidb::BTreeStats empty = minidb::btree_stats(pager, root);
    EXPECT_TRUE(empty.root_is_leaf);
    EXPECT_EQ(empty.keys, 0u);
    EXPECT_TRUE(scan_all(pager, root).empty());
}

TEST(BTree, DuplicateColumnValuesStayOrderedByRowId) {
    minidb::Pager pager = minidb::Pager::open_memory();
    std::uint32_t root = minidb::btree_create(pager);
    minidb::btree_insert(pager, root, key_of(5, 4, 2));
    minidb::btree_insert(pager, root, key_of(5, 4, 0));
    minidb::btree_insert(pager, root, key_of(5, 9, 1));
    minidb::btree_insert(pager, root, key_of(4, 1, 0));
    minidb::btree_insert(pager, root, key_of(6, 1, 1));
    minidb::btree_check(pager, root);

    const std::vector<minidb::RowId> fives =
        minidb::btree_scan(pager, root, column_bound(minidb::Value::integer(5), true),
                           column_bound(minidb::Value::integer(5), true));
    ASSERT_EQ(fives.size(), 3u);
    EXPECT_EQ(fives[0].slot, 0u);
    EXPECT_EQ(fives[1].slot, 2u);
    EXPECT_EQ(fives[2].page_id, 9u);

    minidb::btree_remove(pager, root, key_of(5, 4, 0));
    minidb::btree_check(pager, root);
    const std::vector<minidb::RowId> left =
        minidb::btree_scan(pager, root, column_bound(minidb::Value::integer(5), true),
                           column_bound(minidb::Value::integer(5), true));
    ASSERT_EQ(left.size(), 2u);
    EXPECT_EQ(left[0].slot, 2u);
    EXPECT_EQ(left[1].page_id, 9u);
}

TEST(BTree, SurvivesReopenAndPageReuse) {
    const std::string path = "minidb-btree-reopen.db";
    std::remove(path.c_str());
    std::uint32_t root = 0;
    std::uint32_t pages_after_build = 0;
    {
        minidb::Pager pager = minidb::Pager::open_file(path);
        root = minidb::btree_create(pager);
        for (int value = 0; value < 300; ++value) {
            minidb::btree_insert(pager, root, key_of(value, 2, static_cast<std::uint16_t>(value)));
        }
        minidb::btree_check(pager, root);
        pages_after_build = pager.page_count();
        pager.flush();
    }
    {
        minidb::Pager pager = minidb::Pager::open_file(path);
        minidb::btree_check(pager, root);
        const std::vector<minidb::RowId> hit =
            minidb::btree_scan(pager, root, column_bound(minidb::Value::integer(42), true),
                               column_bound(minidb::Value::integer(42), true));
        ASSERT_EQ(hit.size(), 1u);
        EXPECT_EQ(hit[0].slot, 42u);
        minidb::btree_destroy(pager, root);
        std::uint32_t rebuilt = minidb::btree_create(pager);
        for (int value = 0; value < 300; ++value) {
            minidb::btree_insert(pager, rebuilt, key_of(value, 2, static_cast<std::uint16_t>(value)));
        }
        // The second tree is built from pages the first tree returned to the free list.
        EXPECT_EQ(pager.page_count(), pages_after_build);
        minidb::btree_check(pager, rebuilt);
    }
    std::remove(path.c_str());
}

TEST(BTree, TextKeysAndExclusiveRange) {
    minidb::Pager pager = minidb::Pager::open_memory();
    std::uint32_t root = minidb::btree_create(pager);
    const std::vector<std::string> words = {"ada", "grace", "alan", "ada", "linus"};
    for (std::size_t i = 0; i < words.size(); ++i) {
        minidb::IndexKey key;
        key.column = minidb::encode_index_column(minidb::Value::text(words[i]));
        key.page_id = 1;
        key.slot = static_cast<std::uint16_t>(i);
        minidb::btree_insert(pager, root, key);
    }
    minidb::btree_check(pager, root);
    const std::vector<minidb::RowId> adas =
        minidb::btree_scan(pager, root, column_bound(minidb::Value::text("ada"), true),
                           column_bound(minidb::Value::text("ada"), true));
    ASSERT_EQ(adas.size(), 2u);
    EXPECT_EQ(adas[0].slot, 0u);
    EXPECT_EQ(adas[1].slot, 3u);

    const std::vector<minidb::RowId> between =
        minidb::btree_scan(pager, root, column_bound(minidb::Value::text("ada"), false),
                           column_bound(minidb::Value::text("linus"), false));
    std::vector<std::uint16_t> slots;
    for (const minidb::RowId id : between) {
        slots.push_back(id.slot);
    }
    EXPECT_EQ(slots, (std::vector<std::uint16_t>{2, 1}));
}
