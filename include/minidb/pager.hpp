#pragma once

#include "minidb/page.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace minidb {

// Reads and writes fixed-size pages.
//
// `open_file` creates the file when it is missing or empty, and checks the
// header when it already holds a database. `open_memory` is the same layout
// with no file behind it, used by tests that do not care about restart.
//
// `flush` writes dirty pages and then fflush's the C stream. It does not
// fsync. See docs/storage.md for what a crash can lose.
class Pager {
  public:
    static Pager open_memory();
    static Pager open_file(const std::string& path);

    Pager(Pager&&) noexcept;
    Pager& operator=(Pager&&) noexcept;
    ~Pager();

    Pager(const Pager&) = delete;
    Pager& operator=(const Pager&) = delete;

    Page read_page(std::uint32_t page_id) const;
    void write_page(std::uint32_t page_id, const Page& page);

    // Reuses a free page when one exists, otherwise appends a zero page.
    std::uint32_t allocate_page();
    // Returns a page to the free list. Page 0 (the file header) cannot be freed.
    void free_page(std::uint32_t page_id);

    std::uint32_t page_count() const;
    std::uint32_t catalog_root() const;
    std::uint32_t catalog_tail() const;
    void set_catalog_tail(std::uint32_t page_id);

    // Push dirty pages to the OS. File pagers call fflush and stop there.
    void flush();

    // Logical page reads, including pages served from the in-memory cache.
    // Used to compare an index lookup with a heap scan. Not a disk-I/O count.
    std::uint64_t read_count() const;
    void reset_read_count();

  private:
    struct Impl;
    explicit Pager(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace minidb
