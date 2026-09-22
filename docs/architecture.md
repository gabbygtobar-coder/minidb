# Architecture

M1 has a process entry point, a shell, and a SQL front end. There is no database behind them.

## What exists

- `src/main.cpp` reads an optional data-directory argument and starts the shell. The path is printed in the banner. It is not created, opened, or written.
- `src/cli/repl.cpp` (`minidb::Repl`) reads one line at a time. It implements `.help`, `.exit`, and `.quit`. Any other line that starts with `.` is an unknown meta-command. Every other non-empty line is handed to the parser.
- `src/parser/lexer.cpp` (`minidb::Lexer`) turns a string into tokens. It does not know statement grammar.
- `src/parser/parser.cpp` (`minidb::parse_statement`) turns those tokens into one AST node. It throws `ParseError` (message, 1-based line, 1-based column) and does not execute anything.
- `src/parser/ast.cpp` formats a statement as a single summary line. That formatter is not an executor.

`include/minidb/` is the public header surface. Tests link the same static library as the `minidb` binary (`minidb_core`), so the shell and the parser can be driven without a terminal.

The AST is a `std::variant` of statement structs: `CreateTableStatement`, `DropTableStatement`, `InsertStatement`, `SelectStatement`, `UpdateStatement`, and `DeleteStatement`. Literals carry the source spelling and a typed value (`int64`, `double`, `bool`, or string).

## What does not exist

No catalog, executor, pager, buffer pool, WAL, index, or wire protocol. The shell prints the AST or a parse error and stops. It does not invent rows.

## Intended layering

```
CLI (M0)
  -> front end: lexer, parser, AST (M1)   <- this tree
    -> in-memory catalog and executor (M2)
      -> filters and expressions (M3)
        -> pages and buffer pool (M4)
          -> index and a small planner choice (M5)
```

The lexer does not call the parser. The parser does not call an executor. Later milestones should consume the AST from the outside instead of growing execution into the parser.
