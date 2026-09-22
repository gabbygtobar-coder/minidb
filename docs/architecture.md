# Architecture

M0 has a process entry point and a shell. There is no database behind them.

## What exists

- `src/main.cpp` reads an optional data-directory argument and starts the shell. The path is printed in the banner. It is not created, opened, or written.
- `src/cli/repl.cpp` (`minidb::Repl`) reads one line at a time. It implements `.help`, `.exit`, and `.quit`. Every other non-empty line gets the same rejection: the SQL engine is not implemented yet.

`include/minidb/` is the public header surface. Tests link the same static library as the `minidb` binary (`minidb_core`), so the shell can be driven from a string stream without a terminal.

## What does not exist

No lexer, parser, AST, catalog, executor, pager, buffer pool, WAL, index, or wire protocol. Those stay out of the REPL. The shell should keep handing statements to a future engine instead of growing into one.

## Intended layering (not built)

```
CLI (M0)
  -> front end: lexer, parser, AST (M1)
    -> in-memory catalog and executor (M2)
      -> filters and expressions (M3)
        -> pages and buffer pool (M4)
          -> index and a small planner choice (M5)
```

Each box is a later milestone. None of the modules under the CLI are in this tree.
