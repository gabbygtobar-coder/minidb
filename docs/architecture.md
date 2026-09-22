# Architecture

M2 has a process entry point, a shell, a SQL front end, an in-memory catalog, and an executor. Nothing is stored on disk.

## What exists

- `src/main.cpp` reads an optional data-directory argument and starts the shell. The path is printed in the banner. It is not created, opened, or written.
- `src/cli/repl.cpp` (`minidb::Repl`) reads one line at a time. It implements `.help`, `.tables`, `.schema <table>`, `.exit`, and `.quit`. Any other line that starts with `.` is an unknown meta-command. Every other non-empty line is parsed, then executed. Parse errors and execution errors are printed; the process stays up.
- `src/parser/lexer.cpp` (`minidb::Lexer`) turns a string into tokens. It does not know statement grammar.
- `src/parser/parser.cpp` (`minidb::parse_statement`) turns those tokens into one AST node. It throws `ParseError` (message, 1-based line, 1-based column) and does not execute anything.
- `src/parser/ast.cpp` formats a statement as a single summary line. That formatter is a printer for tests. The shell does not use it as a result.
- `src/catalog/catalog.cpp` (`minidb::Database`) holds tables in a `std::map`. A table is a schema plus a vector of rows in insertion order. Dropping a table drops its rows. Destroying the database drops everything.
- `src/catalog/value.cpp` stores one cell as `INT`, `FLOAT`, `TEXT`, or `BOOLEAN`.
- `src/executor/executor.cpp` (`minidb::execute`) visits one AST node and mutates the catalog or returns a result set. It does not lex, parse, or touch the filesystem.

`include/minidb/` is the public header surface. Tests link the same static library as the `minidb` binary (`minidb_core`), so the shell and the executor can be driven without a terminal.

The AST is a `std::variant` of statement structs: `CreateTableStatement`, `DropTableStatement`, `InsertStatement`, `SelectStatement`, `UpdateStatement`, and `DeleteStatement`. Literals carry the source spelling and a typed value (`int64`, `double`, `bool`, or string). The executor accepts a literal only when its kind matches the column type. `WHERE` on `TEXT` and `BOOLEAN` allows only `=` and `!=`.

## What does not exist

No pager, buffer pool, WAL, index, or wire protocol. Restarting the process starts from an empty catalog.

## Intended layering

```
CLI (M0)
  -> front end: lexer, parser, AST (M1)
    -> in-memory catalog and executor (M2)   <- this tree
      -> pages and buffer pool (M3)
        -> index and a small planner choice (M4)
```

The lexer does not call the parser. The parser does not call the executor. The executor does not call the parser. Later milestones should keep that boundary and replace the in-memory rows with paged storage from the outside.
