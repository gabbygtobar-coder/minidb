# MiniDB

MiniDB is a relational database engine written in C++. The engine is the project: storage, execution, and a small SQL surface built in this repository. It does not wrap, embed, or speak for PostgreSQL, SQLite, or any other existing database.

## Why

This is the systems piece of a CS portfolio, after WorthIt and ThreatLens. Those projects sit higher in the stack. MiniDB is the one that has to deal with files, memory, and query execution directly, in the open, one milestone at a time.

## Status

**M0 — skeleton only.** The tree configures with CMake, builds a `minidb` CLI, runs a handful of GoogleTests, and checks that on GitHub Actions. The shell prints a banner and accepts three meta-commands. It does not parse or execute SQL.

## Stack

- C++17 (the code stays inside that dialect; a newer compiler is fine)
- CMake 3.16 or newer
- [GoogleTest](https://github.com/google/googletest) 1.15.2, downloaded by CMake FetchContent

GoogleTest is not a git submodule and it is not vendored. `FetchContent` pins the v1.15.2 release archive in `CMakeLists.txt` (URL plus SHA256). The version sits next to the test target, CI does not need a recursive clone, and googletest's history stays out of this repository. The first configure needs network access so CMake can download the archive. Later configures reuse CMake's fetch cache. Pass `-DBUILD_TESTING=OFF` to skip the download and build only the CLI.

## Roadmap

| Milestone | Planned scope | In this tree |
| --- | --- | --- |
| M0 | CMake, CLI stub, GoogleTest, CI | Yes |
| M1 | Lexer, parser, and AST for a tiny SQL subset | No |
| M2 | In-memory catalog and execution (`CREATE TABLE`, `INSERT`, `SELECT`) | No |
| M3 | Simple expressions and `WHERE` filters | No |
| M4 | On-disk pages and a buffer pool so data survives restart | No |
| M5 | One secondary index and a trivial access-path choice | No |

Milestones land in order. M1 does not start by hiding a parser inside the shell.

## Build

Requirements: a C++17 compiler (g++ or clang++), CMake 3.16+, and network on the first configure when tests are enabled.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The binary is `build/minidb`.

## Run

```bash
./build/minidb
./build/minidb /tmp/minidb-data
./build/minidb --help
```

The shell prints:

```text
MiniDB v0.1
Database: local
MiniDB>
```

With a path argument, the second line uses that path instead of `local`.

| Input | Result |
| --- | --- |
| `.help` | Lists meta-commands |
| `.exit`, `.quit` | Leaves the shell with status 0 |
| empty line | Another prompt; no error |
| anything else | `SQL engine is not implemented yet (M1+).` |

End of input (Ctrl-D) also exits with status 0. Meta-commands are case-sensitive. Surrounding whitespace is ignored.

## Test

```bash
cmake --build build
cd build && ctest --output-on-failure
```

`minidb_tests` covers the version string, the banner, `.help` / `.exit` / `.quit`, empty lines, and the rejection message for other input.

## Layout

```text
include/minidb/     public headers (version, shell)
src/main.cpp        process entry
src/cli/            shell implementation
tests/              GoogleTest
benchmarks/         placeholder until there is an engine to measure
docs/               architecture notes for M0
examples/           sample shell session
```

## Limitations

- No SQL. Statements are rejected with an explicit message. There is no lexer or parser.
- No tables, rows, types, indexes, planner, transactions, or concurrency control.
- No files are read or written. The data-directory argument is only copied into the banner.
- One process, one thread, stdin/stdout. There is no client/server protocol and no wire compatibility with PostgreSQL or MySQL.
- `benchmarks/` is a note, not a harness.
- Correctness work past the shell stub has not started. A green test run means the skeleton builds and the stub behaves as documented.

## License

[MIT](LICENSE).
