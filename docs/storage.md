# Storage

M3 stores a database in one file of fixed-size pages. The page size is **4096 bytes**. The default path is `minidb.db` in the current working directory. Pass another path as the first argument:

```text
minidb
minidb /tmp/demo.db
```

`Database()` with no path (what the unit tests use unless they pass a file) keeps the same page layout in memory and never creates a file. The CLI always passes a path.

A new file, or an existing empty file, is initialized with a header page and an empty catalog page. A non-empty file must start with the MiniDB magic or open fails.

## What "flushed" means

Every successful `CREATE`, `DROP`, `INSERT`, `UPDATE`, and `DELETE` writes its dirty pages and then calls `fflush` on the `FILE` stream. `fflush` hands the bytes to the operating system. MiniDB does **not** call `fsync` or `fdatasync`.

That is not a durability guarantee:

- A power loss can drop data the kernel still has in its cache.
- `fflush` itself can be interrupted. A page can be torn.
- The C stream is fully buffered (1 MiB when `setvbuf` succeeds). If a write fills that buffer, the C library can flush early, before the statement's own `fflush`. A crash can then leave a prefix of a multi-page write on disk.
- A multi-row `UPDATE`, or a `DELETE` with `WHERE`, flushes once per changed row. It is not atomic. A crash can apply the first rows and not the rest. `DELETE` without `WHERE` frees the heap in one flush.
- There is no write-ahead log, no checksum, and no rollback.

Destroying the `Database` object flushes again, so a normal `.exit` does not rely on the C library's exit-time flush alone. Two processes must not open the same file. There is no lock.

Pages past the header's `page_count` are ignored. A crash that extends the file and dies before the header is updated can leak that space until the file is recreated. The file never shrinks; freed pages stay in the file and are reused.

## File header (page 0)

All multi-byte integers are little-endian.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | Magic `4D 49 4E 49 44 42 01 00` (`MINIDB`, version byte 1, NUL) |
| 8 | 4 | Format version (`1`) |
| 12 | 4 | Page size (`4096`) |
| 16 | 4 | Page count, including page 0. This is the file length in pages, not the number of live pages. |
| 20 | 4 | Catalog root page id (initially 1) |
| 24 | 4 | Free-list head. `0` means the free list is empty. |
| 28 | 4 | Catalog tail page id (initially 1) |
| 32 | 4064 | Reserved, written as zero. Readers ignore it. |

Page 0 is never a heap page and cannot be freed.

## Heap page

Every page except page 0 and free-list pages is a slotted heap page.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Type `1` (heap) |
| 1 | 1 | Flags. Bit 0 (`0x01`) marks an overflow page. |
| 2 | 2 | Slot count |
| 4 | 2 | `free_start`, the first byte not yet used by a record payload |
| 6 | 2 | Reserved `0` |
| 8 | 4 | Next page id, or `0` |
| 12 | 4 | Previous page id, or `0`. Used by the overflow list. Primary pages leave it `0`. |
| 16 | … | Record bytes, growing upward |
| end | 8 × slots | Slot directory, growing downward |

Slot `i` is at byte `4096 - (i + 1) * 8`:

| Offset in slot | Size | Field |
| --- | --- | --- |
| 0 | 2 | Record offset. `0` is a tombstone. `0xFFFF` is a forward pointer. |
| 2 | 2 | Record length, or the target slot when this slot is a forward pointer. |
| 4 | 4 | `0` for a normal record. For a forward pointer, the overflow page id. |

A record has to fit in one page. The largest payload is **4072 bytes** (`4096 - 16 - 8`), which is one record and one slot on an otherwise empty page. A larger row or table definition is an execution error and does not change the table.

`INSERT` appends a slot on the tail page. If the tail is only tombstones, it is reset and reused. Otherwise MiniDB packs the tail once and, if the record still does not fit, links a new primary page. Slot order is insertion order. A scan walks the primary chain and skips tombstones.

`DELETE` of one row turns its slot into a tombstone. Other slot indexes stay valid. `DELETE` without `WHERE` frees the table's pages and sets the heap pointers back to zero.

`UPDATE` rewrites the record in place when the new bytes are the same size or smaller, or moves them into the page's free space, or packs the page and tries again. If the row still does not fit beside its neighbors, the slot becomes a forward pointer and the bytes move to a new overflow page. The row stays at its old position in the scan. Overflow pages are chained from the table's `overflow_head` and are not part of the primary scan. An overflow page holds that one row. A later update of the same row rewrites the overflow page; it does not add another forward.

Dropped table pages and overflow pages go on the free list. `allocate` pops that list before it extends the file. A free page is type `2`, and offset 8 holds the next free page id (`0` ends the list). The page is zeroed when it is allocated again, so an old row cannot reappear as a new row.

## Record bytes

Rows and catalog entries are encoded field by field. MiniDB does not dump a C++ object.

A row:

```text
u16  column_count
repeated column_count times:
  u8   type tag
  payload
```

| Tag | Column | Payload |
| --- | --- | --- |
| 1 | `INT` | int64, little-endian two's complement |
| 2 | `TEXT` | u32 byte length, then that many bytes (no NUL terminator; a NUL inside the text is data) |
| 3 | `BOOLEAN` | one byte, `0` or `1` |
| 4 | `FLOAT` | IEEE-754 binary64, little-endian |

`decode` rejects a short buffer, trailing bytes, an unknown tag, and a boolean other than 0 or 1. On scan, the decoded types and column count have to match the catalog entry or the file is treated as corrupt.

A catalog entry, one per table:

```text
u16  name length, then the name bytes
u16  column count
repeated:
  u16  column-name length, then the name bytes
  u8   type tag (same tags as a row)
u32  head page of the primary heap (0 if the table has no rows)
u32  tail page
u32  overflow list head (0 if none)
```

The catalog itself is a heap chain starting at the catalog root. Dropping a table tombstones its catalog slot. Names stay case-sensitive (`Users` and `users` are different tables). Table names are listed in lexicographic order from the in-memory map built at open, not in catalog-slot order.

## What this is not

- No B-tree and no secondary index. Every `SELECT`, `UPDATE`, and `DELETE` is a heap scan. That is M4.
- No transactions, savepoints, or isolation.
- No buffer-pool eviction. Touched pages stay in the process.
- No checksums. A torn page is not detected until a later read fails a bounds or type check, and it might not fail.
- No `NULL`, and no row that spans pages except through the single forward pointer described above.
