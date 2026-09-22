# Architecture

M5 has a process entry point, a shell, a SQL front end, a page file, a B+ tree index, an executor, and a text plan for that executor. Rows, table schemas, and index nodes are stored in that file.

## What exists

- `src/main.cpp` reads an optional database-file argument and starts the shell. The default path is `minidb.db` in the current directory. The file is created when it is missing or empty.
- `src/cli/repl.cpp` (`minidb::Repl`) reads one line at a time. It implements `.help`, `.tables`, `.schema <table>`, `.indexes`, `.explain <sql>`, `.exit`, and `.quit`. Command names are case-insensitive. Any other line that starts with `.` is an unknown meta-command. Every other non-empty line is parsed, then executed. Parse errors, execution errors, and storage errors are printed; the process stays up. A storage error while opening the file exits the process.
- `src/parser/lexer.cpp` (`minidb::Lexer`) turns a string into tokens. It does not know statement grammar.
- `src/parser/parser.cpp` (`minidb::parse_statement`) turns those tokens into one AST node. It throws `ParseError` (message, 1-based line, 1-based column) and does not execute anything.
- `src/parser/ast.cpp` formats a statement as a single summary line. That formatter is a printer for tests. The shell does not use it as a result.
- `src/storage/pager.cpp` (`minidb::Pager`) creates, reads, writes, and allocates 4 KiB pages. A free list reuses pages from `DROP` and from deleted overflow. `flush` calls `fflush` and does not `fsync`.
- `src/storage/record.cpp` encodes rows and catalog entries as little-endian bytes with a type tag per cell. It does not dump C++ objects.
- `src/storage/slotted_page.cpp` is the heap page: a slot directory, tombstones, and a forward pointer when an updated row no longer fits beside its neighbors.
- `src/storage/database.cpp` (`minidb::Database`) loads the catalog from those pages and applies create, drop, insert, update, delete, and heap scan. `Database()` with no path uses an in-memory pager so tests can run without a file. Destroying a file-backed database flushes it.
- `src/catalog/value.cpp` stores one cell as `INT`, `FLOAT`, `TEXT`, or `BOOLEAN`.
- `src/index/btree.cpp` is the B+ tree. Nodes are index pages in the pager. It does not know SQL. `Database` encodes column values, stores the root page id in the catalog, and calls the tree on insert, update, and delete.
- `src/executor/executor.cpp` (`minidb::execute`) visits one AST node. It does not lex, parse, or open files. It calls `Database` for every read and write. `WHERE` still lives here. A comparison other than `!=` reads the index when the catalog has one on that column. `.explain` uses the same choice, prints the predicate when there is one, and does not run the statement. It does not estimate a cost.

`include/minidb/` is the public header surface. Tests link the same static library as the `minidb` binary (`minidb_core`), so the shell and the executor can be driven without a terminal.

The AST is a `std::variant` of statement structs: `CreateTableStatement`, `DropTableStatement`, `CreateIndexStatement`, `DropIndexStatement`, `InsertStatement`, `SelectStatement`, `UpdateStatement`, and `DeleteStatement`. Literals carry the source spelling and a typed value (`int64`, `double`, `bool`, or string). The executor accepts a literal only when its kind matches the column type. `WHERE` on `TEXT` and `BOOLEAN` allows only `=` and `!=`.

The heap layout is [storage.md](storage.md). The B+ tree layout is [index.md](index.md).

## What does not exist

No write-ahead log, no `fsync`, no page checksum, and no wire protocol. A crash can tear a page or leave an index disagreeing with the heap. Queries with no usable index are heap scans. Pages that have been read stay in memory; nothing evicts them.

## Intended layering

```
CLI (M0)
  -> front end: lexer, parser, AST (M1)
    -> executor (M2)
      -> pages and heap files (M3)
        -> B+ tree index and a scan choice (M4)
          -> EXPLAIN text and a measured point lookup (M5)   <- this tree
```

The lexer does not call the parser. The parser does not call the executor. The executor does not call the parser, and it does not call the pager directly. Storage is reached through `Database`. The B+ tree is reached through `Database`, not from the executor.
