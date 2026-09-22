#include "minidb/pager.hpp"
#include "minidb/storage_error.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

class TempPath {
  public:
    explicit TempPath(std::string path) : path_(std::move(path)) { std::remove(path_.c_str()); }

    ~TempPath() { std::remove(path_.c_str()); }

    TempPath(const TempPath&) = delete;
    TempPath& operator=(const TempPath&) = delete;

    const std::string& path() const { return path_; }

  private:
    std::string path_;
};

}  // namespace

TEST(Page, LittleEndianIntegersAndBounds) {
    EXPECT_EQ(minidb::kPageSize, 4096u);
    EXPECT_EQ(minidb::kMaxRecordBytes, 4072u);

    minidb::Page page;
    page.write_u32(0, 0x01020304u);
    EXPECT_EQ(page.data()[0], 0x04);
    EXPECT_EQ(page.data()[1], 0x03);
    EXPECT_EQ(page.data()[2], 0x02);
    EXPECT_EQ(page.data()[3], 0x01);
    EXPECT_EQ(page.read_u32(0), 0x01020304u);

    page.write_u16(10, 0xABCDu);
    EXPECT_EQ(page.read_u16(10), 0xABCDu);
    page.write_u64(20, 0x0102030405060708ull);
    EXPECT_EQ(page.read_u64(20), 0x0102030405060708ull);

    EXPECT_THROW(page.read_u8(minidb::kPageSize), minidb::StorageError);
    EXPECT_THROW(page.write_u16(minidb::kPageSize - 1, 1), minidb::StorageError);
}

TEST(Pager, HeaderMagicPageSizeAndRoundTrip) {
    TempPath file("minidb-pager-roundtrip.db");
    std::uint32_t id = 0;
    {
        minidb::Pager pager = minidb::Pager::open_file(file.path());
        EXPECT_EQ(pager.page_count(), 2u);
        EXPECT_EQ(pager.catalog_root(), 1u);

        const minidb::Page header = pager.read_page(0);
        EXPECT_EQ(header.data()[0], static_cast<unsigned char>('M'));
        EXPECT_EQ(header.data()[1], static_cast<unsigned char>('I'));
        EXPECT_EQ(header.data()[2], static_cast<unsigned char>('N'));
        EXPECT_EQ(header.data()[3], static_cast<unsigned char>('I'));
        EXPECT_EQ(header.data()[4], static_cast<unsigned char>('D'));
        EXPECT_EQ(header.data()[5], static_cast<unsigned char>('B'));
        EXPECT_EQ(header.data()[6], 1);
        EXPECT_EQ(header.data()[7], 0);
        EXPECT_EQ(header.read_u32(8), 1u);
        EXPECT_EQ(header.read_u32(12), 4096u);

        id = pager.allocate_page();
        minidb::Page page = pager.read_page(id);
        page.write_u32(32, 0xAABBCCDDu);
        const char hello[] = {'h', 'e', 'l', 'l', 'o'};
        page.write_bytes(64, hello, 5);
        pager.write_page(id, page);
    }
    {
        minidb::Pager pager = minidb::Pager::open_file(file.path());
        const minidb::Page page = pager.read_page(id);
        EXPECT_EQ(page.read_u32(32), 0xAABBCCDDu);
        char hello[5] = {};
        page.read_bytes(64, hello, 5);
        EXPECT_EQ(std::string(hello, 5), "hello");
    }
}

TEST(Pager, FreeListReusesPagesAcrossReopen) {
    TempPath file("minidb-pager-freelist.db");
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    {
        minidb::Pager pager = minidb::Pager::open_file(file.path());
        first = pager.allocate_page();
        second = pager.allocate_page();
        EXPECT_NE(first, second);
        EXPECT_NE(first, 0u);
        EXPECT_NE(first, 1u);
        pager.free_page(first);
        EXPECT_EQ(pager.allocate_page(), first);
        pager.free_page(second);
        EXPECT_THROW(pager.free_page(0), minidb::StorageError);
        EXPECT_THROW(pager.free_page(second), minidb::StorageError);
    }
    {
        minidb::Pager pager = minidb::Pager::open_file(file.path());
        EXPECT_EQ(pager.allocate_page(), second);
    }
}

TEST(Pager, EmptyFileIsInitializedAndGarbageIsRejected) {
    TempPath empty("minidb-pager-empty.db");
    {
        std::ofstream out(empty.path(), std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
    }
    {
        minidb::Pager pager = minidb::Pager::open_file(empty.path());
        EXPECT_EQ(pager.page_count(), 2u);
        EXPECT_EQ(pager.read_page(0).read_u32(12), 4096u);
    }

    TempPath junk("minidb-pager-junk.db");
    {
        std::ofstream out(junk.path(), std::ios::binary | std::ios::trunc);
        out << std::string(minidb::kPageSize, 'x');
    }
    EXPECT_THROW(minidb::Pager::open_file(junk.path()), minidb::StorageError);

    TempPath short_file("minidb-pager-short.db");
    {
        std::ofstream out(short_file.path(), std::ios::binary | std::ios::trunc);
        out << "not-a-page";
    }
    EXPECT_THROW(minidb::Pager::open_file(short_file.path()), minidb::StorageError);
    EXPECT_THROW(minidb::Pager::open_file("/tmp"), minidb::StorageError);
}

TEST(Pager, MemoryPagerRoundTripDoesNotNeedAFile) {
    minidb::Pager pager = minidb::Pager::open_memory();
    EXPECT_EQ(pager.page_count(), 2u);
    const std::uint32_t id = pager.allocate_page();
    minidb::Page page = pager.read_page(id);
    page.write_u8(0, 7);
    pager.write_page(id, page);
    pager.flush();
    EXPECT_EQ(pager.read_page(id).read_u8(0), 7);
}
