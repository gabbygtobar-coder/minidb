#include "minidb/pager.hpp"

#include "minidb/storage_error.hpp"
#include "slotted_page.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace minidb {
namespace {

constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kVersionOffset = 8;
constexpr std::size_t kPageSizeOffset = 12;
constexpr std::size_t kPageCountOffset = 16;
constexpr std::size_t kCatalogRootOffset = 20;
constexpr std::size_t kFreeHeadOffset = 24;
constexpr std::size_t kCatalogTailOffset = 28;
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::size_t kFreeNextOffset = 8;
constexpr unsigned char kMagic[8] = {'M', 'I', 'N', 'I', 'D', 'B', 1, 0};

struct FileClose {
    void operator()(FILE* file) const {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

std::string errno_text() {
    const int saved = errno;
    const char* text = std::strerror(saved);
    if (text == nullptr) {
        return "unknown error";
    }
    return text;
}

bool magic_matches(const Page& page) {
    unsigned char found[8];
    page.read_bytes(kMagicOffset, found, 8);
    return std::memcmp(found, kMagic, 8) == 0;
}

void enable_large_buffer(FILE* file) {
    // Ignore failure: the default buffer still honors fflush.
    if (std::setvbuf(file, nullptr, _IOFBF, 1 << 20) != 0) {
        return;
    }
}

}  // namespace

struct Pager::Impl {
    struct Frame {
        bool loaded = false;
        bool dirty = false;
        Page page;
    };

    std::unique_ptr<FILE, FileClose> file;
    std::string path;
    mutable std::vector<Frame> frames;
    std::uint32_t page_count = 0;
    std::uint32_t catalog_root = 1;
    std::uint32_t catalog_tail = 1;
    std::uint32_t free_head = 0;
    mutable std::uint64_t reads = 0;

    void store_header() {
        Page header = frames[0].page;
        header.write_bytes(kMagicOffset, kMagic, 8);
        header.write_u32(kVersionOffset, kFormatVersion);
        header.write_u32(kPageSizeOffset, static_cast<std::uint32_t>(kPageSize));
        header.write_u32(kPageCountOffset, page_count);
        header.write_u32(kCatalogRootOffset, catalog_root);
        header.write_u32(kFreeHeadOffset, free_head);
        header.write_u32(kCatalogTailOffset, catalog_tail);
        frames[0].page = header;
        frames[0].loaded = true;
        frames[0].dirty = true;
    }

    void create_fresh() {
        page_count = 2;
        catalog_root = 1;
        catalog_tail = 1;
        free_head = 0;
        frames.assign(2, Frame{});
        frames[0].loaded = true;
        frames[1].loaded = true;
        frames[0].dirty = true;
        frames[1].dirty = true;
        slotted::initialize_heap_page(frames[1].page);
        store_header();
    }

    void write_raw(std::uint32_t page_id, const Page& page) const {
        if (!file) {
            throw StorageError("Cannot flush an in-memory database to disk");
        }
        const auto offset = static_cast<long>(static_cast<std::uint64_t>(page_id) * kPageSize);
        if (std::fseek(file.get(), offset, SEEK_SET) != 0) {
            throw StorageError("Failed to seek in " + path + ": " + errno_text());
        }
        if (std::fwrite(page.data(), 1, kPageSize, file.get()) != kPageSize) {
            throw StorageError("Failed to write " + path + ": " + errno_text());
        }
    }

    Page read_page(std::uint32_t page_id) const {
        ++reads;
        if (page_id >= page_count) {
            throw StorageError("Page " + std::to_string(page_id) + " is outside the file");
        }
        Frame& frame = frames[page_id];
        if (!frame.loaded) {
            if (!file) {
                throw StorageError("Page " + std::to_string(page_id) + " is not loaded");
            }
            const auto offset = static_cast<long>(static_cast<std::uint64_t>(page_id) * kPageSize);
            if (std::fseek(file.get(), offset, SEEK_SET) != 0) {
                throw StorageError("Failed to seek in " + path + ": " + errno_text());
            }
            if (std::fread(frame.page.data(), 1, kPageSize, file.get()) != kPageSize) {
                throw StorageError("Truncated database file: " + path);
            }
            frame.loaded = true;
        }
        return frame.page;
    }

    void write_page(std::uint32_t page_id, const Page& page) {
        if (page_id >= page_count) {
            throw StorageError("Page " + std::to_string(page_id) + " is outside the file");
        }
        frames[page_id].page = page;
        frames[page_id].loaded = true;
        frames[page_id].dirty = true;
    }

    std::uint32_t allocate_page() {
        std::uint32_t id = 0;
        if (free_head != 0) {
            id = free_head;
            const Page free_page = read_page(id);
            if (free_page.read_u8(0) != kPageTypeFree) {
                throw StorageError("Free list page is not marked free");
            }
            free_head = free_page.read_u32(kFreeNextOffset);
        } else {
            if (page_count == UINT32_MAX) {
                throw StorageError("Database file is full");
            }
            id = page_count;
            frames.emplace_back();
            ++page_count;
        }
        write_page(id, Page{});
        store_header();
        return id;
    }

    void free_page(std::uint32_t page_id) {
        if (page_id == 0 || page_id >= page_count) {
            throw StorageError("Cannot free page " + std::to_string(page_id));
        }
        const Page current = read_page(page_id);
        if (current.read_u8(0) == kPageTypeFree) {
            throw StorageError("Page " + std::to_string(page_id) + " is already free");
        }
        Page free_page;
        free_page.write_u8(0, kPageTypeFree);
        free_page.write_u32(kFreeNextOffset, free_head);
        free_head = page_id;
        write_page(page_id, free_page);
        store_header();
    }

    void flush() {
        if (!file) {
            for (Frame& frame : frames) {
                frame.dirty = false;
            }
            return;
        }
        for (std::uint32_t id = 1; id < page_count; ++id) {
            if (frames[id].dirty) {
                write_raw(id, frames[id].page);
            }
        }
        if (!frames.empty() && frames[0].dirty) {
            write_raw(0, frames[0].page);
        }
        if (std::fflush(file.get()) != 0) {
            throw StorageError("Failed to flush " + path + ": " + errno_text());
        }
        for (Frame& frame : frames) {
            frame.dirty = false;
        }
    }
};

Pager::Pager(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Pager::Pager(Pager&&) noexcept = default;

Pager& Pager::operator=(Pager&&) noexcept = default;

Pager::~Pager() {
    try {
        if (impl_) {
            impl_->flush();
        }
    } catch (const StorageError&) {
        // The destructor cannot report this. Mutators flush and surface errors
        // before the pager is destroyed on the success path.
    }
}

Pager Pager::open_memory() {
    auto impl = std::make_unique<Impl>();
    impl->create_fresh();
    impl->flush();
    return Pager(std::move(impl));
}

Pager Pager::open_file(const std::string& path) {
    if (path.empty()) {
        throw StorageError("Database path is empty");
    }
    auto impl = std::make_unique<Impl>();
    impl->path = path;

    FILE* raw = std::fopen(path.c_str(), "r+b");
    if (raw == nullptr) {
        if (errno != ENOENT) {
            throw StorageError("Cannot open database file " + path + ": " + errno_text());
        }
        raw = std::fopen(path.c_str(), "w+b");
        if (raw == nullptr) {
            throw StorageError("Cannot create database file " + path + ": " + errno_text());
        }
        impl->file.reset(raw);
        enable_large_buffer(impl->file.get());
        impl->create_fresh();
        impl->flush();
        return Pager(std::move(impl));
    }

    impl->file.reset(raw);
    enable_large_buffer(impl->file.get());
    if (std::fseek(impl->file.get(), 0, SEEK_END) != 0) {
        throw StorageError("Failed to seek in " + path + ": " + errno_text());
    }
    const long end = std::ftell(impl->file.get());
    if (end < 0) {
        throw StorageError("Failed to read the size of " + path + ": " + errno_text());
    }
    if (end == 0) {
        impl->create_fresh();
        impl->flush();
        return Pager(std::move(impl));
    }
    if (static_cast<unsigned long>(end) % kPageSize != 0) {
        throw StorageError("Database file is truncated: " + path);
    }

    impl->page_count = 1;
    impl->frames.assign(1, Impl::Frame{});
    Page header;
    if (std::fseek(impl->file.get(), 0, SEEK_SET) != 0 ||
        std::fread(header.data(), 1, kPageSize, impl->file.get()) != kPageSize) {
        throw StorageError("Database file is truncated: " + path);
    }
    if (!magic_matches(header)) {
        throw StorageError("Not a MiniDB database file: " + path);
    }
    const std::uint32_t version = header.read_u32(kVersionOffset);
    if (version != kFormatVersion) {
        throw StorageError("Unsupported MiniDB file version " + std::to_string(version));
    }
    const std::uint32_t stored_page_size = header.read_u32(kPageSizeOffset);
    if (stored_page_size != kPageSize) {
        throw StorageError("Unexpected page size " + std::to_string(stored_page_size));
    }
    const std::uint32_t stored_count = header.read_u32(kPageCountOffset);
    const auto file_pages = static_cast<std::uint64_t>(static_cast<unsigned long>(end) / kPageSize);
    if (stored_count < 2 || file_pages < stored_count) {
        throw StorageError("Database file is truncated: " + path);
    }
    const std::uint32_t root = header.read_u32(kCatalogRootOffset);
    const std::uint32_t tail = header.read_u32(kCatalogTailOffset);
    const std::uint32_t free_head = header.read_u32(kFreeHeadOffset);
    if (root == 0 || root >= stored_count || tail == 0 || tail >= stored_count ||
        free_head >= stored_count) {
        throw StorageError("Corrupt database header: " + path);
    }

    impl->page_count = stored_count;
    impl->catalog_root = root;
    impl->catalog_tail = tail;
    impl->free_head = free_head;
    impl->frames.assign(stored_count, Impl::Frame{});
    impl->frames[0].page = header;
    impl->frames[0].loaded = true;
    return Pager(std::move(impl));
}

Page Pager::read_page(std::uint32_t page_id) const {
    return impl_->read_page(page_id);
}

void Pager::write_page(std::uint32_t page_id, const Page& page) {
    impl_->write_page(page_id, page);
}

std::uint32_t Pager::allocate_page() {
    return impl_->allocate_page();
}

void Pager::free_page(std::uint32_t page_id) {
    impl_->free_page(page_id);
}

std::uint32_t Pager::page_count() const {
    return impl_->page_count;
}

std::uint32_t Pager::catalog_root() const {
    return impl_->catalog_root;
}

std::uint32_t Pager::catalog_tail() const {
    return impl_->catalog_tail;
}

void Pager::set_catalog_tail(std::uint32_t page_id) {
    if (page_id == 0 || page_id >= impl_->page_count) {
        throw StorageError("Catalog tail page is outside the file");
    }
    impl_->catalog_tail = page_id;
    impl_->store_header();
}

void Pager::flush() {
    impl_->flush();
}

std::uint64_t Pager::read_count() const {
    return impl_->reads;
}

void Pager::reset_read_count() {
    impl_->reads = 0;
}

}  // namespace minidb
