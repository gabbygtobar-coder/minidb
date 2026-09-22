# Index

M4 stores a B+ tree in the same 4 KiB page file as the heap. Each node is one page of type `3`. The tree is not an in-memory structure that gets serialized later: splits, deletes, and lookups read and write pages through the pager.

A table can have several single-column indexes. `CREATE INDEX` builds one and fills it from the rows already in the heap. `INSERT`, `UPDATE`, and `DELETE` keep every index on that table aligned with the heap. `DROP INDEX` and `DROP TABLE` free the index pages onto the file free list.

The file format version stays `1`. A catalog entry with no indexes has the same bytes as in M3. An entry with indexes appends them. An M3 file still opens.

## Node header

Every index page:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Type `3` |
| 1 | 1 | Flags. Bit 0 (`0x01`) means a leaf. An internal node has flags `0`. |
| 2 | 2 | Number of keys in this node (separators, on an internal node) |
| 4 | 2 | `payload_end`, the first byte after the packed keys |
| 6 | 2 | Reserved `0` |
| 8 | 4 | Right sibling page id. Leaves only; `0` on an internal node or at the end of the leaf chain |
| 12 | 4 | Left sibling page id. Leaves only; `0` at the start of the chain |
| 16 | … | Keys, growing upward |
| end | 2 × keys | Directory of `u16` key offsets, growing downward. Key `i` is at byte `4096 - (i + 1) * 2` |

Integers are little-endian. The directory is there so a node can binary-search variable-length keys.

An empty tree is one leaf page with zero keys. That page is the root.

## Leaf keys

A leaf holds the indexed column and the heap address of the row. Rows stay in the heap. The index does not copy the rest of the row.

| Offset in key | Size | Field |
| --- | --- | --- |
| 0 | 2 | Encoded column length |
| 2 | length | Encoded column bytes |
| 2 + length | 4 | Heap page id |
| 6 + length | 2 | Heap slot |

Duplicate column values are allowed. The leaf order is the column encoding, then the heap page id, then the slot. Two entries with the same column and the same row id are rejected; that would be the same row inserted twice.

## Internal separators

An internal node starts with the leftmost child page id (`u32` at offset 16), then one separator per key:

| Offset in separator | Size | Field |
| --- | --- | --- |
| 0 | 2 | Encoded column length |
| 2 | length | Encoded column bytes of a leaf key |
| 2 + length | 4 | Heap page id of that leaf key |
| 6 + length | 2 | Heap slot of that leaf key |
| 8 + length | 4 | Child page id to the right of this separator |

The separator is a copy of a leaf key. The leaf still holds the row. All keys in the child to the left of a separator are strictly less than it. All keys in the subtree to the right are greater than or equal to it. After a delete the separator can be lower than the current minimum of the right subtree; it is not raised. Search still follows the same rule, then walks the leaf chain.

A leaf split promotes the first key of the new right leaf. An internal split promotes one separator and can leave a node with a single child. The root grows when a promotion has nowhere to go, and shrinks when the root is left with one child.

## Fanout

The usable region of a node is 4080 bytes (`4096 - 16`).

An `INT` column encodes to 8 bytes.

- A leaf key is 16 bytes plus a 2-byte directory slot, 18 bytes. A leaf holds at most **226** `INT` keys (`18 * 226 = 4068`, plus the 16-byte header, is 4084).
- An internal separator is 20 bytes plus a 2-byte directory slot. The leftmost child takes 4 bytes. An internal node holds at most **185** separators and **186** children (`185 * 22 + 4 = 4074`, plus the header, is 4090).

`TEXT` keys are longer, so a node holds fewer of them. A key whose encoded column is longer than **1024** bytes is rejected. `INT`, `FLOAT`, and `BOOLEAN` encodings are far under that cap. The cap is what keeps a single key able to sit in a leaf and in a separator with room to split.

## Column encoding

Comparison of the encoded bytes, shorter key first when one is a prefix of the other, matches the SQL comparison for that type.

| Column | Encoding |
| --- | --- |
| `INT` | 8-byte big-endian two's complement with the sign bit flipped, so `memcmp` orders negatives below positives |
| `FLOAT` | IEEE-754 binary64, sign bit flipped, and a negative value bit-inverted. `+0` and `-0` both store the `+0` pattern, because SQL treats them as equal. The parser has no unary minus, so SQL literals are non-negative; the encoding still orders a negative value |
| `BOOLEAN` | one byte, `0` or `1` |
| `TEXT` | the raw bytes, no terminator. Order matches `std::string` |

## What uses the index

`.explain <sql>` prints the plan and does not run the statement.

| Plan text | When |
| --- | --- |
| `Index Scan using <name> on <table>` | `SELECT`, `UPDATE`, or `DELETE` has a `WHERE` of `=`, `<`, `>`, `<=`, or `>=` on a column that has an index |
| `Seq Scan on <table>` | no `WHERE`, a `!=` comparison, or no index on that column |
| `No scan` | `CREATE`, `DROP`, `INSERT` |

If several indexes cover the same column, the oldest one is used. `!=` does not use an index. `TEXT` and `BOOLEAN` still reject `<`, `>`, `<=`, and `>=` at execution time; the tree can order those bytes, and the SQL layer does not offer the operators.

An index scan returns rows in index order. A heap scan returns them in insertion order. Those orders differ when the predicate matches more than one row and the rows were not inserted in index order.

`DELETE` without `WHERE` does not walk the index key by key. It frees the heap and replaces each index with a new empty root, then frees the old tree.

`UPDATE` removes and reinserts an index entry only when the encoded key changed. The heap slot stays the row id, including when the row moves to an overflow page, so an update of some other column leaves the index entry where it is.

## Page reads

`Database::page_reads` counts calls to `Pager::read_page`, including pages already in the process cache. It is not a count of disk I/O.

`Index.PointLookupReadsFewerPagesThanAScan` inserts 500 rows `(id INT, name TEXT)` and runs `SELECT id FROM t WHERE id = 250`. On a Debug build of this tree the heap scan performed **504** logical reads and the same statement after `CREATE INDEX idx ON t (id)` performed **5**. The scan number is large because the heap walker reads a page and then reads it again for every live row on it. The index path does not walk the other heap pages. The test's check is the inequality, not those two constants.

## Limitations

- Not a unique index. Duplicate column values are stored.
- One column. No composite key.
- No `NULL`. The SQL subset has no null, so every row is in the index.
- Index names are unique in the database and case-sensitive. `INDEX` and `ON` are reserved words.
- `TEXT` longer than 1024 bytes cannot be inserted into an indexed column, and `CREATE INDEX` fails if an existing value is that long. The table is left unchanged in that case.
- Deletes remove empty leaves and collapse a root that has one child. They do not borrow from a sibling to refill a node. Nodes can sit well under the maximum fanout. Leaf depth stays uniform; `btree_check` rejects a tree whose leaves disagree, whose separators are on the wrong side of a subtree, or whose leaf sibling chain does not match the tree.
- No write-ahead log and no `fsync`. A crash can tear a page, free a page the catalog still names, or leave an index entry for a row the heap no longer has (or the reverse). `CREATE INDEX` publishes the catalog entry only after the backfill; a crash during the backfill can leak the pages it allocated until the file is recreated.
- One writer. Two processes on the same file are undefined.
- The planner is the predicate check above. There is no cost model, no `AND`/`OR`, and no choice between two predicates.
